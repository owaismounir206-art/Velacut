// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/project/ChangeSet.h"
#include "core/project/ProjectData.h"

#include <QImage>
#include <QStringList>

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

// Profile matching a sequence: canvas size, project frame rate, square pixels, progressive, Rec.709.
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
    // Rebuilds only the tracks listed in `changes` (and the background/duration); falls back to build() when the
    // track structure changed. Returns true if a full rebuild happened.
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

private:
    struct TrackSlot
    {
        TrackId id;
        bool audio = false;
        std::unique_ptr<Mlt::Playlist> playlist;
        QSet<MediaId> media;
    };

    void fillTrack(TrackSlot &slot, const Track &track, const ProjectData &project, bool anySolo);
    void updateBackground();
    std::shared_ptr<Mlt::Producer> producerFor(const Media &media);

    Mlt::Profile &m_profile;
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
};

} // namespace vedit::engine
