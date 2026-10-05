// SPDX-License-Identifier: GPL-3.0-or-later
#include "Speech.h"

#include "common/Paths.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>

using namespace Qt::StringLiterals;

namespace vedit::ai {

namespace piper {

QString executable()
{
    const QString tts = QStandardPaths::findExecutable(u"piper-tts"_s);
    if (!tts.isEmpty()) {
        return tts;
    }
    // "piper" may be another program: only the speech one knows "--model".
    const QString piper = QStandardPaths::findExecutable(u"piper"_s);
    if (piper.isEmpty()) {
        return {};
    }
    QProcess help;
    help.setProcessChannelMode(QProcess::MergedChannels);
    help.start(piper, {u"--help"_s});
    if (!help.waitForFinished(3000)) {
        help.kill();
        help.waitForFinished();
        return {};
    }
    return help.readAll().contains("--model") ? piper : QString();
}

QString installCommand()
{
    return u"yay -S piper-tts-bin"_s;
}

QString voicesFolder()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + u"/vedit/models/piper"_s;
}

QStringList voices()
{
    QStringList result;
    const QDir folder(voicesFolder());
    for (const QString &name : folder.entryList({u"*.onnx"_s}, QDir::Files, QDir::Name)) {
        if (QFileInfo::exists(folder.filePath(name + u".json"_s))) {
            result << folder.filePath(name);
        }
    }
    return result;
}

QString addVoice(const QString &onnxPath)
{
    const QFileInfo model(onnxPath);
    const QString config = onnxPath + u".json"_s;
    if (model.suffix().toLower() != u"onnx"_s || !QFileInfo::exists(config)) {
        return QObject::tr("A Piper voice is a .onnx file with its .onnx.json next to it.");
    }
    QDir().mkpath(voicesFolder());
    const QString target = voicesFolder() + u'/' + model.fileName();
    QFile::remove(target);
    QFile::remove(target + u".json"_s);
    if (!QFile::copy(onnxPath, target) || !QFile::copy(config, target + u".json"_s)) {
        return QObject::tr("The voice could not be copied to %1.").arg(voicesFolder());
    }
    return {};
}

QString removeVoice(const QString &onnxPath)
{
    if (!onnxPath.startsWith(voicesFolder())) {
        return {};
    }
    QFile::remove(onnxPath + u".json"_s);
    return QFile::remove(onnxPath) ? QString() : QObject::tr("The voice could not be removed.");
}

QString voiceName(const QString &onnxPath)
{
    // Piper's names: <language>-<name>-<quality>.
    const QStringList parts = QFileInfo(onnxPath).completeBaseName().split(u'-');
    if (parts.size() >= 3) {
        return u"%1 %2 (%3)"_s.arg(parts[0], parts.mid(1, parts.size() - 2).join(u' '), parts.last());
    }
    return QFileInfo(onnxPath).completeBaseName();
}

} // namespace piper

SpeechSynthesis::SpeechSynthesis(QString text, QString voice, QObject *parent)
    : AiTask(parent)
    , m_text(std::move(text))
    , m_voice(std::move(voice))
{
}

QString SpeechSynthesis::title() const
{
    return tr("Reading the text aloud");
}

QString SpeechSynthesis::outputPath() const
{
    const QByteArray key = QCryptographicHash::hash((m_voice + u'\n' + m_text).toUtf8(), QCryptographicHash::Sha1).toHex();
    return paths::cacheDir() + u"/speech/"_s + QString::fromLatin1(key) + u".wav"_s;
}

QString SpeechSynthesis::run()
{
    if (QFileInfo(outputPath()).size() > 0) {
        return {};
    }
    const QString program = piper::executable();
    if (program.isEmpty()) {
        return tr("Reading aloud needs Piper: install it with “%1”.").arg(piper::installCommand());
    }
    if (!QFileInfo::exists(m_voice)) {
        return tr("Add a Piper voice first (Preferences → AI models).");
    }
    QDir().mkpath(QFileInfo(outputPath()).absolutePath());
    const QString partial = outputPath() + u".part.wav"_s;
    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(program, {u"--model"_s, m_voice, u"--output_file"_s, partial});
    if (!process.waitForStarted(10000)) {
        return tr("Piper could not be started.");
    }
    process.write(m_text.toUtf8() + '\n');
    process.closeWriteChannel();
    report(0.2);
    while (!process.waitForFinished(200)) {
        if (isCanceled()) {
            process.kill();
            process.waitForFinished();
            QFile::remove(partial);
            return {};
        }
    }
    if (process.exitCode() != 0 || QFileInfo(partial).size() == 0) {
        QFile::remove(partial);
        return tr("Piper could not read the text: %1").arg(QString::fromUtf8(process.readAll()).trimmed().section(u'\n', -2));
    }
    QFile::remove(outputPath());
    if (!QFile::rename(partial, outputPath())) {
        return tr("The speech could not be saved in the cache.");
    }
    return {};
}

} // namespace vedit::ai
