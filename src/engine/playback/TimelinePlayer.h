// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/project/ChangeSet.h"
#include "core/project/Id.h"
#include "engine/playback/FrameSink.h"
#include "engine/timeline/TimelineProjection.h"

#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QSize>
#include <QStringList>
#include <QTimer>

#include <atomic>
#include <memory>
#include <optional>

namespace Mlt {
class Consumer;
class Event;
class Filter;
class Frame;
class Profile;
} // namespace Mlt

namespace velacut {
class Project;
}

namespace velacut::engine {

class MediaProducerCache;
class ReverseProxyQueue;
class SmoothCopyQueue;
class TimelineProjection;

// Plays a sequence of the live project in the preview (docs/ARCHITECTURE.md §5.3). The MLT graph is a projection
// of the model kept up to date from Project::changed(): small edits patch the running graph, structural edits
// rebuild it, a new canvas or frame rate recreates the profile. Positions are frames at the project frame rate.
//
// The playhead (`position`) is separate from the frame on screen (`shownPosition`): skimming shows the frame under
// the pointer without moving the playhead, and endSkim() goes back to it.
class TimelinePlayer : public QObject
{
    Q_OBJECT
    Q_PROPERTY(velacut::engine::FrameSink *sink READ sink CONSTANT FINAL)
    Q_PROPERTY(bool ready READ ready NOTIFY formatChanged FINAL)
    Q_PROPERTY(bool playing READ playing NOTIFY stateChanged FINAL)
    Q_PROPERTY(double rate READ rate NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool skimming READ skimming NOTIFY stateChanged FINAL)
    // True while a scrub drag holds the audio detached (MLT level, see setScrubMuted): the drag is
    // silent, the commit lands with the audio back on (and playback resumed, if it was playing).
    Q_PROPERTY(bool scrubMuted READ scrubMuted NOTIFY stateChanged FINAL)
    Q_PROPERTY(int position READ position NOTIFY positionChanged FINAL)
    Q_PROPERTY(int shownPosition READ shownPosition NOTIFY shownPositionChanged FINAL)
    Q_PROPERTY(int duration READ duration NOTIFY durationChanged FINAL)
    Q_PROPERTY(double frameRate READ frameRate NOTIFY formatChanged FINAL)
    Q_PROPERTY(QSize canvasSize READ canvasSize NOTIFY formatChanged FINAL)
    Q_PROPERTY(double volume READ volume WRITE setVolume NOTIFY volumeChanged FINAL)
    Q_PROPERTY(QString error READ error NOTIFY stateChanged FINAL)
    Q_PROPERTY(QStringList warnings READ warnings NOTIFY warningsChanged FINAL)
    // A backwards copy of a reversed clip is being prepared (the clip plays, slowly, meanwhile).
    Q_PROPERTY(bool preparingReverse READ preparingReverse NOTIFY reverseChanged FINAL)
    Q_PROPERTY(double reverseProgress READ reverseProgress NOTIFY reverseChanged FINAL)
    // Smooth slow motion being computed for the preview (SmoothCopyQueue).
    Q_PROPERTY(bool preparingSmooth READ preparingSmooth NOTIFY smoothChanged FINAL)
    Q_PROPERTY(double smoothProgress READ smoothProgress NOTIFY smoothChanged FINAL)
    // Preview statistics for the debug HUD (VELACUT_PREVIEW_HUD=1), refreshed twice a second while statsEnabled:
    // frames shown per second, frames lost (skipped by the consumer while playing, or replaced before the
    // preview took them), average time to render a frame and the size it is rendered at.
    Q_PROPERTY(bool statsEnabled READ statsEnabled WRITE setStatsEnabled NOTIFY statsChanged FINAL)
    Q_PROPERTY(double statsFps READ statsFps NOTIFY statsChanged FINAL)
    Q_PROPERTY(int statsDropped READ statsDropped NOTIFY statsChanged FINAL)
    Q_PROPERTY(double statsRenderMs READ statsRenderMs NOTIFY statsChanged FINAL)
    Q_PROPERTY(QSize statsRenderSize READ statsRenderSize NOTIFY statsChanged FINAL)

public:
    explicit TimelinePlayer(QObject *parent = nullptr);
    ~TimelinePlayer() override;

