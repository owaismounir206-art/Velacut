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
#include <memory>
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

// FFmpeg's muxer for a format (a GIF is first an MP4, see render()).
const char *containerOf(ExportFormat format)
{
    switch (format) {
    case ExportFormat::Mp4:
    case ExportFormat::Gif:
        return "mp4";
    case ExportFormat::Mov:
        return "mov";
    case ExportFormat::WebM:
        return "webm";
    case ExportFormat::Images:
        return "image2";
    case ExportFormat::Mp3:
        return "mp3";
    case ExportFormat::Wav:
        return "wav";
    case ExportFormat::M4a:
        return "ipod";
    case ExportFormat::Flac:
        return "flac";
    }
    return "mp4";
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

    // The encoder writes a hidden file in the destination folder, renamed when complete (same filesystem). Pictures
    // go in a hidden folder, renamed the same way.
    const QFileInfo output(settings.outputPath);
    const bool pictures = settings.format == ExportFormat::Images;
    const bool gif = settings.format == ExportFormat::Gif;
    const QString partial = output.absolutePath() + u"/."_s + (pictures ? output.fileName() : output.completeBaseName()) +
                            u".part-"_s + QUuid::createUuid().toString(QUuid::Id128).left(8) +
                            (pictures ? QString() : u"."_s + output.suffix());
    if (pictures) {
        if (!QDir().mkpath(partial)) {
            return failure(RenderError::OutputNotWritable, partial);
        }
    } else {
        QFile probe(partial);
        if (!probe.open(QIODevice::WriteOnly)) {
            return failure(RenderError::OutputNotWritable, probe.errorString());
        }
    }
    // What the consumer writes: the file, the pictures' pattern, or for a GIF the video it is made from.
    const QString encoded = pictures ? partial + u'/' + output.fileName() + u"_%05d.png"_s
                          : gif ? partial + u".source.mp4"_s : partial;
    const auto removePartial = [&] {
        if (pictures) {
            QDir(partial).removeRecursively();
        } else {
            QFile::remove(partial);
        }
        if (gif) {
            QFile::remove(encoded);
        }
    };

    Result result;
    int total = 0;
    bool cancelled = false;
    {
        auto profile = makeProfile(VideoFormat{settings.size, settings.frameRate});
        MediaProducerCache cache(*profile);
        TimelineProjection projection(*profile, cache, TimelineProjection::MediaLoading::Wait);
        projection.build(project, sequenceId);
        result.warnings = projection.warnings();
        // The part between the In and Out points, if asked (SPEC §5.15).
        int first = 0;
        total = projection.duration();
        if (!settings.range.isEmpty()) {
            first = static_cast<int>(std::clamp<std::int64_t>(
                settings.range.start.rescaled(settings.frameRate, Rounding::NearestEven).value(), 0, std::max(0, total - 1)));
            const int length = static_cast<int>(settings.range.duration.rescaled(settings.frameRate, Rounding::NearestEven).value());
            total = std::clamp(length, 1, total - first);
        }
        std::unique_ptr<Mlt::Producer> part(projection.tractor()->cut(first, first + total - 1));

        const RationalTime duration(total, settings.frameRate);
        EncoderPlan plan = planEncoder(settings, hardwareEncoders, duration);
        int reached = -1;
        QString failureDetail;

        // One encode attempt: builds the consumer from the plan and pumps it. On success the frames are all in
        // `partial`; on failure the partial file is removed and `failureDetail` says where it stopped.
        const auto attempt = [&](const EncoderPlan &planToRun) -> std::optional<RenderError> {
            MltRuntime::clearLastError();
            Mlt::Consumer consumer(*profile, "avformat", QFile::encodeName(encoded).constData());
            consumer.set("f", containerOf(settings.format));
            if (settings.format == ExportFormat::Mp4 || settings.format == ExportFormat::Mov || gif) {
                consumer.set("movflags", "+faststart");
            }
            if (planToRun.vcodec.isEmpty()) {
                consumer.set("vn", 1);
            } else {
                consumer.set("vcodec", planToRun.vcodec.constData());
                if (!planToRun.hardware && !planToRun.pixelFormat.isEmpty()) {
                    consumer.set("pix_fmt", planToRun.pixelFormat.constData());
                }
                if (!planToRun.qualityOption.isEmpty()) {
                    consumer.set(planToRun.qualityOption.constData(), planToRun.qualityValue);
                }
                if (!planToRun.preset.isEmpty()) {
                    consumer.set("preset", planToRun.preset.constData());
                }
                if (planToRun.videoBitrate > 0) {
                    consumer.set("vb", kbps(planToRun.videoBitrate).constData());
                    if (planToRun.twoPass) {
                        consumer.set("v2pass", 1);
                    }
                }
                if (planToRun.videoMaxBitrate > 0) {
                    consumer.set("maxrate", kbps(planToRun.videoMaxBitrate).constData());
                    consumer.set("bufsize", kbps(planToRun.videoMaxBitrate * 2).constData());
                }
                for (const auto &[key, value] : planToRun.options) {
                    consumer.set(key.constData(), value.constData());
                }
                // A keyframe every 2 s: quick seeking in players and editors.
                if (!pictures) {
                    consumer.set("g", std::max(1, static_cast<int>(std::lround(2.0 * settings.frameRate.toDouble()))));
                }
            }
            if (planToRun.acodec.isEmpty()) {
                consumer.set("an", 1);
            } else {
                consumer.set("acodec", planToRun.acodec.constData());
                if (planToRun.acodec != "pcm_s16le" && planToRun.acodec != "flac") {
                    consumer.set("ab", kbps(planToRun.audioBitrate).constData());
                }
                consumer.set("ar", 48000);
                consumer.set("ac", 2);
            }
            consumer.set("threads", 0);
            // Every frame is rendered (no dropping) by one read-ahead thread; see D-24 for why not worker threads.
            consumer.set("real_time", -1);
            consumer.set("terminate_on_pause", 1);
            consumer.connect(*part);
            part->set_speed(1.0);
            part->seek(0);
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
            const qint64 size = pictures ? static_cast<qint64>(QDir(partial).entryList(QDir::Files).size())
                                         : QFileInfo(encoded).size();
            if (size <= 0 || reached < total - 1) {
                removePartial();
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
                removePartial();
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
                        removePartial();
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

    // A GIF from the video just made: its own palette of 256 colours, then the picture dithered on it (one pass each).
    if (gif) {
        QProcess process;
        process.start(u"ffmpeg"_s, {u"-hide_banner"_s, u"-loglevel"_s, u"error"_s, u"-y"_s, u"-i"_s, encoded, u"-vf"_s,
                                     u"split[a][b];[a]palettegen=stats_mode=diff[p];[b][p]paletteuse=dither=bayer:bayer_scale=4:diff_mode=rectangle"_s,
                                     u"-loop"_s, u"0"_s, u"-f"_s, u"gif"_s, partial});
        const bool made = process.waitForFinished(-1) && process.exitStatus() == QProcess::NormalExit
                          && process.exitCode() == 0 && QFileInfo(partial).size() > 0;
        QFile::remove(encoded);
        if (!made) {
            const QString detail = QString::fromLocal8Bit(process.readAllStandardError()).trimmed();
            QFile::remove(partial);
            return failure(RenderError::EncoderFailed, u"gif: "_s + detail);
        }
    }
    if (settings.normalizeLoudness && hasAudio(settings.format)) {
        const auto stats = extractLoudness(partial);
        if (stats && std::isfinite(stats->integratedLufs) && stats->integratedLufs > -70.0) {
            const double gainDb = fx::gainAdjustmentForTargetLufs(stats->integratedLufs, settings.targetLufs);
            if (std::abs(gainDb) >= 0.1) {
                const QString adjustedPartial = partial + u".lufs."_s + output.suffix();
                const EncoderPlan audio = planEncoder(settings, {}, RationalTime(total, settings.frameRate));
                QStringList args;
                args << u"-y"_s << u"-i"_s << partial << u"-c:v"_s << u"copy"_s
                     << u"-af"_s << QString::asprintf("volume=%.2fdB", gainDb)
                     << u"-c:a"_s << QString::fromLatin1(audio.acodec);
                if (audio.acodec != "pcm_s16le" && audio.acodec != "flac") {
                    args << u"-b:a"_s << QString::fromLatin1(kbps(audio.audioBitrate));
                }
                args << u"-f"_s << QString::fromLatin1(containerOf(settings.format)) << adjustedPartial;
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
    // The cover inside the file (SPEC §5.13ter): an MJPEG picture stream marked "attached_pic", the video and audio
    // copied as they are. A failure leaves the video without a cover, with a warning.
    const QString suffix = output.suffix().toLower();
    if (!settings.coverImage.isEmpty() && QFileInfo::exists(settings.coverImage)
        && (suffix == u"mp4"_s || suffix == u"mov"_s || suffix == u"m4v"_s)) {
        const QString covered = partial + u".cover."_s + suffix;
        QProcess process;
        process.start(u"ffmpeg"_s, {u"-hide_banner"_s, u"-loglevel"_s, u"error"_s, u"-y"_s, u"-i"_s, partial, u"-i"_s,
                                     settings.coverImage, u"-map"_s, u"0"_s, u"-map"_s, u"1"_s, u"-c"_s, u"copy"_s,
                                     u"-c:v:1"_s, u"mjpeg"_s, u"-disposition:v:1"_s, u"attached_pic"_s, u"-f"_s,
                                     suffix == u"mov"_s ? u"mov"_s : u"mp4"_s, covered});
        if (process.waitForFinished(60000) && process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0) {
            QFile::remove(partial);
            QFile::rename(covered, partial);
        } else {
            QFile::remove(covered);
            result.warnings << u"cover: %1"_s.arg(QString::fromLocal8Bit(process.readAllStandardError()).trimmed());
        }
    }
    std::error_code error;
    std::filesystem::rename(QFile::encodeName(partial).toStdString(), QFile::encodeName(output.absoluteFilePath()).toStdString(),
                            error);
    if (error) {
        removePartial();
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
