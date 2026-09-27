// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/project/ChangeSet.h"
#include "core/project/ProjectData.h"
#include "engine/mlt/Services.h"

#include <QDateTime>
#include <QImage>
#include <QHash>
#include <QSize>
#include <QStringList>

#include <chrono>
#include <map>
#include <memory>
#include <vector>

namespace Mlt {
class Filter;
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
// §5.1–5.2). Tractor layout: track 0 = background (black); then, for every visual track bottom-up (main track
// first), its clips and above them its transitions; then the audio tracks. Every visual layer is composited onto
// track 0 with "vedit.composite"; every clips track's audio is summed into track 0 with "mix".
//
// Every clip is a cut of its media producer (a "timewarp" one for speed and reverse) carrying its own filters:
// colour effects in model order, then vedit.transform (placement on the canvas, and the canvas background for the
// main track), then vedit.gain (volume, fades, pan). A transition is a small tractor (the two clips around the cut,
// with the frames missing beyond their material frozen, and vedit.transition) on the transitions layer of its track.
class TimelineProjection
{
public:
    enum class MediaLoading
    {
        Wait,       // open media synchronously (export, tests)
        Background, // missing producers are requested and shown as gaps until ready (UI)
    };

    // What the preview shows instead of the model while the pointer is over an item of a library (SPEC 0bis rule 5):
    // the projection only, never the model or the undo history.
    struct Preview
    {
        std::optional<Clip> clip;             // replaces the clip with the same id
        std::optional<TrackId> transitionTrack; // with `transition`: added or replacing the one on the same cut
        std::optional<Transition> transition;

        bool isEmpty() const { return !clip && !transition; }
    };

    TimelineProjection(Mlt::Profile &profile, MediaProducerCache &cache, MediaLoading loading);
    ~TimelineProjection();

    // Full rebuild.
    void build(const ProjectData &project, const SequenceId &sequenceId, int activeAngle = -1);
    // True if `changes` alter the structure of the graph (sequence, track list, project settings): update() would
    // replace the whole tractor, so a running consumer must be stopped first.
    bool needsRebuild(const ProjectData &project, const ChangeSet &changes) const;
    // Rebuilds only the tracks listed in `changes` (and the background/duration) with the tractor locked, so it
    // is safe while a consumer runs; falls back to build() when needsRebuild(). Returns true if it rebuilt.
    bool update(const ProjectData &project, const ChangeSet &changes);
    // Re-projects the tracks using a media whose producer just became ready.
    void mediaReady(const ProjectData &project, const MediaId &mediaId);
    // Shows `preview` (empty: back to the model). Patches only the tracks concerned.
    void setPreview(const ProjectData &project, Preview preview);

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
    // How a clip is rendered: the filters of its cuts.
    struct ClipRender
    {
        std::optional<ChromaKeySettings> chromaKey;
        std::optional<MaskSettings> maskSettings;
        std::vector<AdjustSettings> adjusts;
        std::optional<DeflickerSettings> deflicker;
        std::optional<TransformSettings> transform;
        std::optional<GainSettings> gain;
        std::optional<AudioEffectsSettings> audioEffects;
        QByteArray key;
    };

    // One entry of a playlist as the model wants it: a cut of `producer` (null: a blank of out+1 frames) with its
    // filters.
    struct Entry
    {
        std::shared_ptr<Mlt::Producer> producer;
        int in = 0;
        int out = 0;
        std::shared_ptr<const ClipRender> render;
        QByteArray key; // of the render

        friend bool operator==(const Entry &a, const Entry &b)
        {
            return a.producer == b.producer && a.in == b.in && a.out == b.out && a.key == b.key;
        }
    };

    enum class SlotKind
    {
        Clips,
        Transitions,
    };

    struct TrackSlot
    {
        TrackId id;
        SlotKind kind = SlotKind::Clips;
        bool audio = false;
        std::unique_ptr<Mlt::Playlist> playlist;
        std::vector<Entry> entries; // what the playlist contains
        QSet<MediaId> media;
        std::unique_ptr<Mlt::Filter> gainFilter; // track volume and meter
        QByteArray gainKey;
        QHash<QByteArray, std::shared_ptr<Mlt::Producer>> transitions; // built transition tractors, by key
    };

