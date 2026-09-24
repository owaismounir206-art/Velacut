// SPDX-License-Identifier: GPL-3.0-or-later
#include "Player.h"

#include "engine/mlt/MltRuntime.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QPointer>
#include <QThreadPool>

#include <mlt++/Mlt.h>

#include <cmath>

Q_LOGGING_CATEGORY(lcPlayer, "vedit.engine.player")

namespace vedit::engine {

struct Player::Loaded
{
    std::unique_ptr<Mlt::Profile> profile;
    std::unique_ptr<Mlt::Producer> producer;
    QString error;
};

namespace {

// Exact mlt_listener signature (mlt_event_data is a struct passed by value).
void onConsumerFrameShow(mlt_properties, void *owner, mlt_event_data data)
{
    Mlt::Frame frame(mlt_event_data_to_frame(data));
    if (frame.is_valid()) {
        static_cast<Player *>(owner)->deliverFrame(frame);
    }
}

} // namespace

Player::Player(QObject *parent)
    : QObject(parent)
{
}

Player::~Player()
{
    close();
}

void Player::close()
{
    ++m_request; // any pending open() result is now obsolete
    if (m_consumer) {
        m_consumer->stop();
        m_consumer->purge();
    }
    m_frameShowEvent.reset();
    m_consumer.reset();
    m_producer.reset();
    m_profile.reset();
    if (m_playing) {
        m_playing = false;
        emit stateChanged();
    }
}

void Player::open(const QString &path)
{
    const quint64 request = ++m_request;
    m_loading = true;
    m_error.clear();
    emit stateChanged();
    QPointer<Player> self(this);
    QThreadPool::globalInstance()->start([self, path, request] {
        auto loaded = std::make_shared<Loaded>();
        if (!MltRuntime::waitUntilReady()) {
            loaded->error = tr("The video engine (MLT) could not be started.");
        } else if (!QFileInfo(path).isFile()) {
            loaded->error = tr("The file does not exist: %1").arg(path);
        } else {
            // Probe finding: the profile must be right *before* the producer is created (lengths are
            // computed with the profile frame rate), so probe first, then create the real producer.
            const QByteArray resource = QFile::encodeName(path);
            loaded->profile = std::make_unique<Mlt::Profile>();
            {
                Mlt::Producer probe(*loaded->profile, "avformat", resource.constData());
                if (!probe.is_valid()) {
                    loaded->error = tr("This file cannot be opened: the format is not supported or the file is damaged.");
                } else {
                    loaded->profile->from_producer(probe);
                    loaded->profile->set_explicit(1);
                }
            }
            if (loaded->error.isEmpty()) {
                loaded->producer = std::make_unique<Mlt::Producer>(*loaded->profile, "avformat", resource.constData());
                if (!loaded->producer->is_valid()) {
                    loaded->producer.reset();
                    loaded->error = tr("This file cannot be opened: the format is not supported or the file is damaged.");
                }
            }
        }
        QMetaObject::invokeMethod(
            QCoreApplication::instance(),
            [self, loaded, request] {
                if (self) {
                    self->finishOpen(loaded, request);
                }
            },
            Qt::QueuedConnection);
    });
}

void Player::finishOpen(std::shared_ptr<Loaded> loaded, quint64 request)
{
    if (request != m_request) {
        return; // superseded by a newer open() or close()
    }
    close();
    m_request = request;
    m_loading = false;
    if (!loaded->error.isEmpty()) {
        m_error = loaded->error;
        qCWarning(lcPlayer) << "open failed:" << m_error;
        emit stateChanged();
        return;
    }
    m_profile = std::move(loaded->profile);
    m_producer = std::move(loaded->producer);
    m_duration = m_producer->get_length();
    m_frameRate = m_profile->fps();
    m_videoSize = QSize(m_profile->width(), m_profile->height());
    m_position = 0;

    m_consumer = std::make_unique<Mlt::Consumer>(*m_profile, "sdl2_audio");
    if (!m_consumer->is_valid()) {
        m_consumer.reset();
        m_producer.reset();
        m_error = tr("No audio output is available (SDL2).");
        emit stateChanged();
        return;
    }
    // Frames are rendered by the consumer threads directly in RGBA, the format uploaded by the preview.
    m_consumer->set("mlt_image_format", "rgba");
    m_consumer->set("real_time", -2);
    m_consumer->set("terminate_on_pause", 0);
    m_consumer->set("scrub_audio", 1);
    m_consumer->set("volume", m_volume);
    m_consumer->set("frequency", 48000);
    m_consumer->set("channels", 2);
    m_consumer->connect(*m_producer);
    m_frameShowEvent.reset(m_consumer->listen("consumer-frame-show", this, onConsumerFrameShow));
    m_producer->set_speed(0.0);
    m_producer->seek(0);
    m_consumer->start();
    m_consumer->set("refresh", 1);
    m_source = QFileInfo(QFile::decodeName(m_producer->get("resource"))).absoluteFilePath();
    qCInfo(lcPlayer) << "opened" << m_source << m_videoSize << m_frameRate << "fps," << m_duration << "frames";
    emit sourceChanged();
    emit stateChanged();
    emit positionChanged();
}

void Player::deliverFrame(Mlt::Frame &frame)
{
    // MLT consumer thread.
    mlt_image_format format = mlt_image_rgba;
    int width = 0;
    int height = 0;
    const uint8_t *data = frame.get_image(format, width, height);
    if (!data || format != mlt_image_rgba || width <= 0 || height <= 0) {
        qCDebug(lcPlayer) << "frame without usable image:" << (data != nullptr) << format << width << height;
        return;
    }
    // Copy the pixels: an MLT frame kept alive after its consumer is closed prevents the consumer from ever
    // being freed (verified with an ASan probe), and the preview may hold the image longer than the consumer.
    // Cost: one copy per displayed frame (~1 ms at 1080p); a zero-copy path needs a release protocol (PROGRESS).
    const QImage image = QImage(data, width, height, width * 4, QImage::Format_RGBA8888).copy();
    const int position = frame.get_position();
    qCDebug(lcPlayer) << "frame" << position << width << "x" << height;
    m_sink.push(image, position);
    QMetaObject::invokeMethod(this, [this, position] { onFrameShown(position); }, Qt::QueuedConnection);
}

void Player::onFrameShown(int position)
{
    if (!m_producer) {
        return;
    }
    if (position != m_position) {
        m_position = position;
        emit positionChanged();
    }
    // Stop at the end instead of looping or freezing on a black frame.
    if (m_playing && position >= m_duration - 1) {
        pause();
    }
}

void Player::play()
{
    if (!m_producer) {
        return;
    }
    if (m_position >= m_duration - 1) {
        m_producer->seek(0);
    }
    qCDebug(lcPlayer) << "play from" << m_position;
    m_producer->set_speed(1.0);
    // While paused, sdl2_audio shows each speed-0 frame and then waits for a "refresh". With parallel
    // rendering (real_time < 0) the read-ahead may already hold more speed-0 frames: without a purge the
    // consumer takes one of them after play(), uses up the refresh and waits forever (verified by reading
    // consumer_sdl2_audio.c and reproduced in tst_player).
    m_consumer->purge();
    m_consumer->set("refresh", 1);
    m_playing = true;
    emit stateChanged();
}

void Player::pause()
{
    if (!m_producer) {
        return;
    }
    qCDebug(lcPlayer) << "pause at" << m_position;
    m_producer->set_speed(0.0);
    // Stay on the frame on screen, not on the frame the read-ahead thread reached.
    m_producer->seek(m_position);
    m_consumer->purge();
    m_consumer->set("refresh", 1);
    m_playing = false;
    emit stateChanged();
}

void Player::togglePlay()
{
    m_playing ? pause() : play();
}

void Player::seek(int frame)
{
    if (!m_producer) {
        return;
    }
    const int target = std::clamp(frame, 0, std::max(0, m_duration - 1));
    qCDebug(lcPlayer) << "seek to" << target;
    m_producer->seek(target);
    m_consumer->purge();
    m_consumer->set("refresh", 1);
    if (!m_playing && target != m_position) {
        m_position = target;
        emit positionChanged();
    }
}

void Player::step(int frames)
{
    if (m_playing) {
        pause();
    }
    seek(m_position + frames);
}

void Player::setVolume(double volume)
{
    volume = std::clamp(volume, 0.0, 1.0);
    if (qFuzzyCompare(volume, m_volume)) {
        return;
    }
    m_volume = volume;
    if (m_consumer) {
        m_consumer->set("volume", m_volume);
    }
    emit volumeChanged();
}

QString Player::timecode(int frame) const
{
    if (m_frameRate <= 0.0) {
        return QStringLiteral("00:00");
    }
    const int fps = static_cast<int>(std::lround(m_frameRate));
    const int totalSeconds = static_cast<int>(frame / m_frameRate);
    const int frames = fps > 0 ? frame % fps : 0;
    return QStringLiteral("%1:%2.%3")
        .arg(totalSeconds / 60, 2, 10, QLatin1Char('0'))
        .arg(totalSeconds % 60, 2, 10, QLatin1Char('0'))
        .arg(frames, 2, 10, QLatin1Char('0'));
}

} // namespace vedit::engine