    FrameSink *sink() { return &m_sink; }

    // Shows `sequenceId` of `project`; media producers are opened in background (their clips appear when ready).
    // MLT must be initialized (MltRuntime::waitUntilReady()).
    void setSequence(Project *project, const SequenceId &sequenceId);
    // Stops and releases every MLT object (call before MltRuntime::shutdown()).
    void close();

    bool ready() const { return m_consumer != nullptr; }
    bool playing() const { return m_rate != 0.0; }
    double rate() const { return m_rate; }
    bool skimming() const { return m_skimming; }
    bool scrubMuted() const { return m_isScrubMuted; }
    int position() const { return m_position; }
    int shownPosition() const { return m_shownPosition; }
    int duration() const { return m_duration; }
    double frameRate() const { return m_frameRate; }
    QSize canvasSize() const { return m_canvasSize; }
    double volume() const { return m_volume; }
    void setVolume(double volume);
    QString error() const { return m_error; }
    QStringList warnings() const { return m_warnings; }
    bool preparingReverse() const;
    double reverseProgress() const;
    bool preparingSmooth() const;
    double smoothProgress() const;
    // velacut-render, for the backwards copies (default: next to the running executable).
    void setHelperExecutable(const QString &path);
    // Preview rendered with at most this short side (0 = the canvas size). The frames get smaller, not the
    // timeline (frame rate and positions are untouched); the export always renders at full size. For
    // shared-memory iGPUs and software rendering, where every preview pixel costs real bandwidth (SPEC 1bis
    // rules 8 and 9). Must be set before setSequence().
    void setPreviewLimit(int shortSide) { m_previewLimit = shortSide; }

    bool statsEnabled() const { return m_statsTimer.isActive(); }
    void setStatsEnabled(bool enabled);
    double statsFps() const { return m_statsFps; }
    int statsDropped() const { return static_cast<int>(m_skippedFrames + m_sink.framesDropped()); }
    double statsRenderMs() const { return m_timing->averageMs.load(std::memory_order_relaxed); }
    QSize statsRenderSize() const;
    // Render time of the frames of the current graph (any thread writes, read here).
    const FrameTiming &frameTiming() const { return *m_timing; }

    Q_INVOKABLE void play();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void togglePlay();
    // J/K/L: L plays forward and doubles the speed at each press (up to 8x), J the same backwards, K pauses.
    Q_INVOKABLE void shuttleForward();
    Q_INVOKABLE void shuttleBackward();
    // Moves the playhead (and pauses skimming).
    Q_INVOKABLE void seek(int frame);
    // Moves the playhead while dragging it: the frame follows as fast as frames render (one render at a time, the
    // latest position wins, see requestFrame).
    Q_INVOKABLE void scrubSeek(int frame);
    // Detaches the audio for a whole scrub drag, at the MLT level (not a Qt volume dip): sdl2_audio
    // plays the audio of every frame it shows, refresh (speed 0) frames included, so each frame the
    // drag lands on would be heard. The commit (commitSeek) switches the audio back on. `muted` is
    // also where the playing state before the drag is remembered, to be restored on the commit.
    Q_INVOKABLE void setScrubMuted(bool muted);
    // End of a drag (release or pause of the pointer): the playhead lands on `frame`, the audio comes back.
    Q_INVOKABLE void commitSeek(int frame);
    Q_INVOKABLE void step(int frames);
    // Shows `frame` without moving the playhead (pointer over the timeline); ignored while playing.
    Q_INVOKABLE void skim(int frame);
    Q_INVOKABLE void endSkim();
    Q_INVOKABLE QString timecode(int frame) const;
    // Level 0…1 of a track's audio (key: its id) or of the mix ("master"), for the meters; ~30 reads per second.
    Q_INVOKABLE double audioLevel(const QString &key) const;

    // Live preview (pointer over a library item): shown until cleared, never saved.
    void setPreview(TimelineProjection::Preview preview);
    void clearPreview();

