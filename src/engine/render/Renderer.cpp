// SPDX-License-Identifier: GPL-3.0-or-later
#include "Renderer.h"

#include "engine/mlt/MltRuntime.h"
#include "engine/timeline/MediaProducerCache.h"
#include "engine/timeline/TimelineProjection.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>
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
                                  const Progress &progress, const std::atomic<bool> &cancel)
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
    int reached = -1;
    bool cancelled = false;
    {
        auto profile = makeProfile(VideoFormat{settings.size, settings.frameRate});
        MediaProducerCache cache(*profile);
        TimelineProjection projection(*profile, cache, TimelineProjection::MediaLoading::Wait);
        projection.build(project, sequenceId);
        result.warnings = projection.warnings();
        total = projection.duration();

        const EncoderParameters encoder = encoderParameters(settings);
        MltRuntime::clearLastError();
        Mlt::Consumer consumer(*profile, "avformat", QFile::encodeName(partial).constData());
        consumer.set("f", "mp4");
        consumer.set("movflags", "+faststart");
        consumer.set("vcodec", "libx264");
        consumer.set("pix_fmt", "yuv420p");
        consumer.set("crf", encoder.crf);
        consumer.set("preset", encoder.preset.constData());
        consumer.set("maxrate", kbps(encoder.videoMaxBitrate).constData());
        consumer.set("bufsize", kbps(encoder.videoMaxBitrate * 2).constData());
        // A keyframe every 2 s: quick seeking in players and editors.
        consumer.set("g", std::max(1, static_cast<int>(std::lround(2.0 * settings.frameRate.toDouble()))));
        consumer.set("acodec", "aac");
        consumer.set("ab", kbps(encoder.audioBitrate).constData());
        consumer.set("ar", 48000);
        consumer.set("ac", 2);
        consumer.set("threads", 0);
        // Every frame is rendered (no dropping) by one read-ahead thread; see D-24 for why not worker threads.
        consumer.set("real_time", -1);
        consumer.set("terminate_on_pause", 1);
        consumer.connect(*projection.tractor());
        projection.tractor()->set_speed(1.0);
        projection.tractor()->seek(0);
        qCInfo(lcRender) << "exporting" << total << "frames" << settings.size << settings.frameRate.toString() << "to"
                         << settings.outputPath;
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
        // The consumer, then the graph, then the producers are released here, before the profile.
    }

    if (cancelled) {
        QFile::remove(partial);
        result.status = Status::Cancelled;
        return result;
    }
    const qint64 size = QFileInfo(partial).size();
    if (size <= 0 || reached < total - 1) {
        QFile::remove(partial);
        const QString mltError = MltRuntime::lastError();
        result.status = Status::Failed;
        result.error = RenderError::EncoderFailed;
        result.detail = u"stopped at frame %1 of %2, %3 bytes written%4"_s.arg(reached + 1)
                            .arg(total)
                            .arg(size)
                            .arg(mltError.isEmpty() ? QString() : u": "_s + mltError);
        return result;
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
