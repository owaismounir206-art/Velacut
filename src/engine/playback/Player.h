// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "engine/playback/FrameSink.h"

#include <QObject>
#include <QString>

#include <memory>

namespace Mlt {
class Consumer;
class Event;
class Frame;
class Producer;
class Profile;
} // namespace Mlt

namespace vedit::engine {

// Plays a media file (Phase 0) through MLT: opening happens in a worker thread, rendering and audio in
// MLT's consumer threads (sdl2_audio -> PipeWire/PulseAudio), frames go to a FrameSink. The object itself
// lives in the GUI thread and only issues quick, non-blocking control calls.
class Player : public QObject
{
    Q_OBJECT
    Q_PROPERTY(vedit::engine::FrameSink *sink READ sink CONSTANT FINAL)
    Q_PROPERTY(QString source READ source NOTIFY sourceChanged FINAL)
    Q_PROPERTY(bool loading READ loading NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool ready READ ready NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool playing READ playing NOTIFY stateChanged FINAL)
    Q_PROPERTY(QString error READ error NOTIFY stateChanged FINAL)
    Q_PROPERTY(int position READ position NOTIFY positionChanged FINAL)
    Q_PROPERTY(int duration READ duration NOTIFY sourceChanged FINAL)
    Q_PROPERTY(double frameRate READ frameRate NOTIFY sourceChanged FINAL)
    Q_PROPERTY(QSize videoSize READ videoSize NOTIFY sourceChanged FINAL)
    Q_PROPERTY(double volume READ volume WRITE setVolume NOTIFY volumeChanged FINAL)

public:
    explicit Player(QObject *parent = nullptr);
    ~Player() override;

    FrameSink *sink() { return &m_sink; }

    QString source() const { return m_source; }
    bool loading() const { return m_loading; }
    bool ready() const { return m_producer != nullptr; }
    bool playing() const { return m_playing; }
    QString error() const { return m_error; }
    int position() const { return m_position; }
    int duration() const { return m_duration; }
    double frameRate() const { return m_frameRate; }
    QSize videoSize() const { return m_videoSize; }
    double volume() const { return m_volume; }
    void setVolume(double volume);

    // Opens a file asynchronously; emits sourceChanged/stateChanged when done (or error set).
    Q_INVOKABLE void open(const QString &path);
    Q_INVOKABLE void play();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void togglePlay();
    Q_INVOKABLE void seek(int frame);
    Q_INVOKABLE void step(int frames);
    // "mm:ss:ff" style label for a frame number, for the transport bar.
    Q_INVOKABLE QString timecode(int frame) const;
    // Stops playback and releases every MLT object (call before MltRuntime::shutdown()).
    void close();
    // Called from the MLT consumer thread for every frame shown.
    void deliverFrame(Mlt::Frame &frame);

signals:
    void sourceChanged();
    void stateChanged();
    void positionChanged();
    void volumeChanged();

private:
    struct Loaded;
    void finishOpen(std::shared_ptr<Loaded> loaded, quint64 request);
    void onFrameShown(int position);

    FrameSink m_sink;
    std::unique_ptr<Mlt::Profile> m_profile;
    std::unique_ptr<Mlt::Producer> m_producer;
    std::unique_ptr<Mlt::Consumer> m_consumer;
    std::unique_ptr<Mlt::Event> m_frameShowEvent;
    QString m_source;
    QString m_error;
    bool m_loading = false;
    bool m_playing = false;
    int m_position = 0;
    int m_duration = 0;
    double m_frameRate = 0.0;
    QSize m_videoSize;
    double m_volume = 1.0;
    quint64 m_request = 0;
};

} // namespace vedit::engine
