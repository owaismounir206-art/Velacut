// SPDX-License-Identifier: GPL-3.0-or-later
#include "Whisper.h"

#include "common/Paths.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QThread>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace velacut::ai {

namespace whisper {

QString executable()
{
    for (const QString &name : {u"whisper-cli"_s, u"whisper-cpp"_s, u"whisper.cpp"_s}) {
        const QString path = QStandardPaths::findExecutable(name);
        if (!path.isEmpty()) {
            return path;
        }
    }
    return {};
}

QString installCommand()
{
    return u"yay -S whisper.cpp"_s;
}

const std::vector<Model> &catalog()
{
    static const std::vector<Model> models{
        {u"tiny"_s, u"ggml-tiny.bin"_s, 77'691'713, u"https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-tiny.bin"_s, 1},
        {u"base"_s, u"ggml-base.bin"_s, 147'951'465, u"https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-base.bin"_s, 2},
        {u"small"_s, u"ggml-small.bin"_s, 487'601'967, u"https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-small.bin"_s, 3},
        {u"medium"_s, u"ggml-medium.bin"_s, 1'533'763'059, u"https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-medium.bin"_s, 4},
    };
    return models;
}

const Model *model(const QString &id)
{
    const auto &models = catalog();
    const auto it = std::find_if(models.begin(), models.end(), [&id](const Model &m) { return m.id == id; });
    return it == models.end() ? nullptr : &*it;
}

QString modelsFolder()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + u"/velacut/models/whisper"_s;
}

QString modelPath(const QString &id)
{
    const Model *m = model(id);
    return m ? modelsFolder() + u'/' + m->file : QString();
}

bool installed(const QString &id)
{
    const QString path = modelPath(id);
    return !path.isEmpty() && QFileInfo(path).size() > 1'000'000;
}

QString bestInstalled()
{
    QString best;
    int quality = 0;
    for (const Model &m : catalog()) {
        if (installed(m.id) && m.quality > quality) {
            best = m.id;
            quality = m.quality;
        }
    }
    return best;
}

QString transcriptCachePath(const QString &fingerprint, const QString &model, const QString &language)
{
    return paths::cacheDir() + u"/media/"_s + fingerprint + u"/transcript-"_s + model + u'-' + language + u".json"_s;
}

QString translationCachePath(const QString &fingerprint, const QString &model, const QString &language)
{
    return paths::cacheDir() + u"/media/"_s + fingerprint + u"/translation-en-"_s + model + u'-' + language + u".json"_s;
}

} // namespace whisper

Transcription::Transcription(std::vector<File> files, QString model, QString language, bool translate, QObject *parent)
    : AiTask(parent)
    , m_files(std::move(files))
    , m_model(std::move(model))
    , m_language(language.isEmpty() ? u"auto"_s : std::move(language))
    , m_translate(translate)
{
}

QString Transcription::title() const
{
    return m_translate ? tr("Translating the speech into English") : tr("Recognising the speech");
}

QString Transcription::run()
{
    if (whisper::executable().isEmpty()) {
        return tr("Speech recognition needs whisper.cpp: install it with “%1”.").arg(whisper::installCommand());
    }
    if (!whisper::installed(m_model)) {
        return tr("Download a speech model first (Preferences → AI models).");
    }
    const double share = 1.0 / std::max<size_t>(1, m_files.size());
    for (size_t i = 0; i < m_files.size() && !isCanceled(); ++i) {
        const File &file = m_files[i];
        if (m_transcripts.contains(file.fingerprint)) {
            continue;
        }
        const QString cache = m_translate ? whisper::translationCachePath(file.fingerprint, m_model, m_language)
                                          : whisper::transcriptCachePath(file.fingerprint, m_model, m_language);
        QFile cached(cache);
        if (cached.open(QIODevice::ReadOnly)) {
            if (const auto transcript = Transcript::fromJson(QJsonDocument::fromJson(cached.readAll()).object())) {
                m_transcripts.insert(file.fingerprint, *transcript);
                continue;
            }
        }
        Transcript transcript;
        if (const QString error = transcribe(file, static_cast<double>(i) * share, share, &transcript); !error.isEmpty()) {
            return isCanceled() ? QString() : error;
        }
        transcript.model = m_model;
        QDir().mkpath(QFileInfo(cache).absolutePath());
        QSaveFile out(cache);
        if (out.open(QIODevice::WriteOnly)) {
            out.write(QJsonDocument(transcript.toJson()).toJson(QJsonDocument::Compact));
            out.commit();
        }
        m_transcripts.insert(file.fingerprint, std::move(transcript));
    }
    return {};
}

