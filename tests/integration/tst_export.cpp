// SPDX-License-Identifier: GPL-3.0-or-later
// Export integration test: the MP4 written by the renderer is what the timeline shows (checked with ffprobe and
// by decoding frames back), at the project format or another size and frame rate; failures and cancellation
// leave no files behind.
#include "../unit/ProjectFixture.h"
#include "TestMedia.h"

#include "common/Paths.h"
#include "engine/mlt/MltRuntime.h"
#include "engine/render/RenderJob.h"
#include "engine/render/Renderer.h"
#include "engine/timeline/MediaProducerCache.h"
#include "engine/timeline/TimelineProjection.h"

#include <QSignalSpy>
#include <QTemporaryDir>

#include <mlt++/Mlt.h>

using namespace vedit;
using namespace vedit::engine;
using namespace vedit::test;
using namespace Qt::StringLiterals;

class TestExport : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;
    TestMediaFiles m_files;
    Media m_landscape;
    Media m_vertical;
    Media m_music;

    // Three clips cut and reordered on the main track (the Phase 1 criterion), music underneath.
    ProjectData editedProject()
    {
        ProjectData data = ProjectData::createEmpty(u"Export"_s);
        data.settings.frameRate = Rational(30);
        data.settings.defaultCanvas = Canvas{320, 180, CanvasPreset::Landscape16x9};
        data.sequences.front().canvas = data.settings.defaultCanvas;
        data.media = {m_landscape, m_vertical, m_music};
        Session session(data);
        bool ok = session.apply(session.editor().insertMedia(m_landscape.id, frames(0)));
        ok = ok && session.apply(session.editor().insertMedia(m_vertical.id, frames(120)));
        ok = ok && session.apply(session.editor().insertMedia(m_landscape.id, frames(240)));
        const auto clip = [&](int index) { return session.mainTrack().clips[index].id; };
        // Trims take the new position of the edge on the timeline; the magnetic track closes the gaps.
        ok = ok && session.apply(session.editor().trimClip(clip(0), ClipEdge::End, frames(45)));        // 45 frames
        ok = ok && session.apply(session.editor().trimClip(clip(1), ClipEdge::Start, frames(45 + 30)));  // 90 frames
        ok = ok && session.apply(session.editor().trimClip(clip(2), ClipEdge::Start, frames(135 + 60))); // 60 frames
        ok = ok && session.apply(session.editor().moveClip(clip(2), frames(0)));
        ok = ok && session.apply(session.editor().insertMedia(m_music.id, frames(0)));
        if (!ok) {
            qWarning("could not build the test project");
        }
        return session.data();
    }

    static ExportSettings settingsFor(const ProjectData &data, const QString &path)
    {
        const VideoFormat format = sequenceFormat(data, data.mainSequenceId);
        ExportSettings settings;
        settings.outputPath = path;
        settings.size = format.size;
        settings.frameRate = format.frameRate;
        return settings;
    }

    // A path in a fresh output folder.
    QString outputPath(const QString &name) const
    {
        QDir().mkpath(m_dir.filePath(u"out"_s));
        return m_dir.filePath(u"out/"_s + name);
    }

    static int mainTrackFrames(const ProjectData &data)
    {
        const Track &main = data.mainSequence()->visualTracks.front();
        return static_cast<int>(main.clips.back().end().value());
    }

