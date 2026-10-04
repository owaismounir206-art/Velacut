// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/project/ChangeSet.h"
#include "core/project/Id.h"
#include "engine/playback/FrameSink.h"
#include "engine/timeline/TimelineProjection.h"

#include <QObject>
#include <QPointer>
#include <QSize>
#include <QStringList>
#include <QTimer>

#include <atomic>
#include <memory>

namespace Mlt {
class Consumer;
class Event;
class Frame;
class Profile;
} // namespace Mlt

namespace vedit {
class Project;
}

namespace vedit::engine {

class MediaProducerCache;
class ReverseProxyQueue;
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
    Q_PROPERTY(vedit::engine::FrameSink *sink READ sink CONSTANT FINAL)
    Q_PROPERTY(bool ready READ ready NOTIFY formatChanged FINAL)
    Q_PROPERTY(bool playing READ playing NOTIFY stateChanged FINAL)
    Q_PROPERTY(double rate READ rate NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool skimming READ skimming NOTIFY stateChanged FINAL)
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
    // vedit-render, for the backwards copies (default: next to the running executable).
    void setHelperExecutable(const QString &path);
    // Preview rendered with at most this short side (0 = the canvas size). The frames get smaller, not the
    // timeline (frame rate and positions are untouched); the export always renders at full size. For
    // shared-memory iGPUs and software rendering, where every preview pixel costs real bandwidth (SPEC 1bis
    // rules 8 and 9). Must be set before setSequence().
    void setPreviewLimit(int shortSide) { m_previewLimit = shortSide; }

    Q_INVOKABLE void play();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void togglePlay();
    // J/K/L: L plays forward and doubles the speed at each press (up to 8x), J the same backwards, K pauses.
    Q_INVOKABLE void shuttleForward();
    Q_INVOKABLE void shuttleBackward();
    // Moves the playhead (and pauses skimming).
    Q_INVOKABLE void seek(int frame);
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

signals:
    void stateChanged();
    void positionChanged();
    void shownPositionChanged();
    void durationChanged();
    void formatChanged();
    void volumeChanged();
    void warningsChanged();
    void reverseChanged();

private:
    void createGraph();
    void destroyGraph();
    void rebuildGraph();
    void onProjectChanged(const ChangeSet &changes);
    void onMediaReady(const MediaId &mediaId);
    void afterProjectionChange();
    void setRate(double rate);
    void showFrame(int frame);
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
    QTimer m_retiredTimer;
    std::unique_ptr<ReverseProxyQueue> m_reverse;
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
};

} // namespace vedit::engine
