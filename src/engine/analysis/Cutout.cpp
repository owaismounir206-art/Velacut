// SPDX-License-Identifier: GPL-3.0-or-later
#include "Cutout.h"

#include "common/Paths.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUuid>

#include <cmath>

using namespace Qt::StringLiterals;

namespace vedit::engine {

QString CutoutCopy::path() const
{
    const auto ms = [](const RationalTime &time) { return time.rescaled(Rational(1000), Rounding::NearestEven).value(); };
    return paths::cacheDir() + u"/proxy/"_s + source.fingerprint.value + u"-cutout-%1-%2.mov"_s.arg(ms(from)).arg(ms(to));
}

bool CutoutCopy::ready() const
{
    return QFileInfo(path()).size() > 0;
}

Media CutoutCopy::asMedia() const
{
    Media copy = source;
    copy.id = *MediaId::fromString(
        QUuid::createUuidV5(QUuid(u"{0f3c8b52-4f1d-4c84-9a77-51f1a3d2c6b0}"_s), path()).toString(QUuid::WithoutBraces));
    copy.path = path();
    copy.relativePath.clear();
    copy.fingerprint.value = source.fingerprint.value + u"-cutout"_s;
    copy.info.duration = (to - from).rescaled(Rational(1000), Rounding::NearestEven);
    if (copy.info.video) {
        copy.info.video->hasAlpha = true;
        copy.info.video->codec = u"qtrle"_s;
        if (copy.info.video->rotation == 90 || copy.info.video->rotation == 270) {
            std::swap(copy.info.video->width, copy.info.video->height);
        }
        copy.info.video->rotation = 0;
    }
    return copy;
}

std::optional<CutoutCopy> cutoutCopyFor(const Clip &clip, const Media &media, bool evenWhenOff)
{
    const MediaClipData *data = clip.media();
    if (!data || (!data->cutout && !evenWhenOff) || data->reversed || data->curve || media.kind != MediaKind::Video ||
        !media.info.video || !media.info.duration || !media.fingerprint.isValid()) {
        return std::nullopt;
    }
    const double length = media.info.duration->toSecondsDouble();
    const double first = data->sourceIn.toSecondsDouble();
    const double last = first + clip.duration.toSecondsDouble() * data->speed;
    const auto from = static_cast<std::int64_t>(std::max(0.0, std::floor(first)));
    const auto to = static_cast<std::int64_t>(std::min(std::ceil(length), std::ceil(last)));
    if (to <= from) {
        return std::nullopt;
    }
    return CutoutCopy{media, RationalTime(from, Rational(1)), RationalTime(to, Rational(1))};
}

namespace rembg {

QString executable()
{
    return QStandardPaths::findExecutable(u"rembg"_s);
}

QString installCommand()
{
    return u"pipx install \"rembg[cli]\""_s;
}

bool hasModel()
{
    const QString home = qEnvironmentVariableIsSet("U2NET_HOME") ? qEnvironmentVariable("U2NET_HOME")
                                                                  : QDir::homePath() + u"/.u2net"_s;
    return QFileInfo(home + u"/u2net.onnx"_s).size() > 0;
}

} // namespace rembg

namespace {

// Runs a program to its end, cancellable. False if it failed (the error output goes to `detail`).
bool runProgram(const QString &program, const QStringList &arguments, const std::atomic<bool> *cancel, QString *detail,
                const std::function<void()> &tick = {})
{
    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(program, arguments);
    if (!process.waitForStarted(10000)) {
        *detail = QObject::tr("%1 could not be started.").arg(program);
        return false;
    }
    QByteArray output;
    while (!process.waitForFinished(300)) {
        output += process.readAll();
        if (cancel && cancel->load()) {
            process.kill();
            process.waitForFinished();
            return false;
        }
        if (tick) {
            tick();
        }
    }
    output += process.readAll();
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        *detail = QString::fromUtf8(output).trimmed().section(u'\n', -2);
        return false;
    }
    return true;
}

} // namespace

