// SPDX-License-Identifier: GPL-3.0-or-later
// Small test media generated with ffmpeg at test time (no binary files in the repository).
#pragma once

#include "core/project/Media.h"

#include <QDir>
#include <QCryptographicHash>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>
#include <QString>

namespace vedit::test {

struct TestMediaFiles
{
    QString landscape; // 320x180, 30 fps, 4 s, sine 440 Hz
    QString vertical;  // 180x320, 30 fps, 4 s, sine 660 Hz
    QString music;     // 6 s mp3, 330 Hz
    QString photo;     // 64x48 png, red
    bool ok = false;
};

inline bool runFfmpeg(const QStringList &arguments)
{
    const QString ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
    if (ffmpeg.isEmpty()) {
        return false;
    }
    return QProcess::execute(ffmpeg, QStringList{QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"),
                                                 QStringLiteral("error"), QStringLiteral("-y")} +
                                         arguments) == 0;
}

inline TestMediaFiles generateTestMedia(const QString &directory)
{
    TestMediaFiles files;
    QDir().mkpath(directory);
    files.landscape = directory + QStringLiteral("/landscape.mp4");
    files.vertical = directory + QStringLiteral("/vertical.mp4");
    files.music = directory + QStringLiteral("/music.mp3");
    files.photo = directory + QStringLiteral("/photo.png");
    const auto video = [](const QString &size, int frequency, const QString &path) {
        return runFfmpeg({QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
                          QStringLiteral("testsrc2=size=%1:rate=30").arg(size), QStringLiteral("-f"), QStringLiteral("lavfi"),
                          QStringLiteral("-i"), QStringLiteral("sine=frequency=%1:sample_rate=48000").arg(frequency),
                          QStringLiteral("-t"), QStringLiteral("4"), QStringLiteral("-c:v"), QStringLiteral("libx264"),
                          QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"), QStringLiteral("-g"), QStringLiteral("10"),
                          QStringLiteral("-c:a"), QStringLiteral("aac"), QStringLiteral("-shortest"), path});
    };
    files.ok = video(QStringLiteral("320x180"), 440, files.landscape) && video(QStringLiteral("180x320"), 660, files.vertical) &&
               runFfmpeg({QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
                          QStringLiteral("sine=frequency=330:sample_rate=48000"), QStringLiteral("-t"), QStringLiteral("6"),
                          QStringLiteral("-c:a"), QStringLiteral("libmp3lame"), files.music}) &&
               runFfmpeg({QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
                          QStringLiteral("color=c=red:size=64x48"), QStringLiteral("-frames:v"), QStringLiteral("1"), files.photo});
    return files;
}

// ffprobe's view of a file: {"streams": [...], "format": {...}}, with exact frame counts (-count_frames).
inline QJsonObject ffprobe(const QString &path)
{
    const QString program = QStandardPaths::findExecutable(QStringLiteral("ffprobe"));
    if (program.isEmpty()) {
        return {};
    }
    QProcess process;
    process.start(program, {QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-count_frames"),
                            QStringLiteral("-show_streams"), QStringLiteral("-show_format"), QStringLiteral("-of"),
                            QStringLiteral("json"), path});
    process.waitForFinished(60000);
    return QJsonDocument::fromJson(process.readAllStandardOutput()).object();
}

inline QJsonObject streamOfType(const QJsonObject &probe, const QString &type)
{
    for (const QJsonValue &stream : probe.value(QStringLiteral("streams")).toArray()) {
        if (stream.toObject().value(QStringLiteral("codec_type")).toString() == type) {
            return stream.toObject();
        }
    }
    return {};
}

// Frame `index` of a video file (exact, decoded by ffmpeg), as RGB.
inline QImage decodeFrame(const QString &path, int index, const QString &scratch)
{
    const QString png = scratch + QStringLiteral("/frame-%1.png").arg(index);
    if (!runFfmpeg({QStringLiteral("-i"), path, QStringLiteral("-vf"), QStringLiteral("select=eq(n\\,%1)").arg(index),
                    QStringLiteral("-frames:v"), QStringLiteral("1"), QStringLiteral("-update"), QStringLiteral("1"), png})) {
        return {};
    }
    return QImage(png).convertToFormat(QImage::Format_RGB888);
}

// Mean absolute difference per channel (0–255) between two images of the same size; -1 if sizes differ.
inline double meanDifference(const QImage &a, const QImage &b)
{
    if (a.size() != b.size() || a.isNull()) {
        return -1;
    }
    const QImage x = a.convertToFormat(QImage::Format_RGB888);
    const QImage y = b.convertToFormat(QImage::Format_RGB888);
    double sum = 0;
    for (int row = 0; row < x.height(); ++row) {
        const uchar *p = x.constScanLine(row);
        const uchar *q = y.constScanLine(row);
        for (int i = 0; i < x.width() * 3; ++i) {
            sum += std::abs(int(p[i]) - int(q[i]));
        }
    }
    return sum / (double(x.width()) * x.height() * 3);
}

// Media item with the metadata the probe would fill (kept explicit so model tests do not depend on it).
inline Media testMedia(MediaKind kind, const QString &path, std::optional<RationalTime> duration, int width, int height,
                       bool audio)
{
    Media media;
    media.id = MediaId::create();
    media.kind = kind;
    media.name = QFileInfo(path).fileName();
    media.path = path;
    // One fingerprint per file: caches keyed by fingerprint (thumbnails, waveforms, spectra) never mix test files.
    media.fingerprint = {QStringLiteral("sha256-sampled-v1"),
                         QString::fromLatin1(QCryptographicHash::hash(path.toUtf8(), QCryptographicHash::Sha256).toHex()), 1};
    media.info.duration = duration;
    if (kind != MediaKind::Audio) {
        VideoStreamInfo video;
        video.width = width;
        video.height = height;
        video.frameRate = Rational(30);
        media.info.video = video;
    }
    if (audio) {
        media.info.audio = AudioStreamInfo{QStringLiteral("aac"), 48000, 1};
    }
    return media;
}

} // namespace vedit::test
