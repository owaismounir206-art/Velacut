// SPDX-License-Identifier: GPL-3.0-or-later
#include "TimelineEditor.h"

#include "core/edit/SequenceDiff.h"

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
        if (track.kind == kind && !track.locked && hasRoom(track, range)) {
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
        if (!clipAllowedOnTrack(source->clip(), target->track->kind)) {
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
        Track fresh = makeTrack(target->track->kind);
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
    } else {
        shiftClipLocalTime(second, offset);
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

} // namespace vedit