    // Called from the MLT consumer thread for every frame shown.
    void deliverFrame(Mlt::Frame &frame);
    // Tests: every frame takes at least `ms` to render (a slow effect, without depending on how fast effects are).
    void setMinimumRenderTime(int ms) { m_timing->minimumMs.store(ms); }

signals:
    void stateChanged();
    void positionChanged();
    void shownPositionChanged();
    void durationChanged();
    void formatChanged();
    void volumeChanged();
    void warningsChanged();
    void reverseChanged();
    void smoothChanged();
    void statsChanged();
    // A smooth slow motion could not be computed (for the snackbar).
    void smoothFailed(const QString &error);

private:
    void createGraph();
    // The render time of every frame, measured on the tractor (after each build: a rebuild makes a new tractor).
    void attachTiming();
    void updateStats();
    void destroyGraph();
    void rebuildGraph();
    void onProjectChanged(const ChangeSet &changes);
    void onMediaReady(const MediaId &mediaId);
    void afterProjectionChange();
    void setRate(double rate);
    void showFrame(int frame);
    // While paused, every frame shown on request (scrub, skim, seek, an edit) goes through here: one render at a time,
    // and a render in progress is never thrown away for a newer request, which waits (the latest one wins) and is
    // sent when the frame arrives. Purging the consumer for every request, as before, discarded the frame being
    // rendered: with frames slower than the requests (a blur, a transition), nothing was shown until the pointer
    // stopped (docs/PERFORMANCE.md).
    void requestFrame(int frame);
    void dispatchFrame(int frame);
    // Recovery only, if the answer to a request never comes: the time allowed grows with the render time measured.
    void onRenderWatchdog();
    // Audio back on after a scrub drag, wherever the commit lands (see setScrubMuted).
    void restoreAudioAfterScrub();
    // Playback resumed where the drag had paused it, on the commit (see setScrubMuted).
    void resumeAfterScrub();
    // The audio of the hover preview (skim): off while it lasts, back with the playhead frame.
    void startSkimMute();
    void stopSkimMute();
    void onFrameShown(int position, quint64 generation);
    void setPosition(int position);
    void updateWarnings();
    void requestReverseProxies();

    FrameSink m_sink;
    QPointer<Project> m_project;
    SequenceId m_sequenceId;
    QMetaObject::Connection m_projectConnection;
    // Destruction order matters: consumer, then projection, then producers, then profile.
    std::unique_ptr<Mlt::Profile> m_profile;
    std::unique_ptr<MediaProducerCache> m_cache;
    std::unique_ptr<TimelineProjection> m_projection;
    std::unique_ptr<Mlt::Consumer> m_consumer;
    std::unique_ptr<Mlt::Event> m_frameShowEvent;
    std::shared_ptr<FrameTiming> m_timing = std::make_shared<FrameTiming>();
    std::unique_ptr<Mlt::Filter> m_timingFilter;
    QTimer m_statsTimer;
    QElapsedTimer m_statsClock;
    quint64 m_statsFramesBefore = 0;
    double m_statsFps = 0.0;
    quint64 m_skippedFrames = 0;
    int m_lastPlayedPosition = -1;
    QTimer m_retiredTimer;
    std::unique_ptr<ReverseProxyQueue> m_reverse;
    std::unique_ptr<SmoothCopyQueue> m_smooth;
    // Frames queued from the consumer thread before a graph change are ignored.
    std::atomic<quint64> m_generation{0};
    double m_rate = 0.0;
    bool m_skimming = false;
    int m_position = 0;
    int m_shownPosition = 0;
    int m_duration = 1;
    double m_frameRate = 0.0;
    QSize m_canvasSize;
    double m_volume = 1.0;
    int m_previewLimit = 0;
    QString m_error;
    QStringList m_warnings;
    bool m_renderInFlight = false;
    int m_inFlightFrame = 0;
    bool m_inFlightCurrent = false; // no edit since the frame in flight was asked for
    std::optional<int> m_pendingFrame;
    int m_skimTarget = 0;
    QTimer m_renderWatchdog;
    // Audio detached while scrubbing (see setScrubMuted): the drag is silent, the commit is not.
    bool m_isScrubMuted = false;
    bool m_wasPlayingBeforeScrub = false;
    // Same MLT switch, held by the hover preview instead of a drag (see skim): the skim is silent,
    // and its mute never unmutes a drag that has taken over in the meantime.
    bool m_skimMuted = false;
};

} // namespace velacut::engine