private slots:
    void initTestCase()
    {
        m_files = generateTestMedia(m_dir.filePath(u"media"_s));
        if (!m_files.ok || QStandardPaths::findExecutable(u"ffprobe"_s).isEmpty()) {
            QSKIP("ffmpeg and ffprobe are needed");
        }
        QVERIFY(MltRuntime::waitUntilReady());
        m_landscape = testMedia(MediaKind::Video, m_files.landscape, RationalTime(120, Rational(30)), 320, 180, true);
        m_vertical = testMedia(MediaKind::Video, m_files.vertical, RationalTime(120, Rational(30)), 180, 320, true);
        m_music = testMedia(MediaKind::Audio, m_files.music, RationalTime(6 * 48000, Rational(48000)), 0, 0, true);
    }

    void exportsTheTimelineAsMp4()
    {
        const ProjectData data = editedProject();
        QCOMPARE(mainTrackFrames(data), 45 + 90 + 60);
        const QString path = outputPath(u"edited.mp4"_s);
        std::atomic<bool> cancel{false};
        QList<int> progress;
        const Renderer::Result result =
            Renderer::render(data, data.mainSequenceId, settingsFor(data, path),
                             [&](int frame, int total) { progress << frame * 1000 + total; }, cancel);
        QCOMPARE(result.status, Renderer::Status::Done);
        QVERIFY(result.warnings.isEmpty());
        QVERIFY(QDir(QFileInfo(path).absolutePath()).entryList({u".*part*"_s}, QDir::Files | QDir::Hidden).isEmpty());
        QVERIFY(!progress.isEmpty());
        QCOMPARE(progress.back(), 195 * 1000 + 195);
        QVERIFY(std::is_sorted(progress.begin(), progress.end()));

        const QJsonObject probe = ffprobe(path);
        const QJsonObject video = streamOfType(probe, u"video"_s);
        const QJsonObject audio = streamOfType(probe, u"audio"_s);
        QCOMPARE(video.value(u"codec_name"_s).toString(), u"h264"_s);
        QCOMPARE(video.value(u"pix_fmt"_s).toString(), u"yuv420p"_s);
        QCOMPARE(video.value(u"width"_s).toInt(), 320);
        QCOMPARE(video.value(u"height"_s).toInt(), 180);
        QCOMPARE(video.value(u"r_frame_rate"_s).toString(), u"30/1"_s);
        QCOMPARE(video.value(u"nb_read_frames"_s).toString().toInt(), 195);
        QCOMPARE(audio.value(u"codec_name"_s).toString(), u"aac"_s);
        QCOMPARE(audio.value(u"sample_rate"_s).toString(), u"48000"_s);
        QCOMPARE(audio.value(u"channels"_s).toInt(), 2);
        const double seconds = probe.value(u"format"_s).toObject().value(u"duration"_s).toString().toDouble();
        QVERIFY2(std::abs(seconds - 6.5) < 0.1, qPrintable(QString::number(seconds)));

        // Decoded frames match the projection (lossy encoding: small mean difference only).
        auto profile = makeProfile(data, data.mainSequenceId);
        MediaProducerCache cache(*profile);
        TimelineProjection projection(*profile, cache, TimelineProjection::MediaLoading::Wait);
        projection.build(data, data.mainSequenceId);
        for (int frame : {10, 50, 100, 170, 190}) {
            const double difference = meanDifference(decodeFrame(path, frame, m_dir.path()), projection.renderFrame(frame));
            QVERIFY2(difference >= 0 && difference < 6.0, qPrintable(u"frame %1: %2"_s.arg(frame).arg(difference)));
            // …and differ clearly from a neighbouring clip, so the check has teeth.
        }
        QVERIFY(meanDifference(decodeFrame(path, 10, m_dir.path()), projection.renderFrame(100)) > 20.0);
    }

    void exportsAnotherSizeAndFrameRate()
    {
        const ProjectData data = editedProject();
        const QString path = outputPath(u"small.mp4"_s);
        ExportSettings settings = settingsFor(data, path);
        settings.size = scaledToShortSide(settings.size, 90);
        settings.frameRate = Rational(25);
        settings.quality = ExportQuality::Low;
        QCOMPARE(settings.size, QSize(160, 90));
        std::atomic<bool> cancel{false};
        const Renderer::Result result = Renderer::render(data, data.mainSequenceId, settings, {}, cancel);
        QVERIFY2(result.status == Renderer::Status::Done, qPrintable(result.detail));
        const QJsonObject probe = ffprobe(path);
        const QJsonObject video = streamOfType(probe, u"video"_s);
        QCOMPARE(video.value(u"width"_s).toInt(), 160);
        QCOMPARE(video.value(u"height"_s).toInt(), 90);
        QCOMPARE(video.value(u"r_frame_rate"_s).toString(), u"25/1"_s);
        QCOMPARE(video.value(u"nb_read_frames"_s).toString().toInt(), 162); // 6.5 s = 162.5 frames, rounded to even
    }

    void nothingToExport()
    {
        ProjectData data = ProjectData::createEmpty(u"Empty"_s);
        std::atomic<bool> cancel{false};
        const QString path = outputPath(u"empty.mp4"_s);
        const Renderer::Result result = Renderer::render(data, data.mainSequenceId, settingsFor(data, path), {}, cancel);
        QCOMPARE(result.status, Renderer::Status::Failed);
        QCOMPARE(result.error, RenderError::NothingToExport);
        QVERIFY(!QFileInfo::exists(path));
    }

    void unwritableDestination()
    {
        const ProjectData data = editedProject();
        std::atomic<bool> cancel{false};
        const QString path = m_dir.filePath(u"missing-folder/x.mp4"_s);
        const Renderer::Result result = Renderer::render(data, data.mainSequenceId, settingsFor(data, path), {}, cancel);
        QCOMPARE(result.status, Renderer::Status::Failed);
        QCOMPARE(result.error, RenderError::OutputNotWritable);
        QVERIFY(!result.detail.isEmpty());
    }

    void cancelLeavesNoFiles()
    {
        const ProjectData data = editedProject();
        const QString folder = m_dir.filePath(u"cancel"_s);
        QDir().mkpath(folder);
        std::atomic<bool> cancel{false};
        const Renderer::Result result = Renderer::render(data, data.mainSequenceId, settingsFor(data, folder + u"/c.mp4"_s),
                                                         [&](int, int) { cancel = true; }, cancel);
        QCOMPARE(result.status, Renderer::Status::Cancelled);
        QVERIFY(QDir(folder).entryList(QDir::Files | QDir::Hidden).isEmpty());
    }

    // The editor's path: a frozen copy rendered by the vedit-render process.
    void exportsInASeparateProcess()
    {
        const ProjectData data = editedProject();
        const QString path = outputPath(u"process.mp4"_s);
        RenderJob job;
        job.setExecutable(QStringLiteral(VEDIT_RENDER_EXECUTABLE));
        QSignalSpy finished(&job, &RenderJob::finished);
        QSignalSpy failed(&job, &RenderJob::failed);
        QList<double> progress;
        connect(&job, &RenderJob::progressChanged, this, [&] { progress << job.progress(); });
        QVERIFY(job.start(data, data.mainSequenceId, settingsFor(data, path)));
        QVERIFY(job.running());
        QVERIFY(finished.wait(60000));
        QCOMPARE(failed.count(), 0);
        QCOMPARE(finished.first().at(0).toString(), path);
        QVERIFY(!job.running());
        QVERIFY(std::is_sorted(progress.begin(), progress.end()));
        QVERIFY(progress.contains(1.0));
        QCOMPARE(streamOfType(ffprobe(path), u"video"_s).value(u"nb_read_frames"_s).toString().toInt(), 195);
        // The frozen copy and the job file are removed.
        QVERIFY(QDir(paths::cacheDir() + u"/render"_s).entryList(QDir::Files).isEmpty());
    }

    // Phase 2 content through the separate process: text (QPainter on the offscreen platform), a filter, a transition.
    void exportsTextFiltersAndTransitions()
    {
        ProjectData data = editedProject();
        Session session(data);
        TextClipData text;
        text.text = u"HELLO"_s;
        text.style.size = Param(0.2);
        text.style.color = Param(Color{255, 255, 0, 255});
        text.style.stroke = TextStroke{Param(Color{0, 0, 0, 255}), 0.1};
        QVERIFY(session.apply(session.editor().insertText(frames(0), text, frames(60))));
        Effect filter;
        filter.id = EffectId::create();
        filter.type = u"vedit.filter"_s;
        filter.preset = AssetRef{u"vedit.core"_s, u"filters/bw"_s, 1};
        const ClipId second = session.mainTrack().clips[1].id;
        QVERIFY(session.apply(session.editor().updateClips({second}, [&](Clip &c) { c.effects.push_back(filter); }, u"bw"_s)));
        QVERIFY(session.apply(session.editor().addTransition(session.mainTrack().clips[0].id,
                                                             AssetRef{u"vedit.core"_s, u"transitions/dissolve"_s, 1}, frames(10))));
        data = session.data();
        const QString path = outputPath(u"phase2.mp4"_s);
        RenderJob job;
        job.setExecutable(QStringLiteral(VEDIT_RENDER_EXECUTABLE));
        QSignalSpy finished(&job, &RenderJob::finished);
        QSignalSpy failed(&job, &RenderJob::failed);
        QVERIFY(job.start(data, data.mainSequenceId, settingsFor(data, path)));
        QVERIFY2(finished.wait(60000), failed.isEmpty() ? "timeout" : qPrintable(failed.first().at(1).toString()));
        QVERIFY(finished.first().at(1).toStringList().isEmpty()); // no warnings: everything rendered
        auto profile = makeProfile(data, data.mainSequenceId);
        MediaProducerCache cache(*profile);
        TimelineProjection projection(*profile, cache, TimelineProjection::MediaLoading::Wait);
        projection.build(data, data.mainSequenceId);
        for (int frame : {20, 88, 100}) { // text over clip 1, inside the transition, the black and white clip
            const double difference = meanDifference(decodeFrame(path, frame, m_dir.path()), projection.renderFrame(frame));
            QVERIFY2(difference >= 0 && difference < 6.0, qPrintable(u"frame %1: %2"_s.arg(frame).arg(difference)));
        }
    }

    void processCancelLeavesNoFiles()
    {
        // Long enough to be cancelled while running: the same clips at a large size.
        const ProjectData data = editedProject();
        const QString folder = m_dir.filePath(u"process-cancel"_s);
        QDir().mkpath(folder);
        ExportSettings settings = settingsFor(data, folder + u"/c.mp4"_s);
        settings.size = QSize(1920, 1080);
        RenderJob job;
        job.setExecutable(QStringLiteral(VEDIT_RENDER_EXECUTABLE));
        QSignalSpy cancelled(&job, &RenderJob::cancelled);
        QVERIFY(job.start(data, data.mainSequenceId, settings));
        QTRY_VERIFY_WITH_TIMEOUT(job.progress() > 0.0, 30000);
        job.cancel();
        QVERIFY(cancelled.wait(30000));
        QVERIFY(QDir(folder).entryList(QDir::Files | QDir::Hidden).isEmpty());
    }

    void processReportsTranslatableErrors()
    {
        const ProjectData data = editedProject();
        RenderJob job;
        job.setExecutable(QStringLiteral(VEDIT_RENDER_EXECUTABLE));
        QSignalSpy failed(&job, &RenderJob::failed);
        QVERIFY(job.start(data, data.mainSequenceId, settingsFor(data, m_dir.filePath(u"no-such-folder/x.mp4"_s))));
        QVERIFY(failed.wait(30000));
        QCOMPARE(failed.first().at(0).toString(), RenderJob::errorMessage(RenderError::OutputNotWritable));
        // Checked before starting: an empty timeline.
        ProjectData empty = ProjectData::createEmpty(u"Empty"_s);
        QVERIFY(!job.start(empty, empty.mainSequenceId, settingsFor(empty, outputPath(u"e.mp4"_s))));
        QCOMPARE(failed.last().at(0).toString(), RenderJob::errorMessage(RenderError::NothingToExport));
    }

    // Job files of an export killed with vedit are removed at the next start; recent ones are left alone.
    void staleJobFilesAreRemoved()
    {
        const QString directory = paths::cacheDir() + u"/render"_s;
        QDir().mkpath(directory);
        const auto make = [&directory](const QString &name, QDateTime modified) {
            QFile file(directory + u"/"_s + name);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("{}");
            file.close();
            QVERIFY(file.open(QIODevice::ReadWrite));
            QVERIFY(file.setFileTime(modified, QFileDevice::FileModificationTime));
        };
        make(u"old.json"_s, QDateTime::currentDateTime().addSecs(-3600));
        make(u"old.vproj"_s, QDateTime::currentDateTime().addSecs(-3600));
        make(u"new.json"_s, QDateTime::currentDateTime());
        QCOMPARE(engine::RenderJob::removeStaleJobFiles(), 2);
        QVERIFY(!QFile::exists(directory + u"/old.json"_s));
        QVERIFY(QFile::exists(directory + u"/new.json"_s));
        QVERIFY(QFile::remove(directory + u"/new.json"_s));
    }

    void settingsRoundTripAndEstimates()
    {
        ExportSettings settings;
        settings.outputPath = u"/videos/a b.mp4"_s;
        settings.size = QSize(1080, 1920);
        settings.frameRate = Rational(30000, 1001);
        settings.quality = ExportQuality::High;
        QCOMPARE(ExportSettings::fromJson(settings.toJson()), settings);
        settings.videoCodec = VideoCodec::AV1;
        settings.hardwareEncoder = HardwareEncoder::Off;
        settings.maxFileSizeMB = 25;
        QCOMPARE(ExportSettings::fromJson(settings.toJson()), settings);
        QVERIFY(!ExportSettings::fromJson(QJsonObject{}));
        QCOMPARE(scaledToShortSide(QSize(1080, 1920), 720), QSize(720, 1280));
        QCOMPARE(scaledToShortSide(QSize(1920, 1080), 2160), QSize(3840, 2160));
        // Higher quality, bigger file; a minute of 1080p30 "Recommended" is tens of MB.
        const RationalTime minute(60, Rational(1));
        settings = ExportSettings{};
        settings.size = QSize(1920, 1080);
        settings.frameRate = Rational(30);
        settings.quality = ExportQuality::High;
        const qint64 high = estimatedFileSize(settings, minute);
        settings.quality = ExportQuality::Recommended;
        const qint64 recommended = estimatedFileSize(settings, minute);
        settings.quality = ExportQuality::Low;
        const qint64 low = estimatedFileSize(settings, minute);
        QVERIFY(low < recommended && recommended < high);
        QVERIFY(recommended > 20'000'000 && recommended < 80'000'000);
        // A size target is a promise: the estimate is that size; without one, HEVC and AV1 make smaller files than H.264.
        settings.quality = ExportQuality::Recommended;
        settings.maxFileSizeMB = 25;
        QCOMPARE(estimatedFileSize(settings, minute), 25ll * 1024 * 1024);
        settings.maxFileSizeMB = 0;
        settings.videoCodec = VideoCodec::H264;
        const qint64 h264Size = estimatedFileSize(settings, minute);
        settings.videoCodec = VideoCodec::AV1;
        QVERIFY(estimatedFileSize(settings, minute) < h264Size);
        QCOMPARE(renderErrorFromCode(renderErrorCode(RenderError::OutputNotWritable)), RenderError::OutputNotWritable);
    }

    // The encoder the machine will use, after the user's choice meets the verified GPU encoders (pure function).
    void encoderPlanFollowsSettingsAndCapabilities()
    {
        const RationalTime minute(60, Rational(1));
        const RationalTime halfMinute(30, Rational(1));
        const QStringList noHardware;
        const QStringList radeon740m{u"h264_vaapi"_s, u"hevc_vaapi"_s, u"av1_vaapi"_s};
        const QStringList arc130v{u"h264_vaapi"_s, u"av1_vaapi"_s, u"av1_qsv"_s, u"h264_qsv"_s};

        ExportSettings settings;
        settings.size = QSize(1920, 1080);
        settings.frameRate = Rational(30);

        // Software: the classic H.264 path, constant quality capped.
        EncoderPlan plan = planEncoder(settings, noHardware, minute);
        QCOMPARE(plan.vcodec, "libx264");
        QVERIFY(!plan.hardware);
        QCOMPARE(plan.qualityOption, "crf");
        QCOMPARE(plan.qualityValue, 21);
        QVERIFY(!plan.preset.isEmpty());
        QVERIFY(plan.videoBitrate == 0 && !plan.twoPass);

        // A verified GPU encoder is preferred, with the hardware quality scale (VA-API, lower is better).
        plan = planEncoder(settings, radeon740m, minute);
        QCOMPARE(plan.vcodec, "h264_vaapi");
        QVERIFY(plan.hardware);
        QCOMPARE(plan.qualityOption, "quality");
        QVERIFY(plan.preset.isEmpty());
        settings.quality = ExportQuality::Low;
        QCOMPARE(planEncoder(settings, radeon740m, minute).qualityValue, 28);
        settings.quality = ExportQuality::High;
        QCOMPARE(planEncoder(settings, radeon740m, minute).qualityValue, 8);
        settings.quality = ExportQuality::Recommended;

        // Every codec family maps to its own encoder, software and hardware; VA-API before the vendor ones.
        settings.videoCodec = VideoCodec::HEVC;
        QCOMPARE(planEncoder(settings, radeon740m, minute).vcodec, "hevc_vaapi");
        QCOMPARE(planEncoder(settings, noHardware, minute).vcodec, "libx265");
        settings.videoCodec = VideoCodec::AV1;
        QCOMPARE(planEncoder(settings, radeon740m, minute).vcodec, "av1_vaapi");
        QCOMPARE(planEncoder(settings, arc130v, minute).vcodec, "av1_vaapi");
        QCOMPARE(planEncoder(settings, noHardware, minute).vcodec, "libsvtav1");
        QVERIFY(planEncoder(settings, noHardware, minute).preset.isEmpty()); // SVT-AV1 has no named presets
        settings.videoCodec = VideoCodec::H264;
        settings.hardwareEncoder = HardwareEncoder::Auto;
        QCOMPARE(planEncoder(settings, QStringList{u"h264_nvenc"_s}, minute).vcodec, "h264_nvenc");

        // The user can turn hardware off; an empty verified list means software even in Auto.
        settings.hardwareEncoder = HardwareEncoder::Off;
        QCOMPARE(planEncoder(settings, radeon740m, minute).vcodec, "libx264");

        // Size target: average bitrate everywhere; two passes in software only.
        settings.hardwareEncoder = HardwareEncoder::Auto;
        settings.maxFileSizeMB = 25;
        plan = planEncoder(settings, noHardware, minute);
        QVERIFY(plan.videoBitrate > 0);
        QVERIFY(plan.twoPass);
        plan = planEncoder(settings, radeon740m, minute);
        QVERIFY(plan.videoBitrate > 0);
        QVERIFY(!plan.twoPass); // hardware encoders have no two-pass: VBR with a ceiling (documented)
        // The bitrate leaves room for audio and the container, and scales with the duration.
        const qint64 minuteBitrate = plan.videoBitrate;
        QVERIFY(planEncoder(settings, radeon740m, halfMinute).videoBitrate > minuteBitrate);
        QVERIFY(minuteBitrate < 25ll * 1024 * 1024 * 8 / 60); // never more bits than the size has
    }

    // A HEVC export through the whole renderer (software encoder, as on a machine without GPU encoders).
    void exportsHevcWhenAsked()
    {
        const ProjectData data = editedProject();
        const QString path = outputPath(u"hevc.mp4"_s);
        ExportSettings settings = settingsFor(data, path);
        settings.videoCodec = VideoCodec::HEVC;
        settings.hardwareEncoder = HardwareEncoder::Off;
        std::atomic<bool> cancel{false};
        const Renderer::Result result = Renderer::render(data, data.mainSequenceId, settings, {}, cancel);
        QCOMPARE(result.status, Renderer::Status::Done);
        const QJsonObject video = streamOfType(ffprobe(path), u"video"_s);
        QCOMPARE(video.value(u"codec_name"_s).toString(), u"hevc"_s);
        QCOMPARE(video.value(u"pix_fmt"_s).toString(), u"yuv420p"_s);
        QCOMPARE(video.value(u"nb_read_frames"_s).toString().toInt(), 195);
    }

    // "Under N MB" (SPEC §5.15): the file must exist, be complete and be smaller than the target.
    void exportRespectsMaximumSize()
    {
        const ProjectData data = editedProject();
        const QString path = outputPath(u"small-size.mp4"_s);
        ExportSettings settings = settingsFor(data, path);
        settings.hardwareEncoder = HardwareEncoder::Off;
        settings.maxFileSizeMB = 5;
        std::atomic<bool> cancel{false};
        const Renderer::Result result = Renderer::render(data, data.mainSequenceId, settings, {}, cancel);
        QCOMPARE(result.status, Renderer::Status::Done);
        QCOMPARE(streamOfType(ffprobe(path), u"video"_s).value(u"nb_read_frames"_s).toString().toInt(), 195);
        QVERIFY2(QFileInfo(path).size() <= 5ll * 1024 * 1024,
                 qPrintable(QString::number(QFileInfo(path).size())));
    }

    // The real GPU encoders when there are any: same export, same content, encoded by the machine's hardware
    // (Radeon 740M: h264/hevc/av1 VA-API; Intel Arc 130V: the same plus QSV). Skipped where the probe finds none,
    // so the suite stays green on machines without VA-API and in the software-only environment.
    void exportsWithTheGpuEncoderWhenAvailable()
    {
        QProcess probe;
        probe.start(QStringLiteral(VEDIT_GPUPROBE_EXECUTABLE), {u"--video"_s});
        QVERIFY(probe.waitForFinished(30000));
        const QJsonDocument report = QJsonDocument::fromJson(probe.readAllStandardOutput());
        QStringList encoders;
        for (const QJsonValue &encoder : report.object().value(u"video"_s).toObject().value(u"encoders"_s).toArray()) {
            encoders.append(encoder.toString());
        }
        if (!encoders.contains(u"h264_vaapi"_s) && !encoders.contains(u"h264_qsv"_s) &&
            !encoders.contains(u"h264_nvenc"_s)) {
            QSKIP("no verified GPU encoder on this machine");
        }
        const ProjectData data = editedProject();
        const QString path = outputPath(u"gpu.mp4"_s);
        ExportSettings settings = settingsFor(data, path);
        settings.hardwareEncoder = HardwareEncoder::Auto;
        std::atomic<bool> cancel{false};
        const Renderer::Result result = Renderer::render(data, data.mainSequenceId, settings, {}, cancel, encoders);
        QCOMPARE(result.status, Renderer::Status::Done);
        const QString encoder = QString::fromLatin1(planEncoder(settings, encoders, RationalTime(195, Rational(30))).vcodec);
        qInfo() << "hardware encoder used:" << encoder;
        const QJsonObject video = streamOfType(ffprobe(path), u"video"_s);
        QCOMPARE(video.value(u"codec_name"_s).toString(), u"h264"_s);
        QCOMPARE(video.value(u"nb_read_frames"_s).toString().toInt(), 195);
    }

    // No MltRuntime::shutdown(): see vedit-render's main() (FFmpeg/x264 globals reported by LeakSanitizer once
    // Mlt::Factory::close() unloads the modules that reference them).
};

QTEST_MAIN(TestExport) // the projection of texts draws with QPainter
#include "tst_export.moc"
