// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ai/AiTask.h"
#include "ai/Transcript.h"

#include <QHash>
#include <QString>

#include <vector>

namespace velacut::ai {

// whisper.cpp (MIT, models MIT: docs/MODELS.md) as an optional external program: when it is missing, the functions
// that need it are shown disabled with the command that installs it (SPEC §1 "Regole sulle dipendenze").
namespace whisper {

// The program, if installed ("whisper-cli" since whisper.cpp 1.7; older packages call it "whisper-cpp"), else empty.
QString executable();
// What the user types to install it (Arch, AUR).
QString installCommand();

struct Model
{
    QString id;       // "base"
    QString file;     // "ggml-base.bin"
    qint64 bytes = 0; // the size of the download
    QString url;
    int quality = 0;  // higher is better (and slower)
};
// The models offered by the model manager (multilingual, from the whisper.cpp project on Hugging Face).
const std::vector<Model> &catalog();
const Model *model(const QString &id);
// Where downloaded models live: <XDG data>/velacut/models/whisper/.
QString modelsFolder();
QString modelPath(const QString &id);
bool installed(const QString &id);
// The best installed model ("" if none).
QString bestInstalled();
// Where the transcript of a file made by `model` in `language` ("auto" = found) is kept.
QString transcriptCachePath(const QString &fingerprint, const QString &model, const QString &language);
// Where its translation into English (whisper.cpp `--translate`) is kept.
QString translationCachePath(const QString &fingerprint, const QString &model, const QString &language);

} // namespace whisper

// Speech recognition of media files with whisper.cpp: the sound of each file (16 kHz mono, by ffmpeg) given to
// `whisper-cli`, its full JSON output read back word by word (parseWhisperJson). Files already transcribed with the
// same model and language come from the cache.
class Transcription : public AiTask
{
    Q_OBJECT

public:
    struct File
    {
        QString path;
        QString fingerprint;
    };
    // `language`: an ISO 639-1 code, or "auto". With `translate`, what is said is written in English (whisper.cpp
    // translates from any language into English only), timed as said.
    Transcription(std::vector<File> files, QString model, QString language, bool translate = false, QObject *parent = nullptr);
    QString title() const override;
    // By fingerprint.
    const QHash<QString, Transcript> &transcripts() const { return m_transcripts; }

protected:
    QString run() override;

private:
    QString transcribe(const File &file, double shareBefore, double shareOfFile, Transcript *result);

    std::vector<File> m_files;
    QString m_model;
    QString m_language;
    bool m_translate = false;
    QHash<QString, Transcript> m_transcripts;
};

} // namespace velacut::ai