    struct Retired
    {
        std::chrono::steady_clock::time_point since;
        std::vector<std::unique_ptr<Mlt::Producer>> cuts;
        std::vector<std::unique_ptr<Mlt::Filter>> filters;
    };

    // A clip placed in output frames, with its producer and filters.
    struct Placed
    {
        const Clip *clip = nullptr;
        std::int64_t start = 0;
        std::int64_t length = 0;
        std::shared_ptr<Mlt::Producer> producer; // null: nothing to show (gap)
        int in = 0;                              // producer frame of the clip's first frame
        std::shared_ptr<const ClipRender> render;
    };

    struct StructureSlot
    {
        TrackId id;
        SlotKind kind;
    };
    static std::vector<StructureSlot> structureOf(const Sequence &sequence, int activeAngle = -1);

    void fillSlot(TrackSlot &slot, const Track &track, const ProjectData &project, bool anySolo);
    std::vector<Entry> clipEntries(TrackSlot &slot, const Track &track, const ProjectData &project);
    std::vector<Entry> transitionEntries(TrackSlot &slot, const Track &track, const ProjectData &project);
    std::optional<Placed> place(const Clip &clip, const Track &track, const ProjectData &project, bool mainTrack,
                                QSet<MediaId> &usedMedia);
    std::shared_ptr<const ClipRender> renderOf(const Clip &clip, const Track &track, const ProjectData &project,
                                               const Media *media, bool mainTrack, int in, std::int64_t length);
    std::shared_ptr<Mlt::Producer> transitionProducer(TrackSlot &slot, const Transition &transition, const Placed &from,
                                                      const Placed &to, std::int64_t windowStart, std::int64_t length);
    void attachFilters(Mlt::Producer &cut, const ClipRender &render, bool withAudio);
    void updateTrackGain(TrackSlot &slot, const Track &track);
    // Changes the playlist into `desired` replacing only the entries that differ (an edit usually touches one or two
    // clips: a 500-clip track is not rebuilt for a trim).
    void patch(TrackSlot &slot, std::vector<Entry> desired);
    std::shared_ptr<Mlt::Producer> colorProducer(const Color &color);
    std::shared_ptr<Mlt::Producer> textProducer(const TextClipData &text);
    std::shared_ptr<Mlt::Producer> compoundProducer(const ProjectData &project, const SequenceId &sequenceId, int activeAngle = 0);
    void retireEntries(Mlt::Playlist &playlist);
    void updateBackground();
    std::shared_ptr<Mlt::Producer> producerFor(const Media &media, double speed, bool preservePitch);
    const Clip &previewed(const Clip &clip) const;
    // A model time in frames of the profile.
    std::int64_t toFrames(const RationalTime &time) const;

    std::shared_ptr<const fx::CubeLut> cubeLut(const QString &path);

    Mlt::Profile &m_profile;
    Rational m_rate; // of the profile
    MediaProducerCache &m_cache;
    MediaLoading m_loading;
    SequenceId m_sequenceId;
    std::unique_ptr<Mlt::Tractor> m_tractor;
    std::unique_ptr<Mlt::Playlist> m_background;
    std::unique_ptr<Mlt::Producer> m_black;
    std::unique_ptr<Mlt::Filter> m_masterMeter;
    std::vector<TrackSlot> m_tracks; // in tractor order, from track 1
    std::vector<std::unique_ptr<Mlt::Transition>> m_transitions;
    std::vector<std::unique_ptr<Mlt::Filter>> m_adjustmentFilters;
    QHash<QByteArray, std::shared_ptr<Mlt::Producer>> m_colors; // colour producers, shared by cuts
    QHash<QByteArray, std::shared_ptr<Mlt::Producer>> m_texts;  // text producers, by content
    QHash<QPair<SequenceId, int>, std::shared_ptr<TimelineProjection>> m_compounds;
    std::map<QString, std::pair<QDateTime, std::shared_ptr<const fx::CubeLut>>> m_cubeLuts;
    Preview m_preview;
    int m_duration = 1;
    int m_backgroundLength = 0;
    QStringList m_warnings;
    std::vector<Retired> m_retired;
};

} // namespace vedit::engine