QString Transcription::transcribe(const File &file, double shareBefore, double shareOfFile, Transcript *result)
{
    QTemporaryDir work;
    if (!work.isValid()) {
        return tr("No room for temporary files.");
    }
    const QString name = QFileInfo(file.path).fileName();
    // 1. The sound, as whisper.cpp wants it: 16 kHz, mono, 16 bits.
    const QString wav = work.filePath(u"sound.wav"_s);
    {
        QProcess ffmpeg;
        ffmpeg.start(u"ffmpeg"_s, {u"-hide_banner"_s, u"-loglevel"_s, u"error"_s, u"-nostdin"_s, u"-y"_s, u"-i"_s, file.path,
                                   u"-vn"_s, u"-ac"_s, u"1"_s, u"-ar"_s, u"16000"_s, u"-c:a"_s, u"pcm_s16le"_s, wav});
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
    }
    report(shareBefore + shareOfFile * 0.05);
    // 2. Recognition, with the progress whisper.cpp prints.
    const QString base = work.filePath(u"words"_s);
    QProcess process;
    process.setProcessChannelMode(QProcess::SeparateChannels);
    const int threads = std::clamp(QThread::idealThreadCount() - 1, 1, 8);
    QStringList arguments{u"-m"_s, whisper::modelPath(m_model), u"-f"_s, wav, u"-l"_s, m_language,
                          u"-t"_s, QString::number(threads), u"-pp"_s, u"-ojf"_s, u"-of"_s, base};
    if (m_translate) {
        arguments << u"-tr"_s;
    }
    process.start(whisper::executable(), arguments);
    if (!process.waitForStarted(10000)) {
        return tr("whisper.cpp could not be started.");
    }
    static const QRegularExpression progress(u"progress\\s*=\\s*(\\d+)\\s*%"_s);
    QByteArray errors;
    while (!process.waitForFinished(200)) {
        if (isCanceled()) {
            process.kill();
            process.waitForFinished();
            return {};
        }
        const QByteArray output = process.readAllStandardError();
        errors += output;
        QRegularExpressionMatchIterator matches = progress.globalMatch(QString::fromUtf8(output));
        while (matches.hasNext()) {
            const double percent = matches.next().captured(1).toDouble();
            report(shareBefore + shareOfFile * (0.05 + 0.95 * std::clamp(percent / 100.0, 0.0, 1.0)));
        }
    }
    errors += process.readAllStandardError();
    QFile json(base + u".json"_s);
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0 || !json.open(QIODevice::ReadOnly)) {
        const QString detail = QString::fromUtf8(errors).trimmed().section(u'\n', -3);
        return tr("whisper.cpp could not recognise the speech of %1: %2").arg(name, detail);
    }
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(json.readAll(), &error);
    if (error.error != QJsonParseError::NoError) {
        return tr("whisper.cpp gave an answer velacut cannot read (%1).").arg(error.errorString());
    }
    *result = parseWhisperJson(document.object());
    if (m_translate) {
        result->language = u"en"_s;
    } else if (result->language.isEmpty() && m_language != u"auto"_s) {
        result->language = m_language;
    }
    return {};
}

} // namespace velacut::ai
