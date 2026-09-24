// SPDX-License-Identifier: GPL-3.0-or-later
#include "ProjectData.h"

#include <QSet>
#include <QTimeZone>

#include <functional>

using namespace Qt::StringLiterals;

namespace vedit {

ClipKind Clip::kind() const
{
    return std::visit(
        [](const auto &data) {
            using T = std::decay_t<decltype(data)>;
            if constexpr (std::is_same_v<T, MediaClipData>) {
                return ClipKind::Media;
            } else if constexpr (std::is_same_v<T, ColorClipData>) {
                return ClipKind::Color;
            } else if constexpr (std::is_same_v<T, CompoundClipData>) {
                return ClipKind::Compound;
            } else {
                return data.kind;
            }
        },
        payload);
}

bool isVisualTrackKind(TrackKind kind)
{
    return kind != TrackKind::Audio;
}

const Clip *Track::findClip(const ClipId &clipId) const
{
    const int index = clipIndex(clipId);
    return index < 0 ? nullptr : &clips[static_cast<size_t>(index)];
}

int Track::clipIndex(const ClipId &clipId) const
{
    for (size_t i = 0; i < clips.size(); ++i) {
        if (clips[i].id == clipId) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

RationalTime Sequence::duration(const Rational &rate) const
{
    RationalTime end(0, rate);
    const auto scan = [&end](const std::vector<Track> &tracks) {
        for (const Track &track : tracks) {
            for (const Clip &clip : track.clips) {
                if (clip.end() > end) {
                    end = clip.end();
                }
            }
        }
    };
    scan(visualTracks);
    scan(audioTracks);
    return end;
}

ProjectData ProjectData::createEmpty(const QString &name)
{
    ProjectData project;
    project.id = ProjectId::create();
    project.name = name;
    // Whole seconds: the file stores ISO 8601 dates without milliseconds.
    project.createdAt = QDateTime::fromSecsSinceEpoch(QDateTime::currentSecsSinceEpoch(), QTimeZone::UTC);
    project.modifiedAt = project.createdAt;

    Sequence sequence;
    sequence.id = SequenceId::create();
    sequence.canvas = project.settings.defaultCanvas;
    Track mainTrack;
    mainTrack.id = TrackId::create();
    mainTrack.kind = TrackKind::Video;
    sequence.visualTracks.push_back(std::move(mainTrack));
    project.mainSequenceId = sequence.id;
    project.sequences.push_back(std::move(sequence));
    return project;
}

const Media *ProjectData::findMedia(const MediaId &mediaId) const
{
    const int index = mediaIndex(mediaId);
    return index < 0 ? nullptr : &media[static_cast<size_t>(index)];
}

int ProjectData::mediaIndex(const MediaId &mediaId) const
{
    for (size_t i = 0; i < media.size(); ++i) {
        if (media[i].id == mediaId) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int ProjectData::sequenceIndex(const SequenceId &sequenceId) const
{
    for (size_t i = 0; i < sequences.size(); ++i) {
        if (sequences[i].id == sequenceId) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

const Sequence *ProjectData::findSequence(const SequenceId &sequenceId) const
{
    const int index = sequenceIndex(sequenceId);
    return index < 0 ? nullptr : &sequences[static_cast<size_t>(index)];
}

const Sequence *ProjectData::mainSequence() const
{
    return findSequence(mainSequenceId);
}

TrackLocation ProjectData::locateTrack(const TrackId &trackId) const
{
    for (size_t s = 0; s < sequences.size(); ++s) {
        const Sequence &sequence = sequences[s];
        for (size_t t = 0; t < sequence.visualTracks.size(); ++t) {
            if (sequence.visualTracks[t].id == trackId) {
                return {static_cast<int>(s), false, static_cast<int>(t)};
            }
        }
        for (size_t t = 0; t < sequence.audioTracks.size(); ++t) {
            if (sequence.audioTracks[t].id == trackId) {
                return {static_cast<int>(s), true, static_cast<int>(t)};
            }
        }
    }
    return {};
}

const Track *ProjectData::findTrack(const TrackId &trackId) const
{
    const TrackLocation location = locateTrack(trackId);
    return location.isValid() ? &track(location) : nullptr;
}

ClipLocation ProjectData::locateClip(const ClipId &clipId) const
{
    for (size_t s = 0; s < sequences.size(); ++s) {
        const Sequence &sequence = sequences[s];
        for (int pass = 0; pass < 2; ++pass) {
            const std::vector<Track> &tracks = pass == 0 ? sequence.visualTracks : sequence.audioTracks;
            for (size_t t = 0; t < tracks.size(); ++t) {
                const int index = tracks[t].clipIndex(clipId);
                if (index >= 0) {
                    return {static_cast<int>(s), pass == 1, static_cast<int>(t), index};
                }
            }
        }
    }
    return {};
}

const Clip *ProjectData::findClip(const ClipId &clipId) const
{
    const ClipLocation location = locateClip(clipId);
    return location.isValid() ? &clip(location) : nullptr;
}

const Track &ProjectData::track(const TrackLocation &location) const
{
    const Sequence &sequence = sequences.at(static_cast<size_t>(location.sequenceIndex));
    const std::vector<Track> &tracks = location.audioTrack ? sequence.audioTracks : sequence.visualTracks;
    return tracks.at(static_cast<size_t>(location.trackIndex));
}

const Clip &ProjectData::clip(const ClipLocation &location) const
{
    const Track &owner = track({location.sequenceIndex, location.audioTrack, location.trackIndex});
    return owner.clips.at(static_cast<size_t>(location.clipIndex));
}

bool clipAllowedOnTrack(const Clip &clip, TrackKind trackKind)
{
    const ClipKind clipKind = clip.kind();
    switch (trackKind) {
    case TrackKind::Video:
        if (clipKind == ClipKind::Media) {
            return clip.media()->streams != Streams::AudioOnly;
        }
        return clipKind == ClipKind::Color || clipKind == ClipKind::Compound;
    case TrackKind::Audio:
        return clipKind == ClipKind::Media && clip.media()->streams == Streams::AudioOnly;
    case TrackKind::Text:
        return clipKind == ClipKind::Text || clipKind == ClipKind::Subtitle;
    case TrackKind::Sticker:
        return clipKind == ClipKind::Sticker;
    case TrackKind::Effect:
        return clipKind == ClipKind::Effect;
    case TrackKind::Adjustment:
        return clipKind == ClipKind::Adjustment;
    }
    return false;
}

namespace {

class InvariantChecker
{
public:
    explicit InvariantChecker(const ProjectData &project)
        : m_project(project)
        , m_rate(project.settings.frameRate)
    {
    }

    QStringList run()
    {
        if (!m_project.settings.frameRate.isPositive()) {
            fail(u"project frame rate must be positive"_s);
            return m_errors;
        }
        if (m_project.findSequence(m_project.mainSequenceId) == nullptr) {
            fail(u"mainSequenceId does not reference a sequence"_s);
        }
        for (const Media &media : m_project.media) {
            unique(m_mediaIds, media.id, u"media"_s);
        }
        for (const Sequence &sequence : m_project.sequences) {
            unique(m_sequenceIds, sequence.id, u"sequence"_s);
        }
        for (const Sequence &sequence : m_project.sequences) {
            checkSequence(sequence);
        }
        checkCompoundCycles();
        return m_errors;
    }

private:
    template<typename T>
    void unique(QSet<QString> &seen, const T &id, const QString &what)
    {
        if (id.isNull()) {
            fail(what + u" with null id"_s);
            return;
        }
        const QString key = id.toString();
        if (seen.contains(key)) {
            fail(what + u" id is not unique: "_s + key);
        }
        seen.insert(key);
    }

    void onGrid(const RationalTime &time, const QString &what)
    {
        if (time.rate() != m_rate) {
            fail(what + u" is not on the project frame grid: "_s + time.toString());
        }
    }

    void checkSequence(const Sequence &sequence)
    {
        const QString where = u"sequence "_s + sequence.id.toString();
        if (sequence.canvas.width <= 0 || sequence.canvas.height <= 0) {
            fail(where + u": invalid canvas size"_s);
        }
        if (sequence.visualTracks.empty() || sequence.visualTracks.front().kind != TrackKind::Video) {
            fail(where + u": the main track (visualTracks[0]) must exist and be a video track"_s);
        }
        for (size_t i = 0; i < sequence.visualTracks.size(); ++i) {
            const Track &track = sequence.visualTracks[i];
            if (!isVisualTrackKind(track.kind)) {
                fail(where + u": audio track in visualTracks"_s);
            }
            checkTrack(track, i == 0 && sequence.magneticMain);
        }
        for (const Track &track : sequence.audioTracks) {
            if (track.kind != TrackKind::Audio) {
                fail(where + u": non-audio track in audioTracks"_s);
            }
            checkTrack(track, false);
        }
        for (const Marker &marker : sequence.markers) {
            onGrid(marker.time, where + u" marker"_s);
        }
        for (const Group &group : sequence.groups) {
            for (const ClipId &clipId : group.clipIds) {
                if (!m_clipIds.contains(clipId.toString())) {
                    fail(where + u": group references a missing clip "_s + clipId.toString());
                }
            }
        }
    }

    void checkTrack(const Track &track, bool magnetic)
    {
        unique(m_trackIds, track.id, u"track"_s);
        const QString where = u"track "_s + track.id.toString();
        if (!(track.height > 0.0)) {
            fail(where + u": height must be positive"_s);
        }
        for (size_t i = 0; i < track.clips.size(); ++i) {
            const Clip &clip = track.clips[i];
            checkClip(clip, track);
            if (i == 0 && magnetic && !clip.start.isZero()) {
                fail(where + u": magnetic main track must start at 0"_s);
            }
            if (i == 0) {
                continue;
            }
            const Clip &previous = track.clips[i - 1];
            if (!(previous.start < clip.start)) {
                fail(where + u": clips are not sorted by start"_s);
                continue;
            }
            const Transition *transition = transitionBetween(track, previous.id, clip.id);
            const bool overlapTransition = transition && transition->alignment == TransitionAlignment::Overlap;
            if (overlapTransition) {
                if (clip.start != previous.end() - transition->duration) {
                    fail(where + u": overlap transition does not match the clip overlap"_s);
                }
            } else if (clip.start < previous.end()) {
                fail(where + u": overlapping clips "_s + previous.id.toString() + u" and "_s + clip.id.toString());
            } else if (magnetic && clip.start != previous.end()) {
                fail(where + u": gap on the magnetic main track"_s);
            }
        }
        for (const Transition &transition : track.transitions) {
            unique(m_transitionIds, transition.id, u"transition"_s);
            onGrid(transition.duration, where + u" transition duration"_s);
            if (transition.duration.value() <= 0) {
                fail(where + u": transition duration must be positive"_s);
            }
            const int from = track.clipIndex(transition.from);
            const int to = track.clipIndex(transition.to);
            if (from < 0 || to < 0 || to != from + 1) {
                fail(where + u": transition must connect two adjacent clips of the track"_s);
            }
        }
    }

    static const Transition *transitionBetween(const Track &track, const ClipId &from, const ClipId &to)
    {
        for (const Transition &transition : track.transitions) {
            if (transition.from == from && transition.to == to) {
                return &transition;
            }
        }
        return nullptr;
    }

    void checkClip(const Clip &clip, const Track &track)
    {
        unique(m_clipIds, clip.id, u"clip"_s);
        const QString where = u"clip "_s + clip.id.toString();
        onGrid(clip.start, where + u" start"_s);
        onGrid(clip.duration, where + u" duration"_s);
        if (clip.start.isNegative()) {
            fail(where + u": negative start"_s);
        }
        if (clip.duration.isNegative() || clip.duration.isZero()) {
            fail(where + u": duration must be positive"_s);
        }
        if (!clipAllowedOnTrack(clip, track.kind)) {
            fail(where + u": clip kind not allowed on this track kind"_s);
        }
        for (const Marker &marker : clip.markers) {
            onGrid(marker.time, where + u" marker"_s);
        }
        if (const MediaClipData *media = clip.media()) {
            onGrid(media->sourceIn, where + u" sourceIn"_s);
            if (media->sourceIn.isNegative()) {
                fail(where + u": negative sourceIn"_s);
            }
            if (!(media->speed >= 0.1 && media->speed <= 100.0)) {
                fail(where + u": speed out of range"_s);
            }
            if (m_project.findMedia(media->mediaId) == nullptr) {
                fail(where + u": references missing media "_s + media->mediaId.toString());
            }
        }
        if (const auto *compound = std::get_if<CompoundClipData>(&clip.payload)) {
            if (m_project.findSequence(compound->sequenceId) == nullptr) {
                fail(where + u": references missing sequence"_s);
            }
            onGrid(compound->sourceIn, where + u" sourceIn"_s);
        }
    }

    void checkCompoundCycles()
    {
        // Depth-first search over "sequence contains compound clip of sequence" edges.
        std::vector<int> state(m_project.sequences.size(), 0); // 0 new, 1 visiting, 2 done
        std::function<bool(size_t)> visit = [&](size_t index) {
            state[index] = 1;
            const Sequence &sequence = m_project.sequences[index];
            for (int pass = 0; pass < 2; ++pass) {
                for (const Track &track : pass == 0 ? sequence.visualTracks : sequence.audioTracks) {
                    for (const Clip &clip : track.clips) {
                        const auto *compound = std::get_if<CompoundClipData>(&clip.payload);
                        if (!compound) {
                            continue;
                        }
                        const int child = m_project.sequenceIndex(compound->sequenceId);
                        if (child < 0) {
                            continue;
                        }
                        if (state[static_cast<size_t>(child)] == 1) {
                            return false;
                        }
                        if (state[static_cast<size_t>(child)] == 0 && !visit(static_cast<size_t>(child))) {
                            return false;
                        }
                    }
                }
            }
            state[index] = 2;
            return true;
        };
        for (size_t i = 0; i < m_project.sequences.size(); ++i) {
            if (state[i] == 0 && !visit(i)) {
                fail(u"compound clips form a cycle"_s);
                return;
            }
        }
    }

    void fail(const QString &message) { m_errors.append(message); }

    const ProjectData &m_project;
    Rational m_rate;
    QStringList m_errors;
    QSet<QString> m_mediaIds;
    QSet<QString> m_sequenceIds;
    QSet<QString> m_trackIds;
    QSet<QString> m_clipIds;
    QSet<QString> m_transitionIds;
};

} // namespace

QStringList ProjectData::checkInvariants() const
{
    return InvariantChecker(*this).run();
}

} // namespace vedit
