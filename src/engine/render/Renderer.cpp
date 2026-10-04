// SPDX-License-Identifier: GPL-3.0-or-later
#include "Renderer.h"

#include "engine/analysis/Decoding.h"
#include "engine/mlt/MltRuntime.h"
#include "engine/timeline/MediaProducerCache.h"
#include "engine/timeline/TimelineProjection.h"
#include "fx/Loudness.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QProcess>
#include <QUuid>

#include <mlt++/Mlt.h>

#include <chrono>
#include <filesystem>
#include <thread>

Q_LOGGING_CATEGORY(lcRender, "vedit.engine.render")

using namespace Qt::StringLiterals;

namespace vedit::engine {

namespace {

Renderer::Result failure(RenderError error, const QString &detail = {})
{
    Renderer::Result result;
    result.status = Renderer::Status::Failed;
    result.error = error;
    result.detail = detail;
    return result;
}

bool hasClips(const Sequence &sequence)
{
    for (const auto *tracks : {&sequence.visualTracks, &sequence.audioTracks}) {
        for (const Track &track : *tracks) {
            if (!track.clips.empty()) {
                return true;
            }
        }
    }
    return false;
}

QByteArray kbps(qint64 bitsPerSecond)
{
    return QByteArray::number((bitsPerSecond + 500) / 1000) + "k";
}

} // namespace

Renderer::Result Renderer::render(const ProjectData &project, const SequenceId &sequenceId, const ExportSettings &settings,
                                  const Progress &progress, const std::atomic<bool> &cancel,
                                  const QStringList &hardwareEncoders)
{
    if (!MltRuntime::waitUntilReady()) {
        return failure(RenderError::MltUnavailable);
    }
    const Sequence *sequence = project.findSequence(sequenceId);
    if (!sequence) {
        return failure(RenderError::SequenceMissing, sequenceId.toString());
    }
    if (!hasClips(*sequence)) {
        return failure(RenderError::NothingToExport);
    }

    // The encoder writes a hidden file in the destination folder, renamed when complete (same filesystem).
    const QFileInfo output(settings.outputPath);
    const QString partial = output.absolutePath() + u"/."_s + output.completeBaseName() + u".part-"_s +
                            QUuid::createUuid().toString(QUuid::Id128).left(8) + u"."_s + output.suffix();
    {
        QFile probe(partial);
        if (!probe.open(QIODevice::WriteOnly)) {
            return failure(RenderError::OutputNotWritable, probe.errorString());
        }
    }

    Result result;
    int total = 0;
    bool cancelled = false;
    {
        auto profile = makeProfile(VideoFormat{settings.size, settings.frameRate});
        MediaProducerCache cache(*profile);
        TimelineProjection projection(*profile, cache, TimelineProjection::MediaLoading::Wait);
        projection.build(project, sequenceId);
        result.warnings = projection.warnings();
        total = projection.duration();

        const RationalTime duration(total, settings.frameRate);
        EncoderPlan plan = planEncoder(settings, hardwareEncoders, duration);
        int reached = -1;
        QString failureDetail;

        // One encode attempt: builds the consumer from the plan and pumps it. On success the frames are all in
        // `partial`; on failure the partial file is removed and `failureDetail` says where it stopped.
        const auto attempt = [&](const EncoderPlan &planToRun) -> std::optional<RenderError> {
            MltRuntime::clearLastError();
            Mlt::Consumer consumer(*profile, "avformat", QFile::encodeName(partial).constData());
            consumer.set("f", "mp4");
            consumer.set("movflags", "+faststart");
            consumer.set("vcodec", planToRun.vcodec.constData());
            if (!planToRun.hardware) {
                consumer.set("pix_fmt", "yuv420p");
            }
            consumer.set(planToRun.qualityOption.constData(), planToRun.qualityValue);
            if (!planToRun.preset.isEmpty()) {
                consumer.set("preset", planToRun.preset.constData());
            }
            if (planToRun.videoBitrate > 0) {
                consumer.set("vb", kbps(planToRun.videoBitrate).constData());
                if (planToRun.twoPass) {
                    consumer.set("v2pass", 1);
                }
            }
            consumer.set("maxrate", kbps(planToRun.videoMaxBitrate).constData());
            consumer.set("bufsize", kbps(planToRun.videoMaxBitrate * 2).constData());
            // A keyframe every 2 s: quick seeking in players and editors.
            consumer.set("g", std::max(1, static_cast<int>(std::lround(2.0 * settings.frameRate.toDouble()))));
            consumer.set("acodec", "aac");
            consumer.set("ab", kbps(planToRun.audioBitrate).constData());
            consumer.set("ar", 48000);
            consumer.set("ac", 2);
            consumer.set("threads", 0);
            // Every frame is rendered (no dropping) by one read-ahead thread; see D-24 for why not worker threads.
            consumer.set("real_time", -1);
            consumer.set("terminate_on_pause", 1);
            consumer.connect(*projection.tractor());
            projection.tractor()->set_speed(1.0);
            projection.tractor()->seek(0);
            reached = -1;
            qCInfo(lcRender) << "exporting" << total << "frames" << settings.size << settings.frameRate.toString()
                             << "with" << planToRun.vcodec.constData() << "to" << settings.outputPath;
            consumer.start();
            while (!consumer.is_stopped()) {
                if (cancel.load()) {
                    cancelled = true;
                    consumer.stop();
                    break;
                }
                reached = std::max(reached, consumer.position());
                if (progress) {
                    progress(std::clamp(reached + 1, 0, total), total);
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            reached = std::max(reached, consumer.position());
            consumer.stop();
            const qint64 size = QFileInfo(partial).size();
            if (size <= 0 || reached < total - 1) {
                QFile::remove(partial);
                const QString mltError = MltRuntime::lastError();
                failureDetail = u"stopped at frame %1 of %2 with %3 (encoder %4)%5"_s.arg(reached + 1)
                                    .arg(total)
                                    .arg(size)
                                    .arg(QLatin1StringView(planToRun.vcodec))
                                    .arg(mltError.isEmpty() ? QString() : u": "_s + mltError);
                return RenderError::EncoderFailed;
            }
            return std::nullopt;
            // The consumer, then the graph, then the producers are released here, before the profile.
        };

        if (attempt(plan)) {
            if (cancelled) {
                result.status = Status::Cancelled;
                return result;
            }
            if (plan.hardware) {
                // Runtime fallback (SPEC 1bis rule 4): a GPU encoder that fails never fails the export; restart
                // in software and tell the user in the warnings.
                qCWarning(lcRender) << "hardware encoder" << plan.vcodec.constData()
                                    << "failed, falling back to software:" << failureDetail;
                result.warnings << u"The GPU encoder stopped working: the video was exported with software encoding."_s;
                ExportSettings softwareSettings = settings;
                softwareSettings.hardwareEncoder = HardwareEncoder::Off;
                plan = planEncoder(softwareSettings, {}, duration);
                if (attempt(plan)) {
                    if (cancelled) {
                        result.status = Status::Cancelled;
                        return result;
                    }
                    result.status = Status::Failed;
                    result.error = RenderError::EncoderFailed;
                    result.detail = failureDetail;
                    return result;
                }
            } else {
                result.status = Status::Failed;
                result.error = RenderError::EncoderFailed;
                result.detail = failureDetail;
                return result;
            }
        }
    }

    if (settings.normalizeLoudness) {
        const auto stats = extractLoudness(partial);
        if (stats && std::isfinite(stats->integratedLufs) && stats->integratedLufs > -70.0) {
            const double gainDb = fx::gainAdjustmentForTargetLufs(stats->integratedLufs, settings.targetLufs);
            if (std::abs(gainDb) >= 0.1) {
                const QString adjustedPartial = partial + u".lufs.mp4"_s;
                QStringList args;
                args << u"-y"_s << u"-i"_s << partial << u"-c:v"_s << u"copy"_s
                     << u"-af"_s << QString::asprintf("volume=%.2fdB", gainDb)
                     << u"-c:a"_s << u"aac"_s << u"-b:a"_s << u"192k"_s << adjustedPartial;
                QProcess process;
                process.start(u"ffmpeg"_s, args);
                if (process.waitForFinished(60000) && process.exitCode() == 0) {
                    QFile::remove(partial);
                    QFile::rename(adjustedPartial, partial);
                } else {
                    QFile::remove(adjustedPartial);
                }
            }
        }
    }
    std::error_code error;
    std::filesystem::rename(QFile::encodeName(partial).toStdString(), QFile::encodeName(output.absoluteFilePath()).toStdString(),
                            error);
    if (error) {
        QFile::remove(partial);
        return failure(RenderError::OutputNotWritable, QString::fromStdString(error.message()));
    }
    if (progress) {
        progress(total, total);
    }
    result.status = Status::Done;
    return result;
}

Renderer::Result Renderer::renderReversed(const QString &inputPath, const QString &outputPath, const Progress &progress,
                                          const std::atomic<bool> &cancel)
{
    if (!MltRuntime::waitUntilReady()) {
        return failure(RenderError::MltUnavailable);
    }
    const QFileInfo output(outputPath);
    QDir().mkpath(output.absolutePath());
    const QString partial = output.absolutePath() + u"/."_s + output.completeBaseName() + u".part-"_s +
                            QUuid::createUuid().toString(QUuid::Id128).left(8) + u"."_s + output.suffix();
    int total = 0;
    int reached = -1;
    bool cancelled = false;
    {
        // The profile of the file itself (size capped at 720 p, frame rate kept).
        Mlt::Profile profile;
        {
            Mlt::Producer probe(profile, "avformat", QFile::encodeName(inputPath).constData());
            if (!probe.is_valid()) {
                return failure(RenderError::ProjectUnreadable, inputPath);
            }
            profile.from_producer(probe);
        }
        if (profile.height() > 720) {
            const int height = 720;
            const int width = std::max(2, static_cast<int>(std::lround(profile.width() * 720.0 / profile.height() / 2.0)) * 2);
            profile.set_width(width);
            profile.set_height(height);
        }
        profile.set_sample_aspect(1, 1);
        profile.set_explicit(1);
        const QByteArray resource = QByteArray("timewarp:-1.0:") + QFile::encodeName(inputPath);
        Mlt::Producer reversed(profile, resource.constData());
        if (!reversed.is_valid()) {
            return failure(RenderError::ProjectUnreadable, inputPath);
        }
        total = reversed.get_length();
        MltRuntime::clearLastError();
        Mlt::Consumer consumer(profile, "avformat", QFile::encodeName(partial).constData());
        consumer.set("f", "mp4");
        consumer.set("vcodec", "libx264");
        consumer.set("pix_fmt", "yuv420p");
        consumer.set("g", 1); // every frame a keyframe: instant seeks in any direction
        consumer.set("crf", 16);
        consumer.set("preset", "veryfast");
        consumer.set("acodec", "aac");
        consumer.set("ab", "192k");
        consumer.set("ar", 48000);
        consumer.set("ac", 2);
        consumer.set("threads", 0);
        consumer.set("real_time", -1);
        consumer.set("terminate_on_pause", 1);
        consumer.connect(reversed);
        reversed.set_speed(1.0);
        reversed.seek(0);
        consumer.start();
        while (!consumer.is_stopped()) {
            if (cancel.load()) {
                cancelled = true;
                consumer.stop();
                break;
            }
            reached = std::max(reached, consumer.position());
            if (progress) {
                progress(std::clamp(reached + 1, 0, total), total);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        reached = std::max(reached, consumer.position());
        consumer.stop();
    }
    Result result;
    if (cancelled) {
        QFile::remove(partial);
        result.status = Status::Cancelled;
        return result;
    }
    if (QFileInfo(partial).size() <= 0 || reached < total - 1) {
        QFile::remove(partial);
        return failure(RenderError::EncoderFailed, MltRuntime::lastError());
    }
    std::error_code error;
    std::filesystem::rename(QFile::encodeName(partial).toStdString(), QFile::encodeName(output.absoluteFilePath()).toStdString(),
                            error);
    if (error) {
        QFile::remove(partial);
        return failure(RenderError::OutputNotWritable, QString::fromStdString(error.message()));
    }
    result.status = Status::Done;
    return result;
}

} // namespace vedit::engine
