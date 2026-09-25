// SPDX-License-Identifier: GPL-3.0-or-later
// Media import: metadata read by the probe, the sampled fingerprint of docs/FILE_FORMAT.md §7, and an import that
// survives files that are not media or that crash the probe.
#include "TestMedia.h"

#include "core/edit/ProjectFormat.h"
#include "engine/analysis/Fingerprint.h"
#include "engine/analysis/MediaAnalysis.h"
#include "engine/analysis/MediaImporter.h"
#include "engine/analysis/MediaProbe.h"

#include <QCryptographicHash>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTransform>
#include <QtEndian>

using namespace vedit;
using namespace vedit::engine;
using namespace vedit::test;
using namespace Qt::StringLiterals;

class TestImport : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;
    TestMediaFiles m_files;
    QString m_rotated;
    QString m_text;

private slots:
    void initTestCase()
    {
        m_files = generateTestMedia(m_dir.filePath(u"media"_s));
        if (!m_files.ok) {
            QSKIP("ffmpeg is needed to generate the test media");
        }
        // Like a phone's portrait video: landscape pixels with a rotation in the display matrix.
        m_rotated = m_dir.filePath(u"media/rotated.mp4"_s);
        QVERIFY(runFfmpeg({u"-display_rotation:v:0"_s, u"90"_s, u"-i"_s, m_files.landscape, u"-c"_s, u"copy"_s, m_rotated}));
        m_text = m_dir.filePath(u"media/notes.txt"_s);
        QFile text(m_text);
        QVERIFY(text.open(QIODevice::WriteOnly));
        text.write("not a video");
    }

    void probesVideo()
    {
        const ProbeResult result = probeMedia(m_files.landscape);
        QVERIFY2(result.media, qPrintable(result.detail));
        const Media &media = *result.media;
        QCOMPARE(media.kind, MediaKind::Video);
        QCOMPARE(media.name, u"landscape.mp4"_s);
        QCOMPARE(media.path, QFileInfo(m_files.landscape).absoluteFilePath());
        QCOMPARE(media.info.video->width, 320);
        QCOMPARE(media.info.video->height, 180);
        QCOMPARE(media.info.video->frameRate, Rational(30));
        QVERIFY(!media.info.video->variableFrameRate);
        QCOMPARE(media.info.video->codec, u"h264"_s);
        QCOMPARE(media.info.video->pixelFormat, u"yuv420p"_s);
        QCOMPARE(media.info.video->rotation, 0);
        QCOMPARE(media.info.duration, RationalTime(120, Rational(30)));
        QCOMPARE(media.info.audio->codec, u"aac"_s);
        QCOMPARE(media.info.audio->sampleRate, 48000);
        QCOMPARE(media.info.audio->channels, 1);
        QCOMPARE(media.fingerprint.algorithm, u"sha256-sampled-v1"_s);
        QCOMPARE(media.fingerprint.size, QFileInfo(m_files.landscape).size());
    }

    void probesRotationAudioAndImages()
    {
        const ProbeResult rotated = probeMedia(m_rotated);
        QVERIFY(rotated.media);
        QCOMPARE(rotated.media->info.video->rotation, 270); // -display_rotation is counter-clockwise
        QCOMPARE(canvasForMedia(*rotated.media)->width, 180);
        QCOMPARE(canvasForMedia(*rotated.media)->height, 320);

        const ProbeResult music = probeMedia(m_files.music);
        QVERIFY(music.media);
        QCOMPARE(music.media->kind, MediaKind::Audio);
        QVERIFY(!music.media->info.video);
        QCOMPARE(music.media->info.duration->rate(), Rational(48000));
        QVERIFY(std::abs(music.media->info.duration->toSecondsDouble() - 6.0) < 0.1);

        const ProbeResult photo = probeMedia(m_files.photo);
        QVERIFY(photo.media);
        QCOMPARE(photo.media->kind, MediaKind::Image);
        QCOMPARE(photo.media->info.video->width, 64);
        QCOMPARE(photo.media->info.video->height, 48);
        QVERIFY(!photo.media->info.duration);
    }

    void rejectsWhatIsNotMedia()
    {
        QCOMPARE(probeMedia(m_text).error, ProbeError::Unsupported);
        QCOMPARE(probeMedia(m_dir.filePath(u"missing.mp4"_s)).error, ProbeError::Missing);
        QCOMPARE(probeErrorFromCode(probeErrorCode(ProbeError::Unreadable)), ProbeError::Unreadable);
    }

    void fingerprintFollowsTheFormatSpecification()
    {
        const auto expected = [](const QByteArray &content, bool sampled) {
            QCryptographicHash hash(QCryptographicHash::Sha256);
            const quint64 size = qToLittleEndian(static_cast<quint64>(content.size()));
            hash.addData(QByteArrayView(reinterpret_cast<const char *>(&size), 8));
            const qsizetype mib = 1024 * 1024;
            if (!sampled) {
                hash.addData(content);
            } else {
                hash.addData(content.left(mib));
                hash.addData(content.mid(content.size() / 2, mib));
                hash.addData(content.right(mib));
            }
            return QString::fromLatin1(hash.result().toHex());
        };
        for (const qsizetype size : {qsizetype(1000), qsizetype(3 * 1024 * 1024), qsizetype(5 * 1024 * 1024 + 7)}) {
            QByteArray content(size, Qt::Uninitialized);
            for (qsizetype i = 0; i < size; ++i) {
                content[i] = static_cast<char>((i * 131) ^ (i >> 11));
            }
            const QString path = m_dir.filePath(u"blob-%1"_s.arg(size));
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write(content);
            file.close();
            const std::optional<MediaFingerprint> fingerprint = sampledFingerprint(path);
            QVERIFY(fingerprint);
            QCOMPARE(fingerprint->size, size);
            QCOMPARE(fingerprint->value, expected(content, size > 3 * 1024 * 1024));
        }
    }

    void importsInOrderThroughTheProbeProcess()
    {
        MediaImporter importer;
        importer.setExecutable(QStringLiteral(VEDIT_RENDER_EXECUTABLE));
        QSignalSpy imported(&importer, &MediaImporter::imported);
        QSignalSpy failed(&importer, &MediaImporter::failed);
        QSignalSpy finished(&importer, &MediaImporter::finished);
        importer.import({m_files.landscape, m_text, m_files.vertical});
        QVERIFY(importer.busy());
        QVERIFY(finished.wait(30000));
        QVERIFY(!importer.busy());
        QCOMPARE(imported.count(), 2);
        QCOMPARE(imported.at(0).at(0).value<Media>().name, u"landscape.mp4"_s);
        QCOMPARE(imported.at(1).at(0).value<Media>().name, u"vertical.mp4"_s);
        QCOMPARE(failed.count(), 1);
        QCOMPARE(failed.first().at(0).toString(), m_text);
        QCOMPARE(failed.first().at(1).toString(), MediaImporter::errorMessage(ProbeError::Unsupported, u"notes.txt"_s));
    }

    void aFileThatCrashesTheProbeIsRejected()
    {
        // A probe that crashes on files named "crash…", like FFmpeg on a malicious or broken file.
        const QString script = m_dir.filePath(u"crashing-probe.sh"_s);
        QFile file(script);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QStringLiteral("#!/bin/sh\nshift\nfor f in \"$@\"; do\n"
                                  "  case \"$f\" in *crash*) printf '{\"event\":\"probing\",\"path\":\"%s\"}\\n' \"$f\"; kill -SEGV $$;; esac\n"
                                  "  '%1' --probe \"$f\" || exit 1\ndone\n")
                       .arg(QStringLiteral(VEDIT_RENDER_EXECUTABLE))
                       .toUtf8());
        file.close();
        QVERIFY(file.setPermissions(file.permissions() | QFile::ExeOwner));
        const QString crash = m_dir.filePath(u"media/crash.mp4"_s);
        QVERIFY(QFile::copy(m_files.landscape, crash));

        MediaImporter importer;
        importer.setExecutable(script);
        QSignalSpy imported(&importer, &MediaImporter::imported);
        QSignalSpy failed(&importer, &MediaImporter::failed);
        QSignalSpy finished(&importer, &MediaImporter::finished);
        importer.import({m_files.landscape, crash, m_files.music});
        QVERIFY(finished.wait(30000));
        QCOMPARE(imported.count(), 2); // before and after the crash
        QCOMPARE(imported.at(1).at(0).value<Media>().kind, MediaKind::Audio);
        QCOMPARE(failed.count(), 1);
        QCOMPARE(failed.first().at(0).toString(), crash);
        QCOMPARE(failed.first().at(1).toString(), MediaImporter::errorMessage(ProbeError::Damaged, u"crash.mp4"_s));
    }

    void thumbnailsShowTheMediaAsDisplayed()
    {
        const QString cache = m_dir.filePath(u"cache"_s);
        const Media landscape = *probeMedia(m_files.landscape).media;
        const Media rotated = *probeMedia(m_rotated).media;
        const Media photo = *probeMedia(m_files.photo).media;
        QImage strip;
        {
            MediaAnalysis analysis(cache);
            QSignalSpy ready(&analysis, &MediaAnalysis::thumbnailsReady);
            QVERIFY(analysis.thumbnails(landscape).isNull()); // computed in background
            QVERIFY(analysis.thumbnails(rotated).isNull());
            QVERIFY(analysis.thumbnails(photo).isNull());
            QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 3, 20000);
            strip = analysis.thumbnails(landscape);
            // 4 s: one frame per second, 16:9 at 90 px.
            QCOMPARE(MediaAnalysis::thumbnailCount(landscape), 4);
            QCOMPARE(strip.size(), QSize(4 * 160, 90));
            for (int i = 0; i < 4; ++i) {
                const QImage source = decodeFrame(m_files.landscape, 15 + 30 * i, m_dir.path()).scaled(160, 90);
                const double difference = meanDifference(strip.copy(i * 160, 0, 160, 90), source);
                QVERIFY2(difference >= 0 && difference < 12.0, qPrintable(u"frame %1: %2"_s.arg(i).arg(difference)));
            }
            QVERIFY(meanDifference(strip.copy(0, 0, 160, 90), strip.copy(480, 0, 160, 90)) > 5.0);
            // Portrait as displayed: the landscape frames turned by the rotation.
            const QImage portrait = analysis.thumbnails(rotated);
            QCOMPARE(portrait.size(), QSize(4 * 50, 90));
            const QImage turned = strip.copy(0, 0, 160, 90).transformed(QTransform().rotate(270)).scaled(50, 90);
            QVERIFY(meanDifference(portrait.copy(0, 0, 50, 90), turned) < 20.0);
            const QImage picture = analysis.thumbnails(photo);
            QCOMPARE(picture.size(), QSize(120, 90));
            QVERIFY(qRed(picture.pixel(60, 45)) > 230 && qGreen(picture.pixel(60, 45)) < 30);
        }
        // Cached on disk: available at once in a new session.
        MediaAnalysis again(cache);
        QCOMPARE(again.thumbnails(landscape).size(), strip.size());
    }

    void waveformsFollowTheAudio()
    {
        const Media music = *probeMedia(m_files.music).media;
        const Media photo = *probeMedia(m_files.photo).media;
        MediaAnalysis analysis(m_dir.filePath(u"cache"_s));
        QSignalSpy ready(&analysis, &MediaAnalysis::waveformReady);
        QVERIFY(!analysis.waveform(music));
        QVERIFY(!analysis.waveform(photo)); // no audio: never
        QVERIFY(ready.wait(20000));
        const std::shared_ptr<const Waveform> waveform = analysis.waveform(music);
        QVERIFY(waveform);
        QCOMPARE(waveform->bucketsPerSecond, 100);
        QVERIFY2(std::abs(waveform->bucketCount() - 600) <= 10, qPrintable(QString::number(waveform->bucketCount())));
        // ffmpeg's sine source has an amplitude of 1/8: peaks at about ±16 of 127.
        const auto [low, high] = std::minmax_element(waveform->peaks.begin(), waveform->peaks.end());
        QVERIFY2(*high >= 12 && *high <= 20 && *low <= -12 && *low >= -20, qPrintable(u"%1 %2"_s.arg(int(*low)).arg(int(*high))));
        MediaAnalysis again(m_dir.filePath(u"cache"_s));
        QCOMPARE(again.waveform(music)->peaks, waveform->peaks); // from the disk cache
    }

    void missingHelperIsReported()
    {
        MediaImporter importer;
        importer.setExecutable(m_dir.filePath(u"no-such-program"_s));
        QSignalSpy failed(&importer, &MediaImporter::failed);
        QSignalSpy finished(&importer, &MediaImporter::finished);
        importer.import({m_files.landscape});
        QVERIFY(finished.count() == 1 || finished.wait(5000));
        QCOMPARE(failed.count(), 1);
    }
};

QTEST_GUILESS_MAIN(TestImport)
#include "tst_import.moc"
