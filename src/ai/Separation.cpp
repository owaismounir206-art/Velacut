// SPDX-License-Identifier: GPL-3.0-or-later
#include "Separation.h"

#include "common/Paths.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTemporaryDir>

using namespace Qt::StringLiterals;

namespace velacut::ai {

namespace demucs {

QString executable()
{
    return QStandardPaths::findExecutable(u"demucs"_s);
}

QString installCommand()
{
    return u"pipx install demucs"_s;
}

} // namespace demucs

VoiceSeparation::VoiceSeparation(QString path, QString fingerprint, QObject *parent)
    : AiTask(parent)
    , m_path(std::move(path))
    , m_fingerprint(std::move(fingerprint))
{
}

QString VoiceSeparation::title() const
{
    return tr("Separating voice and music");
}

QString VoiceSeparation::voicePathOf(const QString &fingerprint)
{
    return paths::cacheDir() + u"/media/"_s + fingerprint + u"/voice.wav"_s;
}

QString VoiceSeparation::musicPathOf(const QString &fingerprint)
{
    return paths::cacheDir() + u"/media/"_s + fingerprint + u"/music.wav"_s;
}

QString VoiceSeparation::voicePath() const
{
    return voicePathOf(m_fingerprint);
}

QString VoiceSeparation::musicPath() const
{
    return musicPathOf(m_fingerprint);
}

QString VoiceSeparation::run()
{
    if (QFileInfo(voicePath()).size() > 0 && QFileInfo(musicPath()).size() > 0) {
        return {};
    }
    if (demucs::executable().isEmpty()) {
        return tr("Separating voice and music needs Demucs: install it with “%1”.").arg(demucs::installCommand());
    }
    QTemporaryDir work;
    if (!work.isValid()) {
        return tr("No room for temporary files.");
    }
    const QString name = QFileInfo(m_path).fileName();
    // The sound alone, as Demucs reads it best.
    const QString wav = work.filePath(u"sound.wav"_s);
    QProcess ffmpeg;
    ffmpeg.start(u"ffmpeg"_s, {u"-hide_banner"_s, u"-loglevel"_s, u"error"_s, u"-nostdin"_s, u"-y"_s, u"-i"_s, m_path, u"-vn"_s,
                               u"-ac"_s, u"2"_s, u"-ar"_s, u"44100"_s, u"-c:a"_s, u"pcm_s16le"_s, wav});
    while (!ffmpeg.waitForFinished(200)) {
        if (isCanceled() || ffmpeg.state() == QProcess::NotRunning) {
            ffmpeg.kill();
            ffmpeg.waitForFinished();
            break;
        }
    }
    if (isCanceled()) {
        return {};
    }
    if (ffmpeg.exitCode() != 0 || QFileInfo(wav).size() == 0) {
        return tr("The sound of %1 cannot be read.").arg(name);
    }
    report(0.05);
    const QString out = work.filePath(u"out"_s);
    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(demucs::executable(), {u"--two-stems=vocals"_s, u"-n"_s, u"htdemucs"_s, u"-o"_s, out, wav});
    if (!process.waitForStarted(10000)) {
        return tr("Demucs could not be started.");
    }
    // The progress bar of Demucs (tqdm): " 42%|████      |".
    static const QRegularExpression percent(u"(\\d+)%\\|"_s);
    QByteArray output;
    while (!process.waitForFinished(300)) {
        if (isCanceled()) {
            process.kill();
            process.waitForFinished();
            return {};
        }
        const QByteArray chunk = process.readAll();
        output += chunk;
        QRegularExpressionMatchIterator matches = percent.globalMatch(QString::fromUtf8(chunk));
        while (matches.hasNext()) {
            report(0.05 + 0.9 * matches.next().captured(1).toDouble() / 100.0);
        }
    }
    output += process.readAll();
    // <out>/htdemucs/sound/vocals.wav and no_vocals.wav (found wherever the model folder is).
    QString vocals;
    QString rest;
    QDirIterator files(out, {u"*.wav"_s}, QDir::Files, QDirIterator::Subdirectories);
    while (files.hasNext()) {
        const QString file = files.next();
        const QString base = QFileInfo(file).fileName();
        if (base == u"vocals.wav"_s) {
            vocals = file;
        } else if (base == u"no_vocals.wav"_s) {
            rest = file;
        }
    }
    if (process.exitCode() != 0 || vocals.isEmpty() || rest.isEmpty()) {
        return tr("Demucs could not separate %1: %2").arg(name, QString::fromUtf8(output).trimmed().section(u'\n', -2));
    }
    QDir().mkpath(QFileInfo(voicePath()).absolutePath());
    QFile::remove(voicePath());
    QFile::remove(musicPath());
    if (!QFile::copy(vocals, voicePath()) || !QFile::copy(rest, musicPath())) {
        return tr("The separated sounds could not be saved in the cache.");
    }
    report(1.0);
    return {};
}

} // namespace velacut::ai
