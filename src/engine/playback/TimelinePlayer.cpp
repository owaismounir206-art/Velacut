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

Q_LOGGING_CATEGORY(lcTimelinePlayer, "velacut.engine.timelineplayer")

using namespace std::chrono_literals;

namespace velacut::engine {

namespace {
constexpr double kMaxShuttleRate = 8.0;
}

TimelinePlayer::TimelinePlayer(QObject *parent)
    : QObject(parent)
    , m_reverse(std::make_unique<ReverseProxyQueue>())
    , m_smooth(std::make_unique<SmoothCopyQueue>())
{
    m_quietTimer.setSingleShot(true);
    m_quietTimer.setInterval(250);
    connect(&m_quietTimer, &QTimer::timeout, this, &TimelinePlayer::onQuiet);
    m_renderWatchdog.setSingleShot(true);
    connect(&m_renderWatchdog, &QTimer::timeout, this, &TimelinePlayer::onRenderWatchdog);
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
    m_statsTimer.setInterval(500);
    connect(&m_statsTimer, &QTimer::timeout, this, &TimelinePlayer::updateStats);
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
    attachTiming();
    m_consumer = createPreviewConsumer(*m_profile, m_volume);
    if (!m_consumer) {
        m_error = tr("No audio output is available (SDL2).");
        qCWarning(lcTimelinePlayer) << m_error;
    } else {
        m_consumer->connect(*m_projection->tractor());
        m_frameShowEvent = listenFrameShow(*m_consumer, this);
        m_renderSize = QSize();
        applyRenderSize();
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

void TimelinePlayer::attachTiming()
{
    m_timingFilter = makeTimingFilter(*m_profile, m_timing);
    if (m_projection->tractor()) {
        m_projection->tractor()->attach(*m_timingFilter);
    }
}

void TimelinePlayer::setStatsEnabled(bool enabled)
{
    if (enabled == statsEnabled()) {
        return;
    }
    if (enabled) {
        m_statsClock.start();
        m_statsFramesBefore = m_sink.framesReceived();
        m_statsTimer.start();
    } else {
        m_statsTimer.stop();
    }
    emit statsChanged();
}

QSize TimelinePlayer::statsRenderSize() const
{
    return QSize(m_timing->width.load(std::memory_order_relaxed), m_timing->height.load(std::memory_order_relaxed));
}

void TimelinePlayer::updateStats()
{
    const quint64 received = m_sink.framesReceived();
    const qint64 elapsed = m_statsClock.restart();
    m_statsFps = elapsed > 0 ? static_cast<double>(received - m_statsFramesBefore) * 1000.0 / static_cast<double>(elapsed) : 0.0;
    m_statsFramesBefore = received;
    emit statsChanged();
}

int TimelinePlayer::renderScale() const
{
    switch (m_quality) {
    case PreviewQuality::Full:
        return 1;
    case PreviewQuality::Half:
        return 2;
    case PreviewQuality::Quarter:
        return 4;
    case PreviewQuality::Auto:
        break;
    }
    return m_interacting ? m_autoScale : 1;
}

void TimelinePlayer::applyRenderSize()
{
    if (!m_consumer || !m_profile) {
        return;
    }
    // The canvas fitted in the preview area (never larger than the canvas), then the quality step.
    const QSize canvas(m_profile->width(), m_profile->height());
    QSizeF size = canvas;
    if (m_viewSize.width() > 0 && m_viewSize.height() > 0) {
        size = QSizeF(canvas).scaled(QSizeF(m_viewSize), Qt::KeepAspectRatio);
        if (size.width() > canvas.width() || size.height() > canvas.height()) {
            size = canvas;
        }
    }
    const int scale = renderScale();
    const auto even = [](double value) { return std::max(2, static_cast<int>(std::lround(value / 2.0)) * 2); };
    const QSize render(even(size.width() / scale), even(size.height() / scale));
    if (render == m_renderSize) {
        return;
    }
    m_renderSize = render;
    // The consumer asks every frame at this size, and the services render at the size they are asked (verified by the
    // benchmark: 960×540 frames from a 1080p graph). The profile, and so the timeline, stays the same.
    m_consumer->set("width", render.width());
    m_consumer->set("height", render.height());
    m_renderWidth.store(render.width(), std::memory_order_relaxed);
    m_renderHeight.store(render.height(), std::memory_order_relaxed);
    m_framesAtScale = 0;
    emit renderSizeChanged();
}

void TimelinePlayer::setViewSize(const QSize &size)
{
    if (size == m_viewSize) {
        return;
    }
    m_viewSize = size;
    const QSize before = m_renderSize;
    applyRenderSize();
    emit renderSizeChanged();
    if (m_renderSize != before && m_consumer && !playing()) {
        requestFrame(m_skimming ? m_skimTarget : m_position, false);
    }
}

void TimelinePlayer::setPreviewQuality(PreviewQuality quality)
{
    if (quality == m_quality) {
        return;
    }
    m_quality = quality;
    m_autoScale = 1;
    const QSize before = m_renderSize;
    applyRenderSize();
    emit renderSizeChanged();
    if (m_renderSize != before && m_consumer && !playing()) {
        requestFrame(m_skimming ? m_skimTarget : m_position, false);
    }
}

void TimelinePlayer::markInteraction()
{
    m_quietTimer.start();
    if (!m_interacting) {
        m_interacting = true;
        applyRenderSize();
    }
}

void TimelinePlayer::onQuiet()
{
    if (playing() || !m_interacting) {
        return;
    }
    // Quiet for 250 ms: the still frame at the full size (Auto), if the last one was smaller.
    m_interacting = false;
    const QSize before = m_renderSize;
    applyRenderSize();
    if (m_renderSize != before && m_consumer) {
        requestFrame(m_skimming ? m_skimTarget : m_position, false);
    }
}

void TimelinePlayer::adaptQuality()
{
    if (m_quality != PreviewQuality::Auto || !m_interacting || m_frameRate <= 0.0 || ++m_framesAtScale < 4) {
        return;
    }
    const double average = m_timing->averageMs.load(std::memory_order_relaxed);
    const double budget = 1000.0 / m_frameRate;
    int scale = m_autoScale;
    if (average > 0.8 * budget && scale < 4) {
        scale *= 2;
    } else if (average < 0.2 * budget && scale > 1) {
        scale /= 2; // four times the pixels: still under 80 % of the budget
    }
    if (scale != m_autoScale) {
        qCDebug(lcTimelinePlayer) << "preview step" << m_autoScale << "->" << scale << "at" << average << "ms a frame";
        m_autoScale = scale;
        applyRenderSize();
    }
}

QImage TimelinePlayer::renderStill(int frame) const
{
    if (!m_project) {
        return {};
    }
    const ProjectData &data = m_project->data();
    auto profile = makeProfile(data, m_sequenceId);
    MediaProducerCache cache(*profile);
    TimelineProjection projection(*profile, cache, TimelineProjection::MediaLoading::Wait);
    projection.build(data, m_sequenceId);
    return projection.renderFrame(std::clamp(frame, 0, std::max(0, projection.duration() - 1)));
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
    // A graph rebuilt mid-drag (undo while scrubbing, a canvas change…) starts with its own consumer:
    // its audio is on, and nothing is left to resume or unmute on the pending commit.
    m_isScrubMuted = false;
    m_skimMuted = false;
    m_wasPlayingBeforeScrub = false;
    m_renderInFlight = false;
    m_pendingFrame.reset();
    m_renderWatchdog.stop();
    m_retiredTimer.stop();
    m_frameShowEvent.reset();
    m_consumer.reset();
    m_timingFilter.reset();
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
    // The frame being rendered went with the old consumer run: afterProjectionChange() asks again.
    m_renderInFlight = false;
    m_renderWatchdog.stop();
    m_projection->build(m_project->data(), m_sequenceId);
    attachTiming();
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
    m_inFlightCurrent = false; // the frame being rendered (if any) shows the graph before this change
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
    if (playing()) {
        // Frames already read ahead show the old graph: drop them and render again from the frame on screen.
        showFrame(m_shownPosition);
    } else {
        // The frame on screen again with the new graph. A slider dragged faster than frames render (60 edits per
        // second) gets one frame per render, each with the latest values, and the last one exact.
        requestFrame(m_skimming ? m_skimTarget : m_position);
    }
    updateWarnings();
}

void TimelinePlayer::showFrame(int frame)
{
    if (!m_consumer) {
        return;
    }
    Mlt::Producer *tractor = m_projection->tractor();
    // A new request: frames made from now on render again (see FrameTiming::stillRequest).
    m_timing->stillRequest.fetch_add(1);
    tractor->seek(std::clamp(frame, 0, m_duration - 1));
    m_consumer->purge();
    m_consumer->set("refresh", 1);
}

void TimelinePlayer::requestFrame(int frame, bool interactive)
{
    if (!m_consumer) {
        return;
    }
    if (interactive) {
        markInteraction();
    }
    const int target = std::clamp(frame, 0, std::max(0, m_duration - 1));
    if (m_renderInFlight) {
        if (target == m_inFlightFrame && m_inFlightCurrent) {
            m_pendingFrame.reset(); // already coming (e.g. the commit of the position a drag last asked for)
        } else {
            m_pendingFrame = target;
        }
        return;
    }
    dispatchFrame(target);
}

void TimelinePlayer::dispatchFrame(int frame)
{
    m_pendingFrame.reset();
    m_renderInFlight = true;
    m_inFlightFrame = frame;
    m_inFlightCurrent = true;
    // Nothing useful is in the consumer's queue now (no render in flight): the purge of showFrame() only drops
    // frames read ahead at the old position or with the old graph.
    showFrame(frame);
    m_renderWatchdog.start(std::max(1000, static_cast<int>(4.0 * m_timing->averageMs.load(std::memory_order_relaxed))));
}

void TimelinePlayer::onRenderWatchdog()
{
    if (!m_renderInFlight || !m_consumer || playing()) {
        return;
    }
    if (m_timing->busySinceMs.load(std::memory_order_relaxed) > 0) {
        // Still rendering (a very slow frame): wait, never throw it away.
        m_renderWatchdog.start(std::max(1000, static_cast<int>(4.0 * m_timing->averageMs.load(std::memory_order_relaxed))));
        return;
    }
    // The answer never came (the paused consumer can miss a wakeup on a saturated machine): ask again.
    qCWarning(lcTimelinePlayer) << "no frame for the request of frame" << m_inFlightFrame << "- asking again";
    m_renderInFlight = false;
    requestFrame(m_pendingFrame.value_or(m_inFlightFrame));
}

void TimelinePlayer::setRate(double rate)
{
    if (!m_consumer) {
        return;
    }
    if (rate != 0.0 && m_skimming) {
        m_skimming = false;
    }
    if (rate != 0.0 && (m_isScrubMuted || m_skimMuted)) {
        // Playback started while a drag or a hover preview was still holding the audio detached
        // (e.g. the space bar): playing is never silent, so the mute ends here (nothing left to
        // resume or unmute on the pending commit/endSkim).
        m_isScrubMuted = false;
        m_skimMuted = false;
        m_wasPlayingBeforeScrub = false;
        m_consumer->set("audio_off", 0);
        emit stateChanged();
    }
    Mlt::Producer *tractor = m_projection->tractor();
    if (rate > 0.0 && m_position >= m_duration - 1) {
        setPosition(0);
    } else if (rate < 0.0 && m_position <= 0) {
        setPosition(m_duration - 1);
    }
    qCDebug(lcTimelinePlayer) << "rate" << rate << "at" << m_position;
    if (rate != 0.0) {
        markInteraction(); // before the seek below: the frames read ahead get the size of playback
    } else {
        m_quietTimer.start(); // paused: the still frame at the full size soon (Auto)
    }
    tractor->set_speed(rate);
    // Stay on the playhead, not on the frame the read-ahead reached. The purge is also required when starting:
    // speed-0 frames already read ahead would use up the refresh and stall sdl2_audio (see Player::play()).
    tractor->seek(m_position);
    m_consumer->purge();
    m_consumer->set("refresh", 1);
    // The purge above discarded any requested frame still in flight: don't wait for one that will never arrive.
    m_renderInFlight = false;
    m_pendingFrame.reset();
    m_renderWatchdog.stop();
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

void TimelinePlayer::setScrubMuted(bool muted)
{
    if (m_isScrubMuted == muted) {
        return;
    }
    m_isScrubMuted = muted;
    if (muted) {
        // Remember whether the timeline was playing: the drag pauses it, the commit resumes it.
        m_wasPlayingBeforeScrub = playing();
    }
    if (m_consumer) {
        // MLT level, not a Qt volume dip: "audio_off" is sdl2_audio's own switch, checked in its
        // consumer_play_audio() (the audio of every frame shown, refresh/speed-0 ones included,
        // is skipped entirely while it is set).
        m_consumer->set("audio_off", muted ? 1 : 0);
    }
    emit stateChanged(); // the scrubMuted property changed
}

void TimelinePlayer::scrubSeek(int frame)
{
    const int target = std::clamp(frame, 0, std::max(0, m_duration - 1));
    setPosition(target);
    // A drag takes over from hover skimming: stop it so the skim hairline doesn't stick on the playhead
    // (the skim's silence stays: the drag is a preview-only seek too, and it mutes on its own).
    if (m_skimming) {
        m_skimming = false;
        emit stateChanged();
    }
    requestFrame(target);
}

void TimelinePlayer::commitSeek(int frame)
{
    const int target = std::clamp(frame, 0, std::max(0, m_duration - 1));
    const bool wasSkimming = m_skimming;
    m_skimming = false;
    // Already on the requested frame and nothing still rendering: nothing to render again (the debounce of a drag
    // commits the position the drag has already shown).
    if (!wasSkimming && target == m_position && target == m_shownPosition && !m_renderInFlight && !m_pendingFrame) {
        restoreAudioAfterScrub();
        resumeAfterScrub();
        return;
    }
    setPosition(target);
    // The commit is where the audio detached for the drag comes back (before showing the target frame, so its
    // chunk is heard only once, like a normal seek).
    restoreAudioAfterScrub();
    requestFrame(target);
    if (wasSkimming) {
        emit stateChanged();
    }
    resumeAfterScrub();
}

void TimelinePlayer::restoreAudioAfterScrub()
{
    if (!m_isScrubMuted && !m_skimMuted) {
        return;
    }
    m_isScrubMuted = false;
    m_skimMuted = false;
    if (m_consumer) {
        m_consumer->set("audio_off", 0);
    }
    emit stateChanged(); // the scrubMuted property changed
}

void TimelinePlayer::startSkimMute()
{
    if (m_skimMuted || m_isScrubMuted) {
        return; // already silent: a scrub drag holds the audio detached
    }
    m_skimMuted = true;
    if (m_consumer) {
        m_consumer->set("audio_off", 1);
    }
}

void TimelinePlayer::stopSkimMute()
{
    if (!m_skimMuted) {
        return;
    }
    m_skimMuted = false;
    if (m_consumer) {
        // A scrub drag that has taken over keeps the audio detached until its commit.
        m_consumer->set("audio_off", m_isScrubMuted ? 1 : 0);
    }
}

void TimelinePlayer::resumeAfterScrub()
{
    // The drag had paused the timeline: the commit resumes it where it was, whichever path
    // the landing frame took (a fresh seek or an already-settled one).
    if (!m_wasPlayingBeforeScrub) {
        return;
    }
    m_wasPlayingBeforeScrub = false;
    play();
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
    // Hover axis and trim handles: like a scrub, one render at a time and the latest position wins (it is kept, not
    // dropped: the preview ends on the frame under the pointer).
    const int target = std::clamp(frame, 0, std::max(0, m_duration - 1));
    if (!m_skimming) {
        m_skimming = true;
        // The hover preview is silent like the scrub drag: every frame it shows would be
        // heard otherwise (same sdl2_audio behaviour, same "audio_off" switch).
        startSkimMute();
        emit stateChanged();
    }
    m_skimTarget = target;
    requestFrame(target);
}

void TimelinePlayer::endSkim()
{
    if (!m_skimming) {
        return;
    }
    m_skimming = false;
    emit stateChanged();
    if (!playing()) {
        requestFrame(m_position);
    }
    // After the frame of the playhead: the skim is over, its silence is too (a scrub drag
    // that has taken over in the meantime keeps its own mute, see stopSkimMute).
    stopSkimMute();
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
    // MLT consumer thread. Cost of the copy: ~1 ms per frame at 1080p (D-19). Frames shown while paused are rendered
    // here (not by the read-ahead): at the preview size.
    const QImage image = copyFrameImage(frame, m_renderWidth.load(std::memory_order_relaxed),
                                        m_renderHeight.load(std::memory_order_relaxed));
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
    if (position != m_shownPosition) {
        m_shownPosition = position;
        emit shownPositionChanged();
    }
    if (!playing() && m_renderInFlight) {
        // The requested frame (or the one rendering when it was requested): the latest request goes now.
        adaptQuality();
        m_renderInFlight = false;
        m_renderWatchdog.stop();
        if (m_pendingFrame) {
            dispatchFrame(*m_pendingFrame);
        }
        return;
    }
    if (!playing()) {
        m_lastPlayedPosition = -1;
        return;
    }
    // Frames the consumer skipped because it was late (real_time = 1): gaps between two frames shown.
    if (m_lastPlayedPosition >= 0) {
        const int step = std::abs(position - m_lastPlayedPosition);
        const int expected = std::max(1, static_cast<int>(std::lround(std::abs(m_rate))));
        if (step > expected && step < 4 * m_frameRate) {
            m_skippedFrames += static_cast<quint64>((step - expected) / expected);
        }
    }
    m_lastPlayedPosition = position;
    adaptQuality();
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

} // namespace velacut::engine
