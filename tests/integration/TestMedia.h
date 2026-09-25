// SPDX-License-Identifier: GPL-3.0-or-later
// Small test media generated with ffmpeg at test time (no binary files in the repository).
#pragma once

#include "core/project/Media.h"

#include <QDir>
#include <QFileInfo>
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

// Media item with the metadata the probe would fill (kept explicit so model tests do not depend on it).
inline Media testMedia(MediaKind kind, const QString &path, std::optional<RationalTime> duration, int width, int height,
                       bool audio)
{
    Media media;
    media.id = MediaId::create();
    media.kind = kind;
    media.name = QFileInfo(path).fileName();
    media.path = path;
    media.fingerprint = {QStringLiteral("sha256-sampled-v1"), QStringLiteral("00"), 1};
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
