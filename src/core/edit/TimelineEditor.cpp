// SPDX-License-Identifier: GPL-3.0-or-later
#include "TimelineEditor.h"

#include "core/edit/SequenceDiff.h"
#include "core/project/ClipTime.h"
#include "core/project/SpeedCurve.h"
#include "core/serialization/ProjectJson.h"

#include <QCoreApplication>

#include <algorithm>
#include <cmath>

namespace vedit {

namespace {

QString tr(const char *text)
{
    return QCoreApplication::translate("vedit::TimelineEditor", text);
}

struct ClipRef
{
    Track *track = nullptr;
    size_t index = 0;
    bool audio = false;
    int trackIndex = -1;

    Clip &clip() const { return track->clips[index]; }
};

std::optional<ClipRef> findClip(Sequence &sequence, const ClipId &clipId)
{
    for (int pass = 0; pass < 2; ++pass) {
        auto &tracks = pass == 0 ? sequence.visualTracks : sequence.audioTracks;
        for (size_t t = 0; t < tracks.size(); ++t) {
            const int index = tracks[t].clipIndex(clipId);
            if (index >= 0) {
                return ClipRef{&tracks[t], static_cast<size_t>(index), pass == 1, static_cast<int>(t)};
            }
        }
    }
    return std::nullopt;
}

struct TrackRef
{
    Track *track = nullptr;
    bool audio = false;
    int index = -1;
};

std::optional<TrackRef> findTrack(Sequence &sequence, const TrackId &trackId)
{
    for (int pass = 0; pass < 2; ++pass) {
        auto &tracks = pass == 0 ? sequence.visualTracks : sequence.audioTracks;
        for (size_t t = 0; t < tracks.size(); ++t) {
            if (tracks[t].id == trackId) {
                return TrackRef{&tracks[t], pass == 1, static_cast<int>(t)};
            }
        }
    }
    return std::nullopt;
}

bool isMagneticMain(const Sequence &sequence, const Track &track)
{
    return sequence.magneticMain && !sequence.visualTracks.empty() && sequence.visualTracks.front().id == track.id;
}

const Transition *transitionBetween(const Track &track, const ClipId &from, const ClipId &to)
{
    for (const Transition &transition : track.transitions) {
        if (transition.from == from && transition.to == to) {
            return &transition;
        }
    }
    return nullptr;
}

// Drops transitions whose clips are gone or no longer adjacent (in vector order).
void pruneTransitions(Track &track)
{
    std::erase_if(track.transitions, [&track](const Transition &transition) {
        const int from = track.clipIndex(transition.from);
        const int to = track.clipIndex(transition.to);
        return from < 0 || to < 0 || to != from + 1;
    });
}

// Magnetic main track: vector order is the desired order; clips are packed from 0 without gaps
// (an overlap transition makes its two clips overlap by its duration).
void pack(Track &track, const Rational &rate)
{
    pruneTransitions(track);
    RationalTime cursor(0, rate);
    for (size_t i = 0; i < track.clips.size(); ++i) {
        Clip &clip = track.clips[i];
        if (i > 0) {
            const Transition *transition = transitionBetween(track, track.clips[i - 1].id, clip.id);
            if (transition && transition->alignment == TransitionAlignment::Overlap) {
                cursor -= transition->duration;
            }
        }
        clip.start = cursor;
        cursor += clip.duration;
    }
}

bool hasRoom(const Track &track, const TimeRange &range, const ClipId &ignore = {})
{
    return std::none_of(track.clips.begin(), track.clips.end(), [&](const Clip &clip) {
        return clip.id != ignore && clip.range().intersects(range);
    });
}

void insertSorted(Track &track, Clip clip)
{
    const auto position = std::upper_bound(track.clips.begin(), track.clips.end(), clip.start,
                                           [](const RationalTime &t, const Clip &c) { return t < c.start; });
    track.clips.insert(position, std::move(clip));
}

Clip takeClip(Track &track, size_t index)
{
    Clip clip = std::move(track.clips[index]);
    track.clips.erase(track.clips.begin() + static_cast<std::ptrdiff_t>(index));
    std::erase_if(track.transitions,
                  [&clip](const Transition &t) { return t.from == clip.id || t.to == clip.id; });
    return clip;
}

// Index in the main track where a clip dropped at `position` goes: before the clip whose first half
// contains the position, otherwise after it.
size_t boundaryIndex(const Track &track, const RationalTime &position)
{
    for (size_t i = 0; i < track.clips.size(); ++i) {
        const Clip &clip = track.clips[i];
        // position < start + duration / 2  <=>  2 * position < 2 * start + duration
        if (position * 2 < clip.start * 2 + clip.duration) {
            return i;
        }
    }
    return track.clips.size();
}

Track makeTrack(TrackKind kind)
{
    Track track;
    track.id = TrackId::create();
    track.kind = kind;
    return track;
}

// A new track for clips coming from `other` (a caption track stays a caption track, with its style).
Track makeTrackLike(const Track &other)
{
    Track track = makeTrack(other.kind);
    track.captions = other.captions;
    track.extras = other.extras;
    return track;
}

void removeEmptyTracks(Sequence &sequence)
{
    if (sequence.visualTracks.size() > 1) {
        std::erase_if(sequence.visualTracks, [&sequence](const Track &track) {
            return track.clips.empty() && track.id != sequence.visualTracks.front().id;
        });
    }
    std::erase_if(sequence.audioTracks, [](const Track &track) { return track.clips.empty(); });
}

void cleanGroups(Sequence &sequence)
{
    for (Group &group : sequence.groups) {
        std::erase_if(group.clipIds, [&sequence](const ClipId &id) {
            for (const auto *tracks : {&sequence.visualTracks, &sequence.audioTracks}) {
                for (const Track &track : *tracks) {
                    if (track.clipIndex(id) >= 0) {
                        return false;
                    }
                }
            }
            return true;
        });
    }
    std::erase_if(sequence.groups, [](const Group &group) { return group.clipIds.size() < 2; });
}

// First overlay track (above the main one) of `kind` with room for `range`, or a new one on top.
Track &overlayTrackFor(Sequence &sequence, TrackKind kind, const TimeRange &range)
{
    for (size_t i = 1; i < sequence.visualTracks.size(); ++i) {
        Track &track = sequence.visualTracks[i];
        if (track.kind == kind && !track.captions && !track.locked && hasRoom(track, range)) {
            return track;
        }
    }
    sequence.visualTracks.push_back(makeTrack(kind));
    return sequence.visualTracks.back();
}

Track &audioTrackFor(Sequence &sequence, const TimeRange &range)
{
    for (Track &track : sequence.audioTracks) {
        if (!track.locked && hasRoom(track, range)) {
            return track;
        }
    }
    sequence.audioTracks.push_back(makeTrack(TrackKind::Audio));
    return sequence.audioTracks.back();
}

// Source frames consumed by `frames` timeline frames at `speed`.
std::int64_t sourceFrames(std::int64_t frames, double speed)
{
    return static_cast<std::int64_t>(std::llround(static_cast<double>(frames) * speed));
}

// Longest timeline duration (frames) available from `sourceIn` given the media length.
std::optional<std::int64_t> maxFramesFrom(const Media *media, const MediaClipData &data, const Rational &rate)
{
    if (!media || media->kind == MediaKind::Image || !media->info.duration) {
        return std::nullopt; // stills are unbounded
    }
    const std::int64_t length = media->info.duration->rescaled(rate, Rounding::Floor).value();
    const double available = static_cast<double>(length - data.sourceIn.value()) / data.speed;
    return static_cast<std::int64_t>(std::floor(available + 1e-9));
}

// Clip-local keyframes (generated clips) must follow the content when the clip start moves.
void shiftClipLocalTime(Clip &clip, const RationalTime &delta)
{
    if (clip.kind() == ClipKind::Media || clip.kind() == ClipKind::Compound) {
        return; // keyframes of media and compound clips are in source time (docs/FILE_FORMAT.md §3.5)
    }
    const auto shiftParam = [&delta](Param &param) {
        if (!param.isAnimated()) {
            return;
        }
        std::vector<Keyframe> keyframes = param.keyframes();
        for (Keyframe &keyframe : keyframes) {
            keyframe.time -= delta;
        }
        param.setKeyframes(std::move(keyframes));
    };
    shiftParam(clip.transform.position);
    shiftParam(clip.transform.scale);
    shiftParam(clip.transform.rotation);
    shiftParam(clip.transform.crop.left);
    shiftParam(clip.transform.crop.top);
    shiftParam(clip.transform.crop.right);
    shiftParam(clip.transform.crop.bottom);
    shiftParam(clip.opacity);
    for (Effect &effect : clip.effects) {
        shiftParam(effect.intensity);
        for (auto &[name, param] : effect.params) {
            shiftParam(param);
        }
    }
    if (auto *color = std::get_if<ColorClipData>(&clip.payload)) {
        shiftParam(color->color);
    }
    if (auto *line = std::get_if<SubtitleClipData>(&clip.payload)) {
        for (TimedWord &word : line->words) {
            word.start -= delta.rescaled(word.start.rate(), Rounding::NearestEven);
            word.end -= delta.rescaled(word.end.rate(), Rounding::NearestEven);
        }
    }
    for (Marker &marker : clip.markers) {
        marker.time -= delta;
    }
}

// Keeps transition durations within the clips they connect after a trim.
void clampTransitions(Track &track)
{
    for (Transition &transition : track.transitions) {
        const Clip *from = track.findClip(transition.from);
        const Clip *to = track.findClip(transition.to);
        if (!from || !to) {
            continue;
        }
        const RationalTime limit = std::min(from->duration, to->duration);
        if (transition.duration > limit) {
            transition.duration = limit;
        }
    }
}

} // namespace

TimelineEditor::TimelineEditor(const ProjectData &project, const SequenceId &sequenceId)
    : m_project(project)
    , m_sequence(project.findSequence(sequenceId))
    , m_rate(project.settings.frameRate)
{
}

EditResult TimelineEditor::fail(const QString &message) const
{
    EditResult result;
    result.error = message;
    return result;
}

EditResult TimelineEditor::finish(Sequence &&modified, const QString &text, const ClipId &primary) const
{
    EditResult result;
    result.script = diffSequence(*m_sequence, modified);
    result.text = text;
    result.primaryClip = primary;
    return result;
}

ClipAnimation TimelineEditor::kenBurns()
{
    ClipAnimation animation;
    animation.type = AssetRef{QStringLiteral("vedit.core"), QStringLiteral("animations/loop/ken_burns"), 1};
    animation.duration = RationalTime(0, Rational(30));
    animation.easing = Easing::preset(Easing::Preset::Linear);
    return animation;
}

EditResult TimelineEditor::insertMedia(const MediaId &mediaId, const RationalTime &position,
                                       std::optional<TimeRange> sourceRange, Placement placement)
{
    if (!m_sequence) {
        return fail(tr("The sequence does not exist."));
    }
    const Media *media = m_project.findMedia(mediaId);
    if (!media) {
        return fail(tr("The media is not in the project."));
    }
    const bool audioOnly = media->kind == MediaKind::Audio;

    RationalTime sourceIn(0, m_rate);
    RationalTime duration(0, m_rate);
    const std::optional<RationalTime> length =
        media->info.duration ? std::optional(media->info.duration->rescaled(m_rate, Rounding::Floor)) : std::nullopt;
    if (sourceRange) {
        sourceIn = sourceRange->start.rescaled(m_rate, Rounding::NearestEven);
        duration = sourceRange->duration.rescaled(m_rate, Rounding::NearestEven);
        if (sourceIn.isNegative() || (length && media->kind != MediaKind::Image && sourceIn + duration > *length)) {
            return fail(tr("The selected range is outside the media."));
        }
    } else if (media->kind == MediaKind::Image) {
        duration = RationalTime::fromSeconds(Rational(kDefaultImageSeconds), m_rate, Rounding::NearestEven);
    } else if (length) {
        duration = *length;
    } else {
        return fail(tr("The duration of this media is unknown."));
    }
    if (duration.value() <= 0) {
        return fail(tr("The media is too short to be used."));
    }

    Clip clip;
    clip.id = ClipId::create();
    clip.start = position.rescaled(m_rate, Rounding::NearestEven);
    if (clip.start.isNegative()) {
        clip.start = RationalTime(0, m_rate);
    }
    clip.duration = duration;
    MediaClipData data;
    data.mediaId = mediaId;
    data.sourceIn = sourceIn;
    data.streams = audioOnly ? Streams::AudioOnly : (media->info.audio ? Streams::AudioVideo : Streams::VideoOnly);
    clip.payload = data;
    if (media->kind == MediaKind::Image) {
        clip.animations.loop = kenBurns();
    }
    const ClipId clipId = clip.id;

    Sequence modified = *m_sequence;
    if (audioOnly) {
        audioTrackFor(modified, clip.range()).clips.push_back(std::move(clip));
        return finish(std::move(modified), tr("Add audio"), clipId);
    }
    Track &main = modified.visualTracks.front();
    if (placement == Placement::Auto && isMagneticMain(modified, main)) {
        if (main.locked) {
            return fail(tr("The main track is locked."));
        }
        const size_t index = boundaryIndex(main, clip.start);
        main.clips.insert(main.clips.begin() + static_cast<std::ptrdiff_t>(index), std::move(clip));
        pack(main, m_rate);
        return finish(std::move(modified), tr("Add clip"), clipId);
    }
    if (placement == Placement::Auto && !main.locked && hasRoom(main, clip.range())) {
        insertSorted(main, std::move(clip));
        return finish(std::move(modified), tr("Add clip"), clipId);
    }
    Track &overlay = overlayTrackFor(modified, TrackKind::Video, clip.range());
    insertSorted(overlay, std::move(clip));
    return finish(std::move(modified), tr("Add clip"), clipId);
}

EditResult TimelineEditor::moveClip(const ClipId &clipId, const RationalTime &requestedStart,
                                    std::optional<TrackId> targetTrackId)
{
    if (!m_sequence) {
        return fail(tr("The sequence does not exist."));
    }
    Sequence modified = *m_sequence;
    const auto source = findClip(modified, clipId);
    if (!source) {
        return fail(tr("The clip does not exist."));
    }
    if (source->track->locked) {
        return fail(tr("The track is locked."));
    }
    const TrackId sourceTrackId = source->track->id;
    const TrackId targetId = targetTrackId.value_or(sourceTrackId);
    {
        const auto target = findTrack(modified, targetId);
        if (!target) {
            return fail(tr("The track does not exist."));
        }
        if (target->track->locked) {
            return fail(tr("The track is locked."));
        }
        if (!clipAllowedOnTrack(source->clip(), *target->track)) {
            return fail(tr("This clip cannot be placed on that track."));
        }
    }

    const bool sourceMagnetic = isMagneticMain(modified, *source->track);
    Clip clip = takeClip(*source->track, source->index);
    if (sourceMagnetic) {
        pack(*source->track, m_rate);
    }
    RationalTime start = requestedStart.rescaled(m_rate, Rounding::NearestEven);
    if (start.isNegative()) {
        start = RationalTime(0, m_rate);
    }
    clip.start = start;

    auto target = findTrack(modified, targetId);
    if (isMagneticMain(modified, *target->track)) {
        const size_t index = boundaryIndex(*target->track, start);
        target->track->clips.insert(target->track->clips.begin() + static_cast<std::ptrdiff_t>(index), std::move(clip));
        pack(*target->track, m_rate);
    } else if (hasRoom(*target->track, clip.range())) {
        insertSorted(*target->track, std::move(clip));
    } else {
        // Never overwrite: create a new track right above (visual) or below (audio) the target.
        auto &tracks = target->audio ? modified.audioTracks : modified.visualTracks;
        Track fresh = makeTrackLike(*target->track);
        insertSorted(fresh, std::move(clip));
        tracks.insert(tracks.begin() + target->index + 1, std::move(fresh));
    }
    removeEmptyTracks(modified);
    cleanGroups(modified);
    return finish(std::move(modified), tr("Move clip"), clipId);
}

EditResult TimelineEditor::trimClip(const ClipId &clipId, ClipEdge edge, const RationalTime &requestedTime)
{
    if (!m_sequence) {
        return fail(tr("The sequence does not exist."));
    }
    Sequence modified = *m_sequence;
    const auto ref = findClip(modified, clipId);
    if (!ref) {
        return fail(tr("The clip does not exist."));
    }
    Track &track = *ref->track;
    if (track.locked) {
        return fail(tr("The track is locked."));
    }
    const bool magnetic = isMagneticMain(modified, track);
    Clip &clip = ref->clip();
    MediaClipData *media = clip.media();
    const Media *mediaItem = media ? m_project.findMedia(media->mediaId) : nullptr;
    const RationalTime time = requestedTime.rescaled(m_rate, Rounding::NearestEven);
    const RationalTime oneFrame(1, m_rate);

    if (edge == ClipEdge::Start) {
        RationalTime delta = time - clip.start;
        // At least one frame must remain.
        if (delta >= clip.duration) {
            delta = clip.duration - oneFrame;
        }
        // Extending to the left is limited by the material before sourceIn…
        if (media && delta.isNegative() && (!mediaItem || mediaItem->kind != MediaKind::Image)) {
            const auto available = static_cast<std::int64_t>(std::floor(static_cast<double>(media->sourceIn.value()) / media->speed + 1e-9));
            delta = std::max(delta, RationalTime(-available, m_rate));
        }
        // …and, off the magnetic track, by the previous clip and by 0.
        if (!magnetic) {
            RationalTime lowest(0, m_rate);
            if (ref->index > 0) {
                lowest = track.clips[ref->index - 1].end();
            }
            delta = std::max(delta, lowest - clip.start);
        }
        if (delta.isZero()) {
            return EditResult{};
        }
        if (media) {
            media->sourceIn += RationalTime(sourceFrames(delta.value(), media->speed), m_rate);
            if (media->sourceIn.isNegative()) {
                media->sourceIn = RationalTime(0, m_rate);
            }
        } else {
            shiftClipLocalTime(clip, delta);
        }
        clip.duration -= delta;
        if (!magnetic) {
            clip.start += delta;
        }
    } else {
        RationalTime newDuration = time - clip.start;
        if (newDuration < oneFrame) {
            newDuration = oneFrame;
        }
        if (media) {
            if (const auto maxFrames = maxFramesFrom(mediaItem, *media, m_rate)) {
                newDuration = std::min(newDuration, RationalTime(*maxFrames, m_rate));
            }
        }
        if (!magnetic && ref->index + 1 < track.clips.size()) {
            newDuration = std::min(newDuration, track.clips[ref->index + 1].start - clip.start);
        }
        if (newDuration == clip.duration) {
            return EditResult{};
        }
        clip.duration = newDuration;
    }
    clampTransitions(track);
    if (magnetic) {
        pack(track, m_rate);
    }
    return finish(std::move(modified), tr("Trim clip"), clipId);
}

EditResult TimelineEditor::rippleTrimClip(const ClipId &clipId, ClipEdge edge, const RationalTime &requestedTime)
{
    if (!m_sequence) {
        return fail(tr("The sequence does not exist."));
    }
    Sequence modified = *m_sequence;
    const auto ref = findClip(modified, clipId);
    if (!ref) {
        return fail(tr("The clip does not exist."));
    }
    Track &track = *ref->track;
    if (track.locked) {
        return fail(tr("The track is locked."));
    }
    const bool magnetic = isMagneticMain(modified, track);
    Clip &clip = ref->clip();
    MediaClipData *media = clip.media();
    const RationalTime time = requestedTime.rescaled(m_rate, Rounding::NearestEven);
    const RationalTime oneFrame(1, m_rate);

    if (edge == ClipEdge::Start) {
        // Q: Ripple trim from start of clip to playhead
        if (time <= clip.start) {
            return EditResult{};
        }
        RationalTime delta = time - clip.start;
        if (delta >= clip.duration) {
            delta = clip.duration - oneFrame;
        }
        if (delta <= RationalTime(0, m_rate)) {
            return EditResult{};
        }
        if (media) {
            media->sourceIn += RationalTime(sourceFrames(delta.value(), media->speed), m_rate);
            if (media->sourceIn.isNegative()) {
                media->sourceIn = RationalTime(0, m_rate);
            }
        } else if (CompoundClipData *compound = clip.compound()) {
            compound->sourceIn += delta;
        } else {
            shiftClipLocalTime(clip, delta);
        }
        clip.duration -= delta;
        if (magnetic) {
            pack(track, m_rate);
        } else {
            for (size_t i = ref->index + 1; i < track.clips.size(); ++i) {
                track.clips[i].start -= delta;
            }
        }
    } else {
        // W: Ripple trim from playhead to end of clip
        if (time >= clip.end()) {
            return EditResult{};
        }
        RationalTime newDuration = time - clip.start;
        if (newDuration < oneFrame) {
            newDuration = oneFrame;
        }
        if (newDuration >= clip.duration) {
            return EditResult{};
        }
        RationalTime delta = clip.duration - newDuration;
        clip.duration = newDuration;
        if (magnetic) {
            pack(track, m_rate);
        } else {
            for (size_t i = ref->index + 1; i < track.clips.size(); ++i) {
                track.clips[i].start -= delta;
            }
        }
    }
    clampTransitions(track);
    return finish(std::move(modified), edge == ClipEdge::Start ? tr("Ripple trim start to playhead") : tr("Ripple trim playhead to end"), clipId);
}

EditResult TimelineEditor::splitClip(const ClipId &clipId, const RationalTime &requestedTime)
{
    if (!m_sequence) {
        return fail(tr("The sequence does not exist."));
    }
    Sequence modified = *m_sequence;
    const auto ref = findClip(modified, clipId);
    if (!ref) {
        return fail(tr("The clip does not exist."));
    }
    Track &track = *ref->track;
    if (track.locked) {
        return fail(tr("The track is locked."));
    }
    const RationalTime time = requestedTime.rescaled(m_rate, Rounding::NearestEven);
    Clip &first = ref->clip();
    if (!(time > first.start && time < first.end())) {
        return fail(tr("Move the playhead inside the clip to split it."));
    }
    const RationalTime offset = time - first.start;

    Clip second = first;
    second.id = ClipId::create();
    second.start = time;
    second.duration = first.duration - offset;
    for (Effect &effect : second.effects) {
        effect.id = EffectId::create();
    }
    for (Marker &marker : second.markers) {
        marker.id = MarkerId::create();
    }
    if (MediaClipData *media = second.media()) {
        media->sourceIn += RationalTime(sourceFrames(offset.value(), media->speed), m_rate);
        // Fades belong to the outer edges of the original clip.
        media->audio.fadeIn.reset();
        if (MediaClipData *firstMedia = first.media()) {
            firstMedia->audio.fadeOut.reset();
        }
    } else if (CompoundClipData *compound = second.compound()) {
        compound->sourceIn += offset;
    } else {
        shiftClipLocalTime(second, offset);
    }
    if (const SubtitleClipData *line = first.subtitle()) {
        // Each half keeps the words said in it.
        auto [before, after] = captions::split(*line, first.duration, offset);
        first.payload = std::move(before);
        second.payload = std::move(after);
    }
    first.duration = offset;
    // A transition that left the original clip now leaves its second half.
    for (Transition &transition : track.transitions) {
        if (transition.from == first.id) {
            transition.from = second.id;
        }
    }
    const ClipId secondId = second.id;
    track.clips.insert(track.clips.begin() + static_cast<std::ptrdiff_t>(ref->index) + 1, std::move(second));
    clampTransitions(track);
    if (isMagneticMain(modified, track)) {
        pack(track, m_rate);
    }
    return finish(std::move(modified), tr("Split clip"), secondId);
}

EditResult TimelineEditor::deleteClips(const std::vector<ClipId> &clipIds)
{
    if (!m_sequence) {
        return fail(tr("The sequence does not exist."));
    }
    if (clipIds.empty()) {
        return EditResult{};
    }
    Sequence modified = *m_sequence;
    for (const ClipId &clipId : clipIds) {
        const auto ref = findClip(modified, clipId);
        if (!ref) {
            return fail(tr("The clip does not exist."));
        }
        if (ref->track->locked) {
            return fail(tr("The track is locked."));
        }
        takeClip(*ref->track, ref->index);
    }
    Track &main = modified.visualTracks.front();
    if (isMagneticMain(modified, main)) {
        pack(main, m_rate);
    }
    for (int pass = 0; pass < 2; ++pass) {
        for (Track &track : pass == 0 ? modified.visualTracks : modified.audioTracks) {
            pruneTransitions(track);
        }
    }
    removeEmptyTracks(modified);
    cleanGroups(modified);
    return finish(std::move(modified), clipIds.size() == 1 ? tr("Delete clip") : tr("Delete clips"), {});
}

EditResult TimelineEditor::rippleDeleteClips(const std::vector<ClipId> &clipIds)
{
    if (!m_sequence) {
        return fail(tr("The sequence does not exist."));
    }
    if (clipIds.empty()) {
        return EditResult{};
    }
    Sequence modified = *m_sequence;
    std::map<TrackId, std::vector<ClipId>> toDeleteByTrack;
    for (const ClipId &clipId : clipIds) {
        const auto ref = findClip(modified, clipId);
        if (!ref) {
            return fail(tr("The clip does not exist."));
        }
        if (ref->track->locked) {
            return fail(tr("The track is locked."));
        }
        toDeleteByTrack[ref->track->id].push_back(clipId);
    }

    for (auto &[trackId, ids] : toDeleteByTrack) {
        Track *track = nullptr;
        for (Track &t : modified.visualTracks) {
            if (t.id == trackId) {
                track = &t;
                break;
            }
        }
        if (!track) {
            for (Track &t : modified.audioTracks) {
                if (t.id == trackId) {
                    track = &t;
                    break;
                }
            }
        }
        if (!track) {
            continue;
        }

        std::sort(ids.begin(), ids.end(), [&](const ClipId &a, const ClipId &b) {
            const Clip *ca = track->findClip(a);
            const Clip *cb = track->findClip(b);
            return (ca && cb) ? ca->start > cb->start : false;
        });

        for (const ClipId &clipId : ids) {
            const int idx = track->clipIndex(clipId);
            if (idx >= 0) {
                const Clip removed = takeClip(*track, static_cast<size_t>(idx));
                const RationalTime dur = removed.duration;
                for (size_t i = static_cast<size_t>(idx); i < track->clips.size(); ++i) {
                    track->clips[i].start = std::max(RationalTime(0, m_rate), track->clips[i].start - dur);
                }
            }
        }
        pruneTransitions(*track);
    }

    Track &main = modified.visualTracks.front();
    if (isMagneticMain(modified, main)) {
        pack(main, m_rate);
    }
    for (int pass = 0; pass < 2; ++pass) {
        for (Track &track : pass == 0 ? modified.visualTracks : modified.audioTracks) {
            pruneTransitions(track);
        }
    }
    removeEmptyTracks(modified);
    cleanGroups(modified);
    return finish(std::move(modified), clipIds.size() == 1 ? tr("Ripple delete clip") : tr("Ripple delete clips"), {});
}

EditResult TimelineEditor::duplicateClips(const std::vector<ClipId> &clipIds)
{
    if (!m_sequence) {
        return fail(tr("The sequence does not exist."));
    }
    if (clipIds.empty()) {
        return EditResult{};
    }
    Sequence modified = *m_sequence;
    ClipId lastCopy;
    for (const ClipId &clipId : clipIds) {
        const auto ref = findClip(modified, clipId);
        if (!ref) {
            return fail(tr("The clip does not exist."));
        }
        if (ref->track->locked) {
            return fail(tr("The track is locked."));
        }
        Clip copy = ref->clip();
        copy.id = ClipId::create();
        copy.linkId = {};
        for (Effect &effect : copy.effects) {
            effect.id = EffectId::create();
        }
        for (Marker &marker : copy.markers) {
            marker.id = MarkerId::create();
        }
        copy.start = ref->clip().end();
        lastCopy = copy.id;
        Track &track = *ref->track;
        if (isMagneticMain(modified, track)) {
            track.clips.insert(track.clips.begin() + static_cast<std::ptrdiff_t>(ref->index + 1), std::move(copy));
            pack(track, m_rate);
        } else if (hasRoom(track, copy.range())) {
            insertSorted(track, std::move(copy));
        } else {
            auto &tracks = ref->audio ? modified.audioTracks : modified.visualTracks;
            Track fresh = makeTrackLike(track);
            insertSorted(fresh, std::move(copy));
            tracks.insert(tracks.begin() + ref->trackIndex + 1, std::move(fresh));
        }
    }
    return finish(std::move(modified), clipIds.size() == 1 ? tr("Duplicate clip") : tr("Duplicate clips"), lastCopy);
}

EditResult TimelineEditor::moveClipToNewTrack(const ClipId &clipId, const RationalTime &requestedStart, int index)
{
    if (!m_sequence) {
        return fail(tr("The sequence does not exist."));
    }
    Sequence modified = *m_sequence;
    const auto source = findClip(modified, clipId);
    if (!source) {
        return fail(tr("The clip does not exist."));
    }
    if (source->track->locked) {
        return fail(tr("The track is locked."));
    }
    const bool audio = source->audio;
    auto &tracks = audio ? modified.audioTracks : modified.visualTracks;
    const int lowest = audio ? 0 : 1; // nothing goes below the main track
    index = std::clamp(index, lowest, static_cast<int>(tracks.size()));
    Track fresh = makeTrackLike(*source->track);
    const bool sourceMagnetic = isMagneticMain(modified, *source->track);
    Clip clip = takeClip(*source->track, source->index);
    if (sourceMagnetic) {
        pack(*source->track, m_rate);
    }
    clip.start = requestedStart.rescaled(m_rate, Rounding::NearestEven);
    if (clip.start.isNegative()) {
        clip.start = RationalTime(0, m_rate);
    }
    insertSorted(fresh, std::move(clip));
    tracks.insert(tracks.begin() + index, std::move(fresh));
    removeEmptyTracks(modified);
    cleanGroups(modified);
    return finish(std::move(modified), tr("Move clip"), clipId);
}

namespace {

struct TransitionRef
{
    Track *track = nullptr;
    size_t index = 0;
};

std::optional<TransitionRef> findTransition(Sequence &sequence, const TransitionId &transitionId)
{
    for (auto *tracks : {&sequence.visualTracks, &sequence.audioTracks}) {
        for (Track &track : *tracks) {
            for (size_t i = 0; i < track.transitions.size(); ++i) {
                if (track.transitions[i].id == transitionId) {
                    return TransitionRef{&track, i};
                }
            }
        }
    }
    return std::nullopt;
}

} // namespace

EditResult TimelineEditor::insertText(const RationalTime &position, TextClipData text, const RationalTime &duration)
{
    if (!m_sequence) {
        return fail(tr("The sequence does not exist."));
    }
    Clip clip;
    clip.id = ClipId::create();
    clip.start = position.rescaled(m_rate, Rounding::NearestEven);
    if (clip.start.isNegative()) {
        clip.start = RationalTime(0, m_rate);
    }
    clip.duration = duration.rescaled(m_rate, Rounding::NearestEven);
    if (clip.duration.value() <= 0) {
        clip.duration = RationalTime::fromSeconds(Rational(kDefaultTextSeconds), m_rate, Rounding::NearestEven);
    }
    clip.payload = std::move(text);
    const ClipId clipId = clip.id;
    Sequence modified = *m_sequence;
    Track &track = overlayTrackFor(modified, TrackKind::Text, clip.range());
    insertSorted(track, std::move(clip));
    return finish(std::move(modified), tr("Add text"), clipId);
}

EditResult TimelineEditor::insertSticker(const RationalTime &position, StickerClipData sticker, const RationalTime &duration)
{
    if (!m_sequence) {
        return fail(tr("The sequence does not exist."));
    }
    Clip clip;
    clip.id = ClipId::create();
    clip.start = position.rescaled(m_rate, Rounding::NearestEven);
    if (clip.start.isNegative()) {
        clip.start = RationalTime(0, m_rate);
    }
    clip.duration = duration.rescaled(m_rate, Rounding::NearestEven);
    if (clip.duration.value() <= 0) {
        clip.duration = RationalTime::fromSeconds(Rational(kDefaultStickerSeconds), m_rate, Rounding::NearestEven);
    }
    // A picture sticker starts at a third of the canvas, in the middle (visualizers and graphic elements span the canvas:
    // they are drawn at their size).
    if (!sticker.visualizer && !sticker.graphic) {
        clip.transform.scale = Param(Vec2{kDefaultStickerScale, kDefaultStickerScale});
    }
    clip.payload = std::move(sticker);
    const ClipId clipId = clip.id;
    Sequence modified = *m_sequence;
    Track &track = overlayTrackFor(modified, TrackKind::Sticker, clip.range());
    insertSorted(track, std::move(clip));
    return finish(std::move(modified), tr("Add sticker"), clipId);
}

EditResult TimelineEditor::updateClips(const std::vector<ClipId> &clipIds, const std::function<void(Clip &)> &change,
                                       const QString &text)
{
    if (!m_sequence) {
        return fail(tr("The sequence does not exist."));
    }
    if (clipIds.empty()) {
        return fail(tr("Select a clip first."));
    }
    Sequence modified = *m_sequence;
    for (const ClipId &clipId : clipIds) {
        const auto ref = findClip(modified, clipId);
        if (!ref) {
            return fail(tr("The clip does not exist."));
        }
        if (ref->track->locked) {
            return fail(tr("The track is locked."));
        }
        Clip &clip = ref->clip();
        const Clip before = clip;
        change(clip);
        // Timing and identity are not attributes: restored if a caller changed them by mistake.
        clip.id = before.id;
        clip.start = before.start;
        clip.duration = before.duration;
        if (clip.kind() != before.kind()) {
            clip.payload = before.payload;
        }
    }
    return finish(std::move(modified), text, clipIds.size() == 1 ? clipIds.front() : ClipId{});
}

EditResult TimelineEditor::setSpeed(const ClipId &clipId, double speed)
{
    if (!m_sequence) {
        return fail(tr("The sequence does not exist."));
    }
    Sequence modified = *m_sequence;
    const auto ref = findClip(modified, clipId);
    if (!ref) {
        return fail(tr("The clip does not exist."));
    }
    if (ref->track->locked) {
        return fail(tr("The track is locked."));
    }
    Clip &clip = ref->clip();
    MediaClipData *media = clip.media();
    const Media *item = media ? m_project.findMedia(media->mediaId) : nullptr;
    if (!media || !item || item->kind == MediaKind::Image) {
        return fail(tr("The speed can be changed only on video and audio clips."));
    }
    speed = std::clamp(speed, 0.1, 100.0);
    // The material used stays the same: duration × speed source frames.
    const double currentSpeed = media->curve ? SpeedCurveUtil::averageSpeed(*media->curve) : media->speed;
    const double sourceSpan = static_cast<double>(clip.duration.value()) * currentSpeed;
    const auto frames = std::max<std::int64_t>(1, std::llround(sourceSpan / speed));
    media->speed = speed;
    media->curve.reset();
    clip.duration = RationalTime(frames, m_rate);
    Track &track = *ref->track;
    clampTransitions(track);
    if (isMagneticMain(modified, track)) {
        pack(track, m_rate);
    } else if (!hasRoom(track, clip.range(), clip.id)) {
        // Never overwrite: the clip moves onto a new track right above (or below, for audio).
        Clip moved = takeClip(track, ref->index);
        auto &tracks = ref->audio ? modified.audioTracks : modified.visualTracks;
        Track fresh = makeTrack(track.kind);
        insertSorted(fresh, std::move(moved));
        tracks.insert(tracks.begin() + ref->trackIndex + 1, std::move(fresh));
        removeEmptyTracks(modified);
    }
    return finish(std::move(modified), tr("Change speed"), clipId);
}

EditResult TimelineEditor::setSpeedCurve(const ClipId &clipId, const SpeedCurve &curve)
{
    if (!m_sequence) {
        return fail(tr("The sequence does not exist."));
    }
    Sequence modified = *m_sequence;
    const auto ref = findClip(modified, clipId);
    if (!ref) {
        return fail(tr("The clip does not exist."));
    }
    if (ref->track->locked) {
        return fail(tr("The track is locked."));
    }
    Clip &clip = ref->clip();
    MediaClipData *media = clip.media();
    const Media *item = media ? m_project.findMedia(media->mediaId) : nullptr;
    if (!media || !item || item->kind == MediaKind::Image) {
        return fail(tr("The speed can be changed only on video and audio clips."));
    }

    const double currentAvgSpeed = media->curve ? SpeedCurveUtil::averageSpeed(*media->curve) : media->speed;
    const double sourceSpan = static_cast<double>(clip.duration.value()) * currentAvgSpeed;
    const double newAvgSpeed = std::clamp(SpeedCurveUtil::averageSpeed(curve), 0.05, 100.0);
    const auto frames = std::max<std::int64_t>(1, std::llround(sourceSpan / newAvgSpeed));

    media->curve = curve;
    media->speed = newAvgSpeed;
    clip.duration = RationalTime(frames, m_rate);

    Track &track = *ref->track;
    clampTransitions(track);
    if (isMagneticMain(modified, track)) {
        pack(track, m_rate);
    } else if (!hasRoom(track, clip.range(), clip.id)) {
        Clip moved = takeClip(track, ref->index);
        auto &tracks = ref->audio ? modified.audioTracks : modified.visualTracks;
        Track fresh = makeTrack(track.kind);
        insertSorted(fresh, std::move(moved));
        tracks.insert(tracks.begin() + ref->trackIndex + 1, std::move(fresh));
        removeEmptyTracks(modified);
    }
    return finish(std::move(modified), tr("Apply speed curve"), clipId);
}

EditResult TimelineEditor::removeSpeedCurve(const ClipId &clipId)
{
    return setSpeed(clipId, 1.0);
}

EditResult TimelineEditor::insertFreezeFrame(const ClipId &clipId, const RationalTime &requestedTime,
                                             const MediaId &imageMediaId, const RationalTime &duration)
{
    if (!m_sequence) {
        return fail(tr("The sequence does not exist."));
    }
    Sequence modified = *m_sequence;
    auto ref = findClip(modified, clipId);
    if (!ref) {
        return fail(tr("The clip does not exist."));
    }
    if (ref->track->locked) {
        return fail(tr("The track is locked."));
    }
    const RationalTime time = requestedTime.rescaled(m_rate, Rounding::NearestEven);
    Clip &original = ref->clip();
    if (!original.media() || time < original.start || time >= original.end()) {
        return fail(tr("Move the playhead over a video clip to freeze a frame."));
    }
    Track &track = *ref->track;
    size_t insertAt = ref->index;
    if (time > original.start) {
        // Split: the first part ends at `time`, the second part follows the still.
        const RationalTime offset = time - original.start;
        Clip second = original;
        second.id = ClipId::create();
        second.start = time;
        second.duration = original.duration - offset;
        for (Effect &effect : second.effects) {
            effect.id = EffectId::create();
        }
        second.markers.clear();
        MediaClipData *media = second.media();
        media->sourceIn += RationalTime(sourceFrames(offset.value(), media->speed), m_rate);
        media->audio.fadeIn.reset();
        original.media()->audio.fadeOut.reset();
        original.duration = offset;
        for (Transition &transition : track.transitions) {
            if (transition.from == original.id) {
                transition.from = second.id;
            }
        }
        insertAt = ref->index + 1;
        track.clips.insert(track.clips.begin() + static_cast<std::ptrdiff_t>(insertAt), std::move(second));
    }
    const Clip &source = track.clips[insertAt < track.clips.size() ? insertAt : insertAt - 1];
    Clip still;
    still.id = ClipId::create();
    still.start = time;
    still.duration = duration.rescaled(m_rate, Rounding::NearestEven);
    if (still.duration.value() <= 0) {
        still.duration = RationalTime::fromSeconds(Rational(kDefaultTextSeconds), m_rate, Rounding::NearestEven);
    }
    still.transform = source.transform;
    still.opacity = source.opacity;
    still.blendMode = source.blendMode;
    still.background = source.background;
    still.effects = source.effects;
    for (Effect &effect : still.effects) {
        effect.id = EffectId::create();
    }
    MediaClipData data;
    data.mediaId = imageMediaId;
    data.streams = Streams::VideoOnly;
    data.sourceIn = RationalTime(0, m_rate);
    still.payload = data;
    const ClipId stillId = still.id;
    // Everything after `time` on this track moves along by the still's duration.
    for (size_t i = insertAt; i < track.clips.size(); ++i) {
        track.clips[i].start += still.duration;
    }
    track.clips.insert(track.clips.begin() + static_cast<std::ptrdiff_t>(insertAt), std::move(still));
    pruneTransitions(track);
    clampTransitions(track);
    if (isMagneticMain(modified, track)) {
        pack(track, m_rate);
    }
    return finish(std::move(modified), tr("Freeze frame"), stillId);
}

EditResult TimelineEditor::addTransition(const ClipId &fromClip, const AssetRef &type, const RationalTime &duration,
                                         std::map<QString, Param> params)
{
    if (!m_sequence) {
        return fail(tr("The sequence does not exist."));
    }
    Sequence modified = *m_sequence;
    const auto ref = findClip(modified, fromClip);
    if (!ref) {
        return fail(tr("The clip does not exist."));
    }
    Track &track = *ref->track;
    if (track.locked) {
        return fail(tr("The track is locked."));
    }
    if (ref->index + 1 >= track.clips.size() || !(track.clips[ref->index + 1].start == ref->clip().end())) {
        return fail(tr("A transition goes between two clips that touch."));
    }
    const Clip &from = track.clips[ref->index];
    const Clip &to = track.clips[ref->index + 1];
    RationalTime length = duration.rescaled(m_rate, Rounding::NearestEven);
    length = std::clamp(length, RationalTime(1, m_rate), std::min(from.duration, to.duration));
    const auto existing = std::find_if(track.transitions.begin(), track.transitions.end(),
                                       [&](const Transition &t) { return t.from == from.id && t.to == to.id; });
    if (existing != track.transitions.end()) {
        existing->type = type;
        existing->duration = length;
        existing->params = std::move(params);
    } else {
        Transition transition;
        transition.id = TransitionId::create();
        transition.type = type;
        transition.from = from.id;
        transition.to = to.id;
        transition.duration = length;
        transition.params = std::move(params);
        track.transitions.push_back(std::move(transition));
    }
    return finish(std::move(modified), tr("Add transition"), {});
}

EditResult TimelineEditor::updateTransition(const TransitionId &transitionId, const std::function<void(Transition &)> &change)
{
    if (!m_sequence) {
        return fail(tr("The sequence does not exist."));
    }
    Sequence modified = *m_sequence;
    const auto ref = findTransition(modified, transitionId);
    if (!ref) {
        return fail(tr("The transition does not exist."));
    }
    Transition &transition = ref->track->transitions[ref->index];
    const Transition before = transition;
    change(transition);
    transition.id = before.id;
    transition.from = before.from;
    transition.to = before.to;
    const Clip *from = ref->track->findClip(transition.from);
    const Clip *to = ref->track->findClip(transition.to);
    if (from && to) {
        transition.duration = std::clamp(transition.duration.rescaled(m_rate, Rounding::NearestEven),
                                         RationalTime(1, m_rate), std::min(from->duration, to->duration));
    }
    return finish(std::move(modified), tr("Change transition"), {});
}

EditResult TimelineEditor::removeTransitions(const std::vector<TransitionId> &transitionIds)
{
    if (!m_sequence) {
        return fail(tr("The sequence does not exist."));
    }
    Sequence modified = *m_sequence;
    for (const TransitionId &transitionId : transitionIds) {
        const auto ref = findTransition(modified, transitionId);
        if (!ref) {
            return fail(tr("The transition does not exist."));
        }
        ref->track->transitions.erase(ref->track->transitions.begin() + static_cast<std::ptrdiff_t>(ref->index));
    }
    return finish(std::move(modified), transitionIds.size() == 1 ? tr("Remove transition") : tr("Remove transitions"), {});
}

EditResult TimelineEditor::applyTransitionToAll(const TrackId &trackId, const AssetRef &type, const RationalTime &duration,
                                                std::map<QString, Param> params)
{
    if (!m_sequence) {
        return fail(tr("The sequence does not exist."));
    }
    Sequence modified = *m_sequence;
    const auto ref = findTrack(modified, trackId);
    if (!ref) {
        return fail(tr("The track does not exist."));
    }
    Track &track = *ref->track;
    if (track.locked) {
        return fail(tr("The track is locked."));
    }
    const RationalTime length = std::max(RationalTime(1, m_rate), duration.rescaled(m_rate, Rounding::NearestEven));
    int count = 0;
    for (size_t i = 0; i + 1 < track.clips.size(); ++i) {
        const Clip &from = track.clips[i];
        const Clip &to = track.clips[i + 1];
        if (!(to.start == from.end())) {
            continue;
        }
        const RationalTime clamped = std::min(length, std::min(from.duration, to.duration));
        const auto existing = std::find_if(track.transitions.begin(), track.transitions.end(),
                                           [&](const Transition &t) { return t.from == from.id && t.to == to.id; });
        if (existing != track.transitions.end()) {
            existing->type = type;
            existing->duration = clamped;
            existing->params = params;
        } else {
            Transition transition;
            transition.id = TransitionId::create();
            transition.type = type;
            transition.from = from.id;
            transition.to = to.id;
            transition.duration = clamped;
            transition.params = params;
            track.transitions.push_back(std::move(transition));
        }
        ++count;
    }
    if (count == 0) {
        return fail(tr("There are no cuts between clips on this track."));
    }
    return finish(std::move(modified), tr("Transition on every cut"), {});
}

EditResult TimelineEditor::removeAllTransitions(const TrackId &trackId)
{
    if (!m_sequence) {
        return fail(tr("The sequence does not exist."));
    }
    Sequence modified = *m_sequence;
    const auto ref = findTrack(modified, trackId);
    if (!ref) {
        return fail(tr("The track does not exist."));
    }
    ref->track->transitions.clear();
    return finish(std::move(modified), tr("Remove transitions"), {});
}

EditResult TimelineEditor::updateTrack(const TrackId &trackId, const std::function<void(Track &)> &change, const QString &text)
{
    if (!m_sequence) {
        return fail(tr("The sequence does not exist."));
    }
    Sequence modified = *m_sequence;
    const auto ref = findTrack(modified, trackId);
    if (!ref) {
        return fail(tr("The track does not exist."));
    }
    Track &track = *ref->track;
    const std::vector<Clip> clips = track.clips;
    const std::vector<Transition> transitions = track.transitions;
    const TrackId id = track.id;
    change(track);
    track.id = id;
    track.clips = clips;
    track.transitions = transitions;
    return finish(std::move(modified), text, {});
}

EditResult TimelineEditor::setDefaultBackground(const std::optional<CanvasBackground> &background)
{
    if (!m_sequence) {
        return fail(tr("The sequence does not exist."));
    }
    Sequence modified = *m_sequence;
    modified.defaultBackground = background;
    return finish(std::move(modified), tr("Change background"), {});
}

EditResult TimelineEditor::setMagneticMain(bool enabled)
{
    if (!m_sequence) {
        return fail(tr("The sequence does not exist."));
    }
    Sequence modified = *m_sequence;
    modified.magneticMain = enabled;
    return finish(std::move(modified), enabled ? tr("Enable magnetic main track") : tr("Disable magnetic main track"), {});
}

EditResult TimelineEditor::createCompoundClip(const std::vector<ClipId> &clipIds, const QString &name)
{
    if (!m_sequence) {
        return fail(tr("The sequence does not exist."));
    }
    if (clipIds.empty()) {
        return fail(tr("Select at least one clip."));
    }
    Sequence modified = *m_sequence;

    std::vector<Clip> selected;
    RationalTime minStart;
    RationalTime maxEnd;
    bool first = true;
    TrackId primaryTrackId;

    for (const ClipId &id : clipIds) {
        const auto ref = findClip(modified, id);
        if (!ref) {
            return fail(tr("A clip does not exist."));
        }
        if (ref->track->locked) {
            return fail(tr("A track is locked."));
        }
        const Clip &c = ref->clip();
        if (first) {
            minStart = c.start;
            maxEnd = c.end();
            primaryTrackId = ref->track->id;
            first = false;
        } else {
            minStart = std::min(minStart, c.start);
            maxEnd = std::max(maxEnd, c.end());
        }
        selected.push_back(c);
    }

    const RationalTime duration = maxEnd - minStart;
    if (duration.value() <= 0) {
        return fail(tr("Invalid duration for compound clip."));
    }

    Sequence nestedSeq;
    nestedSeq.id = SequenceId::create();
    nestedSeq.name = name.isEmpty() ? tr("Compound Clip") : name;
    nestedSeq.canvas = m_sequence->canvas;
    nestedSeq.defaultBackground = m_sequence->defaultBackground;

    std::map<TrackId, Track> nestedVisualTracks;
    std::map<TrackId, Track> nestedAudioTracks;

    for (const Clip &c : selected) {
        const auto ref = findClip(modified, c.id);
        const Track &srcTrack = *ref->track;
        const bool isAudio = !isVisualTrackKind(srcTrack.kind);
        auto &targetMap = isAudio ? nestedAudioTracks : nestedVisualTracks;

        if (targetMap.find(srcTrack.id) == targetMap.end()) {
            Track t = makeTrack(srcTrack.kind);
            t.name = srcTrack.name;
            targetMap[srcTrack.id] = t;
        }
        Clip shifted = c;
        shifted.start = c.start - minStart;
        insertSorted(targetMap[srcTrack.id], std::move(shifted));
    }

    for (auto &[trackId, track] : nestedVisualTracks) {
        nestedSeq.visualTracks.push_back(std::move(track));
    }
    for (auto &[trackId, track] : nestedAudioTracks) {
        nestedSeq.audioTracks.push_back(std::move(track));
    }
    if (nestedSeq.visualTracks.empty()) {
        nestedSeq.visualTracks.push_back(makeTrack(TrackKind::Video));
    }

    for (const ClipId &id : clipIds) {
        const auto ref = findClip(modified, id);
        if (ref) {
            auto &clips = ref->track->clips;
            clips.erase(std::remove_if(clips.begin(), clips.end(), [&](const Clip &c) { return c.id == id; }), clips.end());
        }
    }

    Clip compoundClip;
    compoundClip.id = ClipId::create();
    compoundClip.name = nestedSeq.name;
    compoundClip.start = minStart;
    compoundClip.duration = duration;
    compoundClip.payload = CompoundClipData{nestedSeq.id, RationalTime(0, m_rate)};

    auto targetTrackRef = findTrack(modified, primaryTrackId);
    if (!targetTrackRef || !clipAllowedOnTrack(compoundClip, targetTrackRef->track->kind)) {
        targetTrackRef = findTrack(modified, modified.visualTracks.front().id);
    }
    insertSorted(*targetTrackRef->track, compoundClip);
    if (modified.magneticMain && !modified.visualTracks.empty()) {
        pack(modified.visualTracks.front(), m_rate);
    }

    EditResult result;
    result.script.push_back(edits::insertSequence(static_cast<int>(m_project.sequences.size()), std::move(nestedSeq)));
    EditScript seqEdits = diffSequence(*m_sequence, modified);
    for (auto &e : seqEdits) {
        result.script.push_back(std::move(e));
    }
    result.text = tr("Create compound clip");
    result.primaryClip = compoundClip.id;
    return result;
}

EditResult TimelineEditor::expandCompoundClip(const ClipId &clipId)
{
    if (!m_sequence) {
        return fail(tr("The sequence does not exist."));
    }
    Sequence modified = *m_sequence;
    const auto ref = findClip(modified, clipId);
    if (!ref) {
        return fail(tr("The clip does not exist."));
    }
    if (ref->track->locked) {
        return fail(tr("The track is locked."));
    }
    const Clip &c = ref->clip();
    const auto *compound = c.compound();
    if (!compound) {
        return fail(tr("The clip is not a compound clip."));
    }

    const Sequence *nested = m_project.findSequence(compound->sequenceId);
    if (!nested) {
        return fail(tr("The nested sequence was not found."));
    }

    const SequenceId nestedSeqId = compound->sequenceId;
    const Sequence nestedSeqCopy = *nested;
    const RationalTime compStart = c.start;
    const RationalTime sourceIn = compound->sourceIn.rescaled(m_rate, Rounding::NearestEven);
    const TrackId parentTrackId = ref->track->id;

    auto &clips = ref->track->clips;
    clips.erase(std::remove_if(clips.begin(), clips.end(), [&](const Clip &item) { return item.id == clipId; }), clips.end());

    ClipId primary;
    for (int pass = 0; pass < 2; ++pass) {
        const auto &tracks = (pass == 0) ? nestedSeqCopy.visualTracks : nestedSeqCopy.audioTracks;
        for (const Track &t : tracks) {
            for (const Clip &nestedClip : t.clips) {
                Clip expanded = nestedClip;
                expanded.id = ClipId::create();
                expanded.start = compStart + (nestedClip.start - sourceIn);
                if (expanded.start.isNegative()) {
                    continue;
                }
                if (primary.isNull()) {
                    primary = expanded.id;
                }
                if (pass == 0) {
                    const auto trk = findTrack(modified, parentTrackId);
                    if (trk && t.kind == trk->track->kind && hasRoom(*trk->track, expanded.range())) {
                        insertSorted(*trk->track, std::move(expanded));
                    } else {
                        Track &target = overlayTrackFor(modified, t.kind, expanded.range());
                        insertSorted(target, std::move(expanded));
                    }
                } else {
                    Track &target = audioTrackFor(modified, expanded.range());
                    insertSorted(target, std::move(expanded));
                }
            }
        }
    }

    if (modified.magneticMain && !modified.visualTracks.empty()) {
        pack(modified.visualTracks.front(), m_rate);
    }

    EditResult result;
    EditScript seqEdits = diffSequence(*m_sequence, modified);
    for (auto &e : seqEdits) {
        result.script.push_back(std::move(e));
    }
    const int seqIdx = m_project.sequenceIndex(nestedSeqId);
    if (seqIdx >= 0) {
        result.script.push_back(edits::removeSequence(seqIdx, nestedSeqCopy));
    }
    result.text = tr("Expand compound clip");
    result.primaryClip = primary;
    return result;
}

EditResult TimelineEditor::insertAdjustment(const RationalTime &position, const RationalTime &duration)
{
    if (!m_sequence) {
        return fail(tr("The sequence does not exist."));
    }
    Clip clip;
    clip.id = ClipId::create();
    clip.name = tr("Adjustment Layer");
    clip.start = position.rescaled(m_rate, Rounding::NearestEven);
    if (clip.start.isNegative()) {
        clip.start = RationalTime(0, m_rate);
    }
    clip.duration = duration.rescaled(m_rate, Rounding::NearestEven);
    if (clip.duration.value() <= 0) {
        clip.duration = RationalTime::fromSeconds(Rational(kDefaultTextSeconds), m_rate, Rounding::NearestEven);
    }
    clip.payload = AdjustmentClipData{};
    const ClipId clipId = clip.id;
    Sequence modified = *m_sequence;
    Track &track = overlayTrackFor(modified, TrackKind::Adjustment, clip.range());
    insertSorted(track, std::move(clip));
    return finish(std::move(modified), tr("Add adjustment layer"), clipId);
}

EditResult TimelineEditor::addSequenceMarker(const RationalTime &time, const QString &name,
                                             const QString &color, const QString &note, MarkerKind kind)
{
    if (!m_sequence) {
        return fail(tr("The sequence does not exist."));
    }
    Sequence modified = *m_sequence;
    Marker marker;
    marker.id = MarkerId::create();
    marker.time = time.rescaled(m_rate, Rounding::NearestEven);
    if (marker.time.isNegative()) {
        marker.time = RationalTime(0, m_rate);
    }
    marker.name = name;
    marker.color = color.isEmpty() ? QStringLiteral("primary") : color;
    marker.note = note;
    marker.kind = kind;
    modified.markers.push_back(std::move(marker));
    std::sort(modified.markers.begin(), modified.markers.end(),
              [](const Marker &a, const Marker &b) { return a.time < b.time; });
    return finish(std::move(modified), tr("Add marker"), ClipId{});
}

EditResult TimelineEditor::removeSequenceMarker(const MarkerId &markerId)
{
    if (!m_sequence) {
        return fail(tr("The sequence does not exist."));
    }
    Sequence modified = *m_sequence;
    const auto it = std::find_if(modified.markers.begin(), modified.markers.end(),
                                 [&](const Marker &m) { return m.id == markerId; });
    if (it == modified.markers.end()) {
        return fail(tr("The marker does not exist."));
    }
    modified.markers.erase(it);
    return finish(std::move(modified), tr("Remove marker"), ClipId{});
}

EditResult TimelineEditor::updateSequenceMarker(const Marker &marker)
{
    if (!m_sequence) {
        return fail(tr("The sequence does not exist."));
    }
    Sequence modified = *m_sequence;
    const auto it = std::find_if(modified.markers.begin(), modified.markers.end(),
                                 [&](const Marker &m) { return m.id == marker.id; });
    if (it == modified.markers.end()) {
        return fail(tr("The marker does not exist."));
    }
    *it = marker;
    it->time = it->time.rescaled(m_rate, Rounding::NearestEven);
    std::sort(modified.markers.begin(), modified.markers.end(),
              [](const Marker &a, const Marker &b) { return a.time < b.time; });
    return finish(std::move(modified), tr("Edit marker"), ClipId{});
}

EditResult TimelineEditor::addClipMarker(const ClipId &clipId, const RationalTime &time, const QString &name,
                                         const QString &color, const QString &note, MarkerKind kind)
{
    if (!m_sequence) {
        return fail(tr("The sequence does not exist."));
    }
    Sequence modified = *m_sequence;
    const auto ref = findClip(modified, clipId);
    if (!ref) {
        return fail(tr("The clip does not exist."));
    }
    if (ref->track->locked) {
        return fail(tr("The track is locked."));
    }
    Clip &clip = ref->clip();
    Marker marker;
    marker.id = MarkerId::create();
    marker.time = time.rescaled(m_rate, Rounding::NearestEven);
    if (marker.time.isNegative()) {
        marker.time = RationalTime(0, m_rate);
    }
    marker.name = name;
    marker.color = color.isEmpty() ? QStringLiteral("primary") : color;
    marker.note = note;
    marker.kind = kind;
    clip.markers.push_back(std::move(marker));
    std::sort(clip.markers.begin(), clip.markers.end(),
              [](const Marker &a, const Marker &b) { return a.time < b.time; });
    return finish(std::move(modified), tr("Add clip marker"), clipId);
}

EditResult TimelineEditor::removeClipMarker(const ClipId &clipId, const MarkerId &markerId)
{
    if (!m_sequence) {
        return fail(tr("The sequence does not exist."));
    }
    Sequence modified = *m_sequence;
    const auto ref = findClip(modified, clipId);
    if (!ref) {
        return fail(tr("The clip does not exist."));
    }
    if (ref->track->locked) {
        return fail(tr("The track is locked."));
    }
    Clip &clip = ref->clip();
    const auto it = std::find_if(clip.markers.begin(), clip.markers.end(),
                                 [&](const Marker &m) { return m.id == markerId; });
    if (it == clip.markers.end()) {
        return fail(tr("The marker does not exist."));
    }
    clip.markers.erase(it);
    return finish(std::move(modified), tr("Remove clip marker"), clipId);
}

EditResult TimelineEditor::updateClipMarker(const ClipId &clipId, const Marker &marker)
{
    if (!m_sequence) {
        return fail(tr("The sequence does not exist."));
    }
    Sequence modified = *m_sequence;
    const auto ref = findClip(modified, clipId);
    if (!ref) {
        return fail(tr("The clip does not exist."));
    }
    if (ref->track->locked) {
        return fail(tr("The track is locked."));
    }
    Clip &clip = ref->clip();
    const auto it = std::find_if(clip.markers.begin(), clip.markers.end(),
                                 [&](const Marker &m) { return m.id == marker.id; });
    if (it == clip.markers.end()) {
        return fail(tr("The marker does not exist."));
    }
    *it = marker;
    it->time = it->time.rescaled(m_rate, Rounding::NearestEven);
    std::sort(clip.markers.begin(), clip.markers.end(),
              [](const Marker &a, const Marker &b) { return a.time < b.time; });
    return finish(std::move(modified), tr("Edit clip marker"), clipId);
}

EditResult TimelineEditor::setBeatMarkers(const ClipId &clipId, const std::vector<double> &beatSeconds)
{
    if (!m_sequence) {
        return fail(tr("The sequence does not exist."));
    }
    Sequence modified = *m_sequence;
    const auto ref = findClip(modified, clipId);
    if (!ref) {
        return fail(tr("The clip does not exist."));
    }
    if (ref->track->locked) {
        return fail(tr("The track is locked."));
    }
    Clip &clip = ref->clip();
    // Beats are times of the source (keyframe time, D-44): only those of the part the clip plays are kept.
    const RationalTime a = keyframeTime(clip, RationalTime(0, m_rate));
    const RationalTime b = keyframeTime(clip, clip.duration);
    const RationalTime low = std::min(a, b);
    const RationalTime high = std::max(a, b);
    std::erase_if(clip.markers, [](const Marker &marker) { return marker.kind == MarkerKind::Beat; });
    for (const double seconds : beatSeconds) {
        // Analysis results are seconds: converted once, to the nearest frame.
        const RationalTime time(std::llround(seconds * m_rate.toDouble()), m_rate);
        if (time < low || time > high) {
            continue;
        }
        Marker marker;
        marker.id = MarkerId::create();
        marker.time = time;
        marker.kind = MarkerKind::Beat;
        clip.markers.push_back(std::move(marker));
    }
    std::sort(clip.markers.begin(), clip.markers.end(), [](const Marker &x, const Marker &y) { return x.time < y.time; });
    return finish(std::move(modified), tr("Beats"), clipId);
}

EditResult TimelineEditor::insertPlaceholder(const Placeholder &placeholder, const RationalTime &duration)
{
    if (!m_sequence) {
        return fail(tr("The sequence does not exist."));
    }
    Sequence modified = *m_sequence;
    Track &main = modified.visualTracks.front();
    if (main.locked) {
        return fail(tr("The main track is locked."));
    }
    Clip clip;
    clip.id = ClipId::create();
    clip.start = main.clips.empty() ? RationalTime(0, m_rate) : main.clips.back().end();
    clip.duration = duration.rescaled(m_rate, Rounding::NearestEven);
    if (clip.duration.value() <= 0) {
        return fail(tr("The duration must be positive."));
    }
    clip.payload = ColorClipData{Param(Color{0x5f, 0x63, 0x68, 255})};
    clip.placeholder = placeholder;
    const ClipId clipId = clip.id;
    main.clips.push_back(std::move(clip));
    return finish(std::move(modified), tr("Add a slot"), clipId);
}

EditResult TimelineEditor::replaceClipMedia(const ClipId &clipId, const MediaId &mediaId)
{
    if (!m_sequence) {
        return fail(tr("The sequence does not exist."));
    }
    const Media *media = m_project.findMedia(mediaId);
    if (!media || media->kind == MediaKind::Audio) {
        return fail(tr("Only a video or a photo can replace a clip."));
    }
    Sequence modified = *m_sequence;
    const auto ref = findClip(modified, clipId);
    if (!ref) {
        return fail(tr("The clip does not exist."));
    }
    if (ref->track->locked) {
        return fail(tr("The track is locked."));
    }
    Clip &clip = ref->clip();
    if (!clip.media() && !std::holds_alternative<ColorClipData>(clip.payload)) {
        return fail(tr("This clip cannot be replaced."));
    }
    MediaClipData data;
    data.mediaId = mediaId;
    data.sourceIn = RationalTime(0, m_rate);
    data.streams = media->info.audio ? Streams::AudioVideo : Streams::VideoOnly;
    if (const MediaClipData *old = clip.media()) {
        data.audio = old->audio; // volume and fades belong to the slot
    }
    if (media->kind != MediaKind::Image && media->info.duration) {
        const RationalTime length = media->info.duration->rescaled(m_rate, Rounding::Floor);
        if (length.value() <= 0) {
            return fail(tr("The media is too short to be used."));
        }
        if (length < clip.duration) {
            clip.duration = length;
        }
    }
    clip.payload = data;
    clip.placeholder.reset();
    if (clip.name.isEmpty() || clip.name == media->name) {
        clip.name.clear();
    }
    if (ref->track == &modified.visualTracks.front() && isMagneticMain(modified, modified.visualTracks.front())) {
        pack(modified.visualTracks.front(), m_rate);
    }
    return finish(std::move(modified), tr("Replace clip"), clipId);
}

EditResult TimelineEditor::setClipAnimations(const ClipId &clipId, const ClipAnimations &animations)
{
    return updateClips({clipId}, [&](Clip &c) { c.animations = animations; }, tr("Animation"));
}

EditResult TimelineEditor::alignClipsByAudio(const ClipId &refClipId, const std::vector<ClipId> &targetClipIds,
                                             const AudioOffsetFn &calcOffset)
{
    if (!m_sequence) {
        return fail(tr("The sequence does not exist."));
    }
    if (!calcOffset) {
        return fail(tr("No alignment algorithm provided."));
    }
    Sequence modified = *m_sequence;
    const auto ref = findClip(modified, refClipId);
    if (!ref) {
        return fail(tr("Reference clip not found."));
    }
    const Clip refClip = ref->clip();

    struct TargetMove {
        ClipId id;
        RationalTime newStart;
    };
    std::vector<TargetMove> moves;
    RationalTime minStart = refClip.start;

    for (const ClipId &tgtId : targetClipIds) {
        if (tgtId == refClipId) {
            continue;
        }
        const auto tgtRef = findClip(modified, tgtId);
        if (!tgtRef) {
            continue;
        }
        const Clip &tgtClip = tgtRef->clip();
        const auto offsetSecOpt = calcOffset(refClip, tgtClip);
        if (!offsetSecOpt) {
            continue;
        }
        const double offsetSec = *offsetSecOpt;
        const RationalTime offsetTime(static_cast<std::int64_t>(std::round(offsetSec * m_rate.toDouble())), m_rate);

        const RationalTime refSourceIn = refClip.media() ? refClip.media()->sourceIn : RationalTime(0, m_rate);
        const RationalTime tgtSourceIn = tgtClip.media() ? tgtClip.media()->sourceIn : RationalTime(0, m_rate);
        const RationalTime alignedStart = refClip.start + (tgtSourceIn - refSourceIn) - offsetTime;
        if (alignedStart < minStart) {
            minStart = alignedStart;
        }
        moves.push_back({tgtClip.id, alignedStart});
    }

    if (moves.empty()) {
        return fail(tr("No clips could be aligned."));
    }

    RationalTime globalShift(0, m_rate);
    if (minStart.value() < 0) {
        globalShift = -minStart;
        auto curRef = findClip(modified, refClipId);
        if (curRef) {
            curRef->clip().start += globalShift;
        }
    }

    const TrackId refTrackId = ref->track->id;

    for (const auto &m : moves) {
        auto curTgt = findClip(modified, m.id);
        if (curTgt) {
            curTgt->clip().start = m.newStart + globalShift;
            if (curTgt->track->id == refTrackId || !hasRoom(*curTgt->track, curTgt->clip().range(), m.id)) {
                Clip moved = curTgt->clip();
                auto &trackClips = curTgt->track->clips;
                trackClips.erase(trackClips.begin() + static_cast<std::ptrdiff_t>(curTgt->index));
                if (curTgt->track->kind == TrackKind::Audio) {
                    Track &dest = audioTrackFor(modified, moved.range());
                    insertSorted(dest, std::move(moved));
                } else {
                    Track &dest = overlayTrackFor(modified, curTgt->track->kind, moved.range());
                    insertSorted(dest, std::move(moved));
                }
            }
        }
    }

    auto mainTrackRef = findTrack(modified, refTrackId);
    if (mainTrackRef && isMagneticMain(modified, *mainTrackRef->track)) {
        pack(*mainTrackRef->track, m_rate);
    }

    return finish(std::move(modified), tr("Align clips by audio"), refClipId);
}

EditResult TimelineEditor::createMulticamClip(const std::vector<ClipId> &clipIds, const QString &name,
                                              const AudioOffsetFn &calcOffset)
{
    if (!m_sequence) {
        return fail(tr("The sequence does not exist."));
    }
    if (clipIds.size() < 2) {
        return fail(tr("Select at least 2 clips for multicam."));
    }

    Sequence tempSeq = *m_sequence;
    std::vector<Clip> clips;
    TrackId primaryTrackId;
    for (const ClipId &id : clipIds) {
        const auto ref = findClip(tempSeq, id);
        if (ref) {
            clips.push_back(ref->clip());
            if (primaryTrackId.isNull()) {
                primaryTrackId = ref->track->id;
            }
        }
    }
    if (clips.size() < 2) {
        return fail(tr("Clips not found for multicam."));
    }

    const Clip &refClip = clips.front();
    std::vector<RationalTime> relStarts(clips.size(), RationalTime(0, m_rate));
    RationalTime minRel(0, m_rate);

    for (size_t i = 1; i < clips.size(); ++i) {
        RationalTime rel(0, m_rate);
        bool aligned = false;
        if (calcOffset) {
            const auto optSec = calcOffset(refClip, clips[i]);
            if (optSec) {
                const RationalTime offsetTime(static_cast<std::int64_t>(std::round(*optSec * m_rate.toDouble())), m_rate);
                const RationalTime refSourceIn = refClip.media() ? refClip.media()->sourceIn : RationalTime(0, m_rate);
                const RationalTime tgtSourceIn = clips[i].media() ? clips[i].media()->sourceIn : RationalTime(0, m_rate);
                rel = (tgtSourceIn - refSourceIn) - offsetTime;
                aligned = true;
            }
        }
        if (!aligned) {
            rel = clips[i].start - refClip.start;
        }
        relStarts[i] = rel;
        if (rel < minRel) {
            minRel = rel;
        }
    }

    Sequence nestedSeq;
    nestedSeq.id = SequenceId::create();
    nestedSeq.name = name.isEmpty() ? tr("Multicam Clip") : name;
    nestedSeq.canvas = m_sequence->canvas;
    nestedSeq.defaultBackground = m_sequence->defaultBackground;

    RationalTime maxNestedEnd(0, m_rate);
    for (size_t i = 0; i < clips.size(); ++i) {
        Track t = makeTrack(TrackKind::Video);
        t.name = tr("Angle %1").arg(i + 1);
        Clip c = clips[i];
        c.start = relStarts[i] - minRel;
        const RationalTime clipEnd = c.start + c.duration;
        if (clipEnd > maxNestedEnd) {
            maxNestedEnd = clipEnd;
        }
        insertSorted(t, std::move(c));
        nestedSeq.visualTracks.push_back(std::move(t));
    }

    // Master audio track from Angle 1
    if (refClip.media()) {
        Track audioTrack = makeTrack(TrackKind::Audio);
        audioTrack.name = tr("Audio (Angle 1)");
        Clip audioClip = refClip;
        audioClip.id = ClipId::create();
        audioClip.media()->streams = Streams::AudioOnly;
        audioClip.start = relStarts[0] - minRel;
        insertSorted(audioTrack, std::move(audioClip));
        nestedSeq.audioTracks.push_back(std::move(audioTrack));
    }

    Sequence modified = *m_sequence;
    for (const ClipId &id : clipIds) {
        const auto ref = findClip(modified, id);
        if (ref) {
            auto &trackClips = ref->track->clips;
            trackClips.erase(std::remove_if(trackClips.begin(), trackClips.end(),
                                            [&](const Clip &c) { return c.id == id; }),
                             trackClips.end());
        }
    }

    Clip compoundClip;
    compoundClip.id = ClipId::create();
    compoundClip.name = nestedSeq.name;
    compoundClip.start = refClip.start;
    compoundClip.duration = maxNestedEnd;
    compoundClip.payload = CompoundClipData{nestedSeq.id, RationalTime(0, m_rate), 0};

    auto targetTrackRef = findTrack(modified, primaryTrackId);
    if (!targetTrackRef || !clipAllowedOnTrack(compoundClip, targetTrackRef->track->kind)) {
        targetTrackRef = findTrack(modified, modified.visualTracks.front().id);
    }
    insertSorted(*targetTrackRef->track, compoundClip);
    if (modified.magneticMain && !modified.visualTracks.empty()) {
        pack(modified.visualTracks.front(), m_rate);
    }

    EditResult result;
    result.script.push_back(edits::insertSequence(static_cast<int>(m_project.sequences.size()), std::move(nestedSeq)));
    EditScript seqEdits = diffSequence(*m_sequence, modified);
    for (auto &e : seqEdits) {
        result.script.push_back(std::move(e));
    }
    result.text = tr("Create multicam clip");
    result.primaryClip = compoundClip.id;
    return result;
}

EditResult TimelineEditor::setMulticamAngle(const ClipId &clipId, int angle)
{
    if (!m_sequence) {
        return fail(tr("The sequence does not exist."));
    }
    if (angle < 0) {
        return fail(tr("Invalid angle."));
    }
    Sequence modified = *m_sequence;
    const auto ref = findClip(modified, clipId);
    if (!ref) {
        return fail(tr("Clip not found."));
    }
    CompoundClipData *compound = ref->clip().compound();
    if (!compound) {
        return fail(tr("Clip is not a multicam or compound clip."));
    }
    compound->activeAngle = angle;
    return finish(std::move(modified), tr("Set camera angle"), clipId);
}

EditResult TimelineEditor::cutAndSwitchAngle(const ClipId &clipId, int angle, const RationalTime &requestedTime)
{
    if (!m_sequence) {
        return fail(tr("The sequence does not exist."));
    }
    if (angle < 0) {
        return fail(tr("Invalid angle."));
    }
    Sequence modified = *m_sequence;
    const auto ref = findClip(modified, clipId);
    if (!ref) {
        return fail(tr("Clip not found."));
    }
    Track &track = *ref->track;
    if (track.locked) {
        return fail(tr("The track is locked."));
    }
    const RationalTime time = requestedTime.rescaled(m_rate, Rounding::NearestEven);
    Clip &first = ref->clip();
    if (time > first.start && time < first.end()) {
        const RationalTime offset = time - first.start;
        Clip second = first;
        second.id = ClipId::create();
        second.start = time;
        second.duration = first.duration - offset;
        for (Effect &effect : second.effects) {
            effect.id = EffectId::create();
        }
        for (Marker &marker : second.markers) {
            marker.id = MarkerId::create();
        }
        if (MediaClipData *media = second.media()) {
            media->sourceIn += RationalTime(sourceFrames(offset.value(), media->speed), m_rate);
            media->audio.fadeIn.reset();
            if (MediaClipData *firstMedia = first.media()) {
                firstMedia->audio.fadeOut.reset();
            }
        } else if (CompoundClipData *compound = second.compound()) {
            compound->sourceIn += offset;
            compound->activeAngle = angle;
        } else {
            shiftClipLocalTime(second, offset);
        }
        first.duration = offset;
        for (Transition &transition : track.transitions) {
            if (transition.from == first.id) {
                transition.from = second.id;
            }
        }
        const ClipId secondId = second.id;
        track.clips.insert(track.clips.begin() + static_cast<std::ptrdiff_t>(ref->index) + 1, std::move(second));
        clampTransitions(track);
        if (isMagneticMain(modified, track)) {
            pack(track, m_rate);
        }
        return finish(std::move(modified), tr("Cut and switch camera angle"), secondId);
    }

    CompoundClipData *compound = first.compound();
    if (!compound) {
        return fail(tr("Clip is not a multicam or compound clip."));
    }
    compound->activeAngle = angle;
    return finish(std::move(modified), tr("Set camera angle"), clipId);
}

EditResult TimelineEditor::insertCaptions(const std::vector<captions::CaptionLine> &lines,
                                          const std::optional<CaptionStyle> &style, bool replace)
{
    if (!m_sequence) {
        return fail(tr("The sequence does not exist."));
    }
    std::vector<captions::CaptionLine> sorted = lines;
    std::stable_sort(sorted.begin(), sorted.end(),
                     [](const captions::CaptionLine &a, const captions::CaptionLine &b) { return a.start < b.start; });
    std::vector<Clip> clips;
    RationalTime previousEnd(0, m_rate);
    for (size_t i = 0; i < sorted.size(); ++i) {
        const captions::CaptionLine &line = sorted[i];
        RationalTime start = std::max(line.start.rescaled(m_rate, Rounding::NearestEven), previousEnd);
        RationalTime end = line.end.rescaled(m_rate, Rounding::NearestEven);
        if (i + 1 < sorted.size()) {
            end = std::min(end, std::max(start, sorted[i + 1].start.rescaled(m_rate, Rounding::NearestEven)));
        }
        const QString text = captions::splitWords(line.text).join(u' ');
        if (end <= start || text.isEmpty()) {
            continue;
        }
        Clip clip;
        clip.id = ClipId::create();
        clip.start = start;
        clip.duration = end - start;
        SubtitleClipData data;
        data.text = text;
        for (const TimedWord &word : line.words) {
            const RationalTime from = std::clamp(word.start.rescaled(m_rate, Rounding::NearestEven), start, end) - start;
            const RationalTime to = std::clamp(word.end.rescaled(m_rate, Rounding::NearestEven), start, end) - start;
            data.words.push_back(TimedWord{word.text, from, std::max(from, to)});
        }
        clip.payload = std::move(data);
        clips.push_back(std::move(clip));
        previousEnd = end;
    }
    if (clips.empty()) {
        return fail(tr("There are no captions to add."));
    }

    Sequence modified = *m_sequence;
    Track *target = nullptr;
    for (size_t i = 1; i < modified.visualTracks.size() && !target; ++i) {
        Track &track = modified.visualTracks[i];
        if (!track.captions) {
            continue;
        }
        if (replace) {
            if (track.locked) {
                return fail(tr("The track is locked."));
            }
            track.clips.clear();
            track.transitions.clear();
            if (style) {
                projectjson::setCaptionStyle(track, *style);
            }
            target = &track;
        } else if (!track.locked && std::all_of(clips.begin(), clips.end(),
                                                [&track](const Clip &clip) { return hasRoom(track, clip.range()); })) {
            target = &track;
        }
    }
    if (!target) {
        Track track = makeTrack(TrackKind::Text);
        track.captions = true;
        track.name = tr("Captions");
        if (style) {
            projectjson::setCaptionStyle(track, *style); // otherwise the default style (projectjson::captionStyleOf)
        }
        modified.visualTracks.push_back(std::move(track));
        target = &modified.visualTracks.back();
    }
    const ClipId first = clips.front().id;
    for (Clip &clip : clips) {
        insertSorted(*target, std::move(clip));
    }
    return finish(std::move(modified), tr("Add captions"), first);
}

} // namespace vedit
