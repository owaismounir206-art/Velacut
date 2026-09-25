// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/project/ChangeSet.h"
#include "core/project/ProjectData.h"

#include <QImage>
#include <QSize>
#include <QStringList>

#include <chrono>
#include <memory>
#include <vector>

namespace Mlt {
class Playlist;
class Producer;
class Profile;
class Tractor;
class Transition;
} // namespace Mlt

namespace vedit::engine {

class MediaProducerCache;

// Frame size and rate of a rendering: the sequence's canvas and the project frame rate for the preview, any
// size and rate for an export (the projection converts the model's times to the profile's frame rate).
struct VideoFormat
{
    QSize size;
    Rational frameRate;
};
VideoFormat sequenceFormat(const ProjectData &project, const SequenceId &sequenceId);

// Square pixels, progressive, Rec.709.
std::unique_ptr<Mlt::Profile> makeProfile(const VideoFormat &format);
std::unique_ptr<Mlt::Profile> makeProfile(const ProjectData &project, const SequenceId &sequenceId);
bool profileMatches(const Mlt::Profile &profile, const ProjectData &project, const SequenceId &sequenceId);

// The MLT graph of a sequence: a projection of the core model, rebuilt from it at any time (docs/ARCHITECTURE.md
// §5.1–5.2). Tractor layout: track 0 = background (black), then the visual tracks bottom-up (main track first),
// then the audio tracks. Every visual track is composited onto track 0 with "vedit.composite"; every track's
// audio is summed into track 0 with "mix".
class TimelineProjection
{
public:
    enum class MediaLoading
    {
        Wait,       // open media synchronously (export, tests)
        Background, // missing producers are requested and shown as gaps until ready (UI)
    };

    TimelineProjection(Mlt::Profile &profile, MediaProducerCache &cache, MediaLoading loading);
    ~TimelineProjection();

    // Full rebuild.
    void build(const ProjectData &project, const SequenceId &sequenceId);
    // True if `changes` alter the structure of the graph (sequence, track list, project settings): update() would
    // replace the whole tractor, so a running consumer must be stopped first.
    bool needsRebuild(const ProjectData &project, const ChangeSet &changes) const;
    // Rebuilds only the tracks listed in `changes` (and the background/duration) with the tractor locked, so it
    // is safe while a consumer runs; falls back to build() when needsRebuild(). Returns true if it rebuilt.
    bool update(const ProjectData &project, const ChangeSet &changes);
    // Re-projects the tracks using a media whose producer just became ready.
    void mediaReady(const ProjectData &project, const MediaId &mediaId);

    Mlt::Tractor *tractor() const { return m_tractor.get(); }
    // Frames of the sequence (at least 1).
    int duration() const { return m_duration; }
    const SequenceId &sequenceId() const { return m_sequenceId; }
    // Clips that cannot be rendered yet (kinds of later phases) or media that failed to open.
    const QStringList &warnings() const { return m_warnings; }

    // Synchronous rendering of one frame (tests, thumbnails of drafts). Not for use while a consumer runs.
    QImage renderFrame(int position);

    // Cuts removed by update() are kept alive for a while: frames already read ahead by a running consumer may
    // still reference them (and their filters) while they are rendered in MLT's threads.
    void releaseRetired(std::chrono::milliseconds olderThan);
    bool hasRetired() const { return !m_retired.empty(); }

private:
    struct TrackSlot
    {
        TrackId id;
        bool audio = false;
        std::unique_ptr<Mlt::Playlist> playlist;
        QSet<MediaId> media;
    };

    struct Retired
    {
        std::chrono::steady_clock::time_point since;
        std::vector<std::unique_ptr<Mlt::Producer>> cuts;
    };

    void fillTrack(TrackSlot &slot, const Track &track, const ProjectData &project, bool anySolo);
    void retireEntries(Mlt::Playlist &playlist);
    void updateBackground();
    std::shared_ptr<Mlt::Producer> producerFor(const Media &media);
    // A model time in frames of the profile.
    std::int64_t toFrames(const RationalTime &time) const;

    Mlt::Profile &m_profile;
    Rational m_rate; // of the profile
    MediaProducerCache &m_cache;
    MediaLoading m_loading;
    SequenceId m_sequenceId;
    std::unique_ptr<Mlt::Tractor> m_tractor;
    std::unique_ptr<Mlt::Playlist> m_background;
    std::unique_ptr<Mlt::Producer> m_black;
    std::vector<TrackSlot> m_tracks; // in tractor order, from track 1
    std::vector<std::unique_ptr<Mlt::Transition>> m_transitions;
    std::vector<std::shared_ptr<Mlt::Producer>> m_keepAlive; // colour producers used by cuts
    int m_duration = 1;
    QStringList m_warnings;
    std::vector<Retired> m_retired;
};

} // namespace vedit::engine
