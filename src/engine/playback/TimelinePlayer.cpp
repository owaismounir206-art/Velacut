// SPDX-License-Identifier: GPL-3.0-or-later
#include "TimelinePlayer.h"

#include "core/project/Project.h"
#include "engine/analysis/ReverseProxy.h"
#include "engine/analysis/SmoothMotion.h"
#include "engine/playback/AudioMeters.h"
#include "engine/playback/PreviewConsumer.h"
#include "engine/timeline/MediaProducerCache.h"
#include "engine/timeline/TimelineProjection.h"

#include <QLoggingCategory>

#include <algorithm>
#include <cmath>

Q_LOGGING_CATEGORY(lcTimelinePlayer, "vedit.engine.timelineplayer")

using namespace std::chrono_literals;

namespace vedit::engine {

namespace {
constexpr double kMaxShuttleRate = 8.0;
}

TimelinePlayer::TimelinePlayer(QObject *parent)
    : QObject(parent)
    , m_reverse(std::make_unique<ReverseProxyQueue>())
    , m_smooth(std::make_unique<SmoothCopyQueue>())
{
    m_seekClock.start();
    connect(m_smooth.get(), &SmoothCopyQueue::busyChanged, this, &TimelinePlayer::smoothChanged);
    connect(m_smooth.get(), &SmoothCopyQueue::progressChanged, this, &TimelinePlayer::smoothChanged);
    connect(m_smooth.get(), &SmoothCopyQueue::ready, this, &TimelinePlayer::onMediaReady);
    connect(m_smooth.get(), &SmoothCopyQueue::failed, this,
            [this](const MediaId &, const QString &error) { emit smoothFailed(error); });
    connect(m_reverse.get(), &ReverseProxyQueue::busyChanged, this, &TimelinePlayer::reverseChanged);
    connect(m_reverse.get(), &ReverseProxyQueue::progressChanged, this, &TimelinePlayer::reverseChanged);
    connect(m_reverse.get(), &ReverseProxyQueue::ready, this, [this](const MediaId &mediaId) {
        if (m_cache) {
            m_cache->forget(mediaId); // the next producer reads the backwards copy
            if (m_project) {
                if (const Media *media = m_project->data().findMedia(mediaId)) {
                    if (const Sequence *seq = m_project->data().findSequence(m_sequenceId)) {
                        for (const auto *tracks : {&seq->visualTracks, &seq->audioTracks}) {
                            for (const Track &track : *tracks) {
                                for (const Clip &clip : track.clips) {
                                    if (const MediaClipData *data = clip.media();
                                        data && data->mediaId == mediaId && data->reversed) {
                                        const double speed = media->kind == MediaKind::Image ? 1.0 : data->speed;
                                        // In background, like every file (never MLT's loader on this thread):
                                        // the cache's ready() refreshes the picture when it is open.
                                        m_cache->producerOrRequest(*media, -speed, data->preservePitch);
                                    }
                                }
                            }
                        }
                    }
                }
            }
            onMediaReady(mediaId);
        }
    });
    // Cuts replaced during playback are released once no frame read ahead can reference them any more.
    m_retiredTimer.setInterval(1000);
    connect(&m_retiredTimer, &QTimer::timeout, this, [this] {
        if (m_projection) {
            m_projection->releaseRetired(1s);
            if (!m_projection->hasRetired()) {
                m_retiredTimer.stop();
            }
        }
    });
}

TimelinePlayer::~TimelinePlayer()
{
    close();
}

void TimelinePlayer::setSequence(Project *project, const SequenceId &sequenceId)
{
    close();
    m_project = project;
    m_sequenceId = sequenceId;
    if (!project) {
        return;
    }
    m_projectConnection = connect(project, &Project::changed, this, &TimelinePlayer::onProjectChanged);
    m_position = 0;
    m_shownPosition = 0;
    createGraph();
    emit positionChanged();
    emit shownPositionChanged();
}

void TimelinePlayer::close()
{
    disconnect(m_projectConnection);
    m_project.clear();
    destroyGraph();
    if (m_rate != 0.0 || m_skimming) {
        m_rate = 0.0;
        m_skimming = false;
        emit stateChanged();
    }
}

void TimelinePlayer::createGraph()
{
    const ProjectData &data = m_project->data();
    ++m_generation;
    m_error.clear();
    m_profile = makeProfile(data, m_sequenceId);
    // The preview frames get smaller, never the timeline: frame rate and positions are untouched, and the
    // export always renders at the canvas size. On a shared-memory iGPU every preview pixel costs real
    // bandwidth (Radeon 740M, Intel Arc 130V), and in software rendering every pixel costs CPU.
    if (m_previewLimit > 0) {
        const int shortSide = std::min(m_profile->width(), m_profile->height());
        if (shortSide > m_previewLimit) {
            const double scale = static_cast<double>(m_previewLimit) / shortSide;
            const auto even = [](double value) { return std::max(2, static_cast<int>(std::lround(value / 2.0)) * 2); };
            m_profile->set_width(even(m_profile->width() * scale));
            m_profile->set_height(even(m_profile->height() * scale));
            qCInfo(lcTimelinePlayer) << "preview limited to short side" << m_previewLimit << "px: frames are"
                                     << m_profile->width() << u'×' << m_profile->height();
        }
    }
    m_cache = std::make_unique<MediaProducerCache>(*m_profile);
    m_cache->setUseReverseProxies(true);
    connect(m_cache.get(), &MediaProducerCache::ready, this, &TimelinePlayer::onMediaReady);
    m_projection = std::make_unique<TimelineProjection>(*m_profile, *m_cache, TimelineProjection::MediaLoading::Background);
    m_projection->build(data, m_sequenceId);
    m_consumer = createPreviewConsumer(*m_profile, m_volume);
    if (!m_consumer) {
        m_error = tr("No audio output is available (SDL2).");
        qCWarning(lcTimelinePlayer) << m_error;
    } else {
        m_consumer->connect(*m_projection->tractor());
        m_frameShowEvent = listenFrameShow(*m_consumer, this);
        m_duration = m_projection->duration();
        m_position = std::clamp(m_position, 0, m_duration - 1);
        m_projection->tractor()->set_speed(0.0);
        m_projection->tractor()->seek(m_position);
        m_consumer->start();
        m_consumer->set("refresh", 1);
    }
    m_frameRate = m_profile->fps();
    m_canvasSize = QSize(m_profile->width(), m_profile->height());
    qCInfo(lcTimelinePlayer) << "graph for" << m_canvasSize << m_frameRate << "fps," << m_duration << "frames";
    emit formatChanged();
    emit durationChanged();
    emit stateChanged();
    updateWarnings();
    requestReverseProxies();
}

void TimelinePlayer::requestReverseProxies()
{
    if (!m_project) {
        return;
    }
    const ProjectData &data = m_project->data();
    const Sequence *sequence = data.findSequence(m_sequenceId);
    if (!sequence) {
        return;
    }
    for (const auto *tracks : {&sequence->visualTracks, &sequence->audioTracks}) {
        for (const Track &track : *tracks) {
            for (const Clip &clip : track.clips) {
                const MediaClipData *media = clip.media();
                const Media *item = media ? data.findMedia(media->mediaId) : nullptr;
                if (!item) {
                    continue;
                }
                if (media->reversed) {
                    m_reverse->request(*item);
                }
                if (const std::optional<SmoothCopy> copy = smoothCopyFor(clip, *item); copy && !copy->ready() && !media->cutout) {
                    m_smooth->request(*copy);
                }
            }
        }
    }
}

bool TimelinePlayer::preparingSmooth() const
{
    return m_smooth->busy();
}

double TimelinePlayer::smoothProgress() const
{
    return m_smooth->progress();
}

bool TimelinePlayer::preparingReverse() const
{
    return m_reverse->busy();
}

double TimelinePlayer::reverseProgress() const
{
    return m_reverse->progress();
}

void TimelinePlayer::setHelperExecutable(const QString &path)
{
    m_reverse->setExecutable(path);
}

void TimelinePlayer::destroyGraph()
{
    ++m_generation;
    if (m_consumer) {
        m_consumer->stop();
        m_consumer->purge();
    }
    m_retiredTimer.stop();
    m_frameShowEvent.reset();
    m_consumer.reset();
    m_projection.reset();
    m_cache.reset();
    m_profile.reset();
}

void TimelinePlayer::rebuildGraph()
{
    // The consumer holds the tractor and reads ahead: stop it before replacing the graph.
    ++m_generation;
    m_consumer->stop();
    m_consumer->purge();
    m_projection->build(m_project->data(), m_sequenceId);
    m_consumer->connect(*m_projection->tractor());
    m_projection->tractor()->set_speed(m_rate);
    m_consumer->start();
    afterProjectionChange();
}

void TimelinePlayer::onProjectChanged(const ChangeSet &changes)
{
    if (!m_project || !m_projection) {
        return;
    }
    const ProjectData &data = m_project->data();
    if (!profileMatches(*m_profile, data, m_sequenceId)) {
        // New canvas or frame rate: producers are bound to the profile, so everything is recreated.
        const RationalTime playhead(m_position, Rational(m_profile->frame_rate_num(), m_profile->frame_rate_den()));
        const double rate = m_rate;
        destroyGraph();
        m_position = static_cast<int>(playhead.rescaled(data.settings.frameRate, Rounding::Floor).value());
        createGraph();
        if (rate != 0.0) {
            setRate(rate);
        }
        return;
    }
    if (!m_consumer) {
        m_projection->update(data, changes);
        updateWarnings();
        requestReverseProxies();
        return;
    }
    if (m_projection->needsRebuild(data, changes)) {
        rebuildGraph();
    } else {
        m_projection->update(data, changes);
        afterProjectionChange();
    }
    requestReverseProxies();
}

void TimelinePlayer::onMediaReady(const MediaId &mediaId)
{
    if (!m_project || !m_projection) {
        return;
    }
    m_projection->mediaReady(m_project->data(), mediaId);
    afterProjectionChange();
}

void TimelinePlayer::afterProjectionChange()
{
    if (m_projection->hasRetired() && !m_retiredTimer.isActive()) {
        m_retiredTimer.start();
    }
    if (m_projection->duration() != m_duration) {
        m_duration = m_projection->duration();
        emit durationChanged();
    }
    if (m_position > m_duration - 1) {
        setPosition(m_duration - 1);
    }
    if (!m_consumer) {
        updateWarnings();
        return;
    }
    // Frames already read ahead show the old graph: drop them and render again from the frame on screen.
    showFrame(playing() ? m_shownPosition : (m_skimming ? m_shownPosition : m_position));
    updateWarnings();
}

void TimelinePlayer::showFrame(int frame)
{
    if (!m_consumer) {
        return;
    }
    Mlt::Producer *tractor = m_projection->tractor();
    tractor->seek(std::clamp(frame, 0, m_duration - 1));
    m_consumer->purge();
    m_consumer->set("refresh", 1);
}

void TimelinePlayer::setRate(double rate)
{
    if (!m_consumer) {
        return;
    }
    if (rate != 0.0 && m_skimming) {
        m_skimming = false;
    }
    Mlt::Producer *tractor = m_projection->tractor();
    if (rate > 0.0 && m_position >= m_duration - 1) {
        setPosition(0);
    } else if (rate < 0.0 && m_position <= 0) {
        setPosition(m_duration - 1);
    }
    qCDebug(lcTimelinePlayer) << "rate" << rate << "at" << m_position;
    tractor->set_speed(rate);
    // Stay on the playhead, not on the frame the read-ahead reached. The purge is also required when starting:
    // speed-0 frames already read ahead would use up the refresh and stall sdl2_audio (see Player::play()).
    tractor->seek(m_position);
    m_consumer->purge();
    m_consumer->set("refresh", 1);
    // The purge above discarded any seek response still in flight: don't wait for a frame that will
    // never arrive, or the next scrub would silently swallow its first seek for up to 120 ms.
    m_seekInFlight = false;
    m_pendingScrubFrame.reset();
    m_rate = rate;
    emit stateChanged();
}

void TimelinePlayer::play()
{
    setRate(1.0);
}

void TimelinePlayer::pause()
{
    if (m_rate != 0.0) {
        setRate(0.0);
    }
}

void TimelinePlayer::togglePlay()
{
    playing() ? pause() : play();
}

void TimelinePlayer::shuttleForward()
{
    setRate(m_rate > 0.0 ? std::min(m_rate * 2.0, kMaxShuttleRate) : 1.0);
}

void TimelinePlayer::shuttleBackward()
{
    setRate(m_rate < 0.0 ? std::max(m_rate * 2.0, -kMaxShuttleRate) : -1.0);
}

void TimelinePlayer::seek(int frame)
{
    commitSeek(frame);
}

void TimelinePlayer::scrubSeek(int frame)
{
    const int target = std::clamp(frame, 0, std::max(0, m_duration - 1));
    setPosition(target);
    // A drag takes over from hover skimming: stop it so the skim hairline doesn't stick on the playhead.
    if (m_skimming) {
        m_skimming = false;
        emit stateChanged();
    }

    const qint64 now = m_seekClock.elapsed();
    // Safety watchdog: reset stuck in-flight flag after 120ms
    if (m_seekInFlight && (now - m_lastSeekMs > 120)) {
        m_seekInFlight = false;
    }

    // Backpressure & rate-limiting: coalesce intermediate seeks to prevent choking the MLT consumer/tractor
    if (m_seekInFlight || (now - m_lastSeekMs < 33)) {
        m_pendingScrubFrame = target;
        return;
    }

    m_pendingScrubFrame.reset();
    m_seekInFlight = true;
    m_lastSeekMs = now;
    showFrame(target);
}

void TimelinePlayer::commitSeek(int frame)
{
    const int target = std::clamp(frame, 0, std::max(0, m_duration - 1));
    const bool wasSkimming = m_skimming;
    m_skimming = false;
    // Already on the requested frame and nothing still rendering: skip the purge/refresh storm that
    // the 60 ms debounce timer would otherwise send during fast scrubbing (async backlog prevention).
    if (!wasSkimming && target == m_position && target == m_shownPosition
        && !m_seekInFlight && !m_pendingScrubFrame.has_value()) {
        return;
    }
    m_pendingScrubFrame.reset();
    m_seekInFlight = true;
    m_lastSeekMs = m_seekClock.elapsed();
    setPosition(target);
    showFrame(target);
    if (wasSkimming) {
        emit stateChanged();
    }
}

void TimelinePlayer::step(int frames)
{
    pause();
    seek(m_position + frames);
}

void TimelinePlayer::skim(int frame)
{
    if (playing() || !m_consumer) {
        return;
    }
    const qint64 now = m_seekClock.elapsed();
    // Safety watchdog (same as scrubSeek): never stay stuck if a seek response never arrives.
    if (m_seekInFlight && (now - m_lastSeekMs > 120)) {
        m_seekInFlight = false;
    }
    // Backpressure: skimming (hover axis, trim handles) used to call showFrame() for every pointer move,
    // bypassing the scrub rate limiting and flooding the MLT consumer with async seeks (frame backlog,
    // stale preview, playhead/cursor desync). One seek in flight at a time: the next hover point wins.
    if (m_seekInFlight || m_pendingScrubFrame.has_value()) {
        return;
    }
    const int target = std::clamp(frame, 0, std::max(0, m_duration - 1));
    if (!m_skimming) {
        m_skimming = true;
        emit stateChanged();
    }
    m_seekInFlight = true;
    m_lastSeekMs = now;
    showFrame(target);
}

void TimelinePlayer::endSkim()
{
    if (!m_skimming) {
        return;
    }
    m_skimming = false;
    emit stateChanged();
    if (!playing()) {
        showFrame(m_position);
    }
}

void TimelinePlayer::setPosition(int position)
{
    if (position != m_position) {
        m_position = position;
        emit positionChanged();
    }
}

void TimelinePlayer::setVolume(double volume)
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

QString TimelinePlayer::timecode(int frame) const
{
    if (m_frameRate <= 0.0) {
        return QStringLiteral("00:00.00");
    }
    const int fps = static_cast<int>(std::lround(m_frameRate));
    const int totalSeconds = static_cast<int>(frame / m_frameRate);
    const int frames = fps > 0 ? frame % fps : 0;
    return QStringLiteral("%1:%2.%3")
        .arg(totalSeconds / 60, 2, 10, QLatin1Char('0'))
        .arg(totalSeconds % 60, 2, 10, QLatin1Char('0'))
        .arg(frames, 2, 10, QLatin1Char('0'));
}

double TimelinePlayer::audioLevel(const QString &key) const
{
    return AudioMeters::level(key.toLatin1());
}

void TimelinePlayer::setPreview(TimelineProjection::Preview preview)
{
    if (!m_project || !m_projection) {
        return;
    }
    m_projection->setPreview(m_project->data(), std::move(preview));
    afterProjectionChange();
}

void TimelinePlayer::clearPreview()
{
    setPreview({});
}

void TimelinePlayer::deliverFrame(Mlt::Frame &frame)
{
    // MLT consumer thread. Cost of the copy: ~1 ms per frame at 1080p (D-19).
    const QImage image = copyFrameImage(frame);
    const int position = frame.get_position();
    if (image.isNull()) {
        qCDebug(lcTimelinePlayer) << "frame without usable image" << position;
        return;
    }
    m_sink.push(image, position);
    QMetaObject::invokeMethod(this, [this, position, generation = m_generation.load()] { onFrameShown(position, generation); },
                              Qt::QueuedConnection);
}

void TimelinePlayer::onFrameShown(int position, quint64 generation)
{
    if (generation != m_generation || !m_consumer) {
        return;
    }
    m_seekInFlight = false;
    if (position != m_shownPosition) {
        m_shownPosition = position;
        emit shownPositionChanged();
    }
    // If a scrub seek request arrived while previous frame was rendering, dispatch the latest one now
    if (m_pendingScrubFrame.has_value()) {
        const int target = *m_pendingScrubFrame;
        m_pendingScrubFrame.reset();
        m_seekInFlight = true;
        m_lastSeekMs = m_seekClock.elapsed();
        showFrame(target);
        return;
    }
    if (!playing()) {
        return;
    }
    setPosition(position);
    // Stop at the ends instead of looping or freezing on a black frame.
    if ((m_rate > 0.0 && position >= m_duration - 1) || (m_rate < 0.0 && position <= 0)) {
        pause();
    }
}

void TimelinePlayer::updateWarnings()
{
    const QStringList warnings = m_projection ? m_projection->warnings() : QStringList();
    if (warnings != m_warnings) {
        m_warnings = warnings;
        emit warningsChanged();
    }
}

} // namespace vedit::engine