QString makeCutoutCopy(const CutoutCopy &copy, const std::function<void(double)> &progress, const std::atomic<bool> *cancel)
{
    if (rembg::executable().isEmpty()) {
        return QObject::tr("Removing the background needs rembg: install it with “%1”.").arg(rembg::installCommand());
    }
    QTemporaryDir work;
    if (!work.isValid()) {
        return QObject::tr("No room for temporary files.");
    }
    const QString in = work.filePath(u"in"_s);
    const QString out = work.filePath(u"out"_s);
    QDir().mkpath(in);
    QDir().mkpath(out);
    const QString from = QString::number(copy.from.toSecondsDouble(), 'f', 3);
    const QString to = QString::number(copy.to.toSecondsDouble(), 'f', 3);
    const QString name = QFileInfo(copy.source.path).fileName();
    QString detail;
    // 1. The frames.
    if (!runProgram(u"ffmpeg"_s, {u"-hide_banner"_s, u"-loglevel"_s, u"error"_s, u"-nostdin"_s, u"-y"_s, u"-ss"_s, from, u"-to"_s, to,
                                  u"-i"_s, copy.source.path, u"-fps_mode"_s, u"passthrough"_s, in + u"/%06d.png"_s},
                    cancel, &detail)) {
        return cancel && cancel->load() ? QString() : QObject::tr("The pictures of %1 cannot be read: %2").arg(name, detail);
    }
    const qsizetype total = QDir(in).entryList({u"*.png"_s}, QDir::Files).size();
    if (total == 0) {
        return QObject::tr("The pictures of %1 cannot be read.").arg(name);
    }
    if (progress) {
        progress(0.05);
    }
    // 2. Cut out, frame by frame (the progress is the number of frames done).
    if (!runProgram(rembg::executable(), {u"p"_s, in, out}, cancel, &detail, [&] {
            if (progress) {
                progress(0.05 + 0.85 * static_cast<double>(QDir(out).entryList({u"*.png"_s}, QDir::Files).size()) / total);
            }
        })) {
        return cancel && cancel->load() ? QString() : QObject::tr("rembg could not remove the background of %1: %2").arg(name, detail);
    }
    if (QDir(out).entryList({u"*.png"_s}, QDir::Files).size() != total) {
        return QObject::tr("rembg did not cut out every picture of %1.").arg(name);
    }
    // 3. Back together, with the sound of that part of the file.
    const Rational rate = copy.source.info.video && copy.source.info.video->frameRate ? *copy.source.info.video->frameRate : Rational(30);
    QDir().mkpath(QFileInfo(copy.path()).absolutePath());
    const QString partial = copy.path() + u".part.mov"_s;
    if (!runProgram(u"ffmpeg"_s, {u"-hide_banner"_s, u"-loglevel"_s, u"error"_s, u"-nostdin"_s, u"-y"_s, u"-framerate"_s,
                                  rate.toString(), u"-i"_s, out + u"/%06d.png"_s, u"-ss"_s, from, u"-to"_s, to, u"-i"_s,
                                  copy.source.path, u"-map"_s, u"0:v"_s, u"-map"_s, u"1:a?"_s, u"-c:v"_s, u"qtrle"_s,
                                  u"-pix_fmt"_s, u"argb"_s, u"-c:a"_s, u"pcm_s16le"_s, u"-shortest"_s, partial},
                    cancel, &detail)) {
        QFile::remove(partial);
        return cancel && cancel->load() ? QString() : QObject::tr("The cut-out video of %1 could not be made: %2").arg(name, detail);
    }
    QFile::remove(copy.path());
    if (!QFile::rename(partial, copy.path())) {
        return QObject::tr("The cut-out video could not be saved in the cache.");
    }
    if (progress) {
        progress(1.0);
    }
    return {};
}

} // namespace vedit::engine
