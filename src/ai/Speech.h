// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ai/AiTask.h"

#include <QString>
#include <QStringList>

namespace velacut::ai {

// Piper (MIT) as an optional external program for "Read aloud" (text to speech, SPEC §5.12). Voices are files the
// user adds (a .onnx model with its .onnx.json): their licences differ from voice to voice, so velacut offers none of its
// own (docs/MODELS.md).
namespace piper {
// "piper-tts" (the name of the Arch packages: "piper" is a mouse configuration tool there), else a "piper" that is
// the speech one.
QString executable();
QString installCommand();
QString voicesFolder();
// The voices added (paths of the .onnx files), by name.
QStringList voices();
// Copies a voice (.onnx, with the .onnx.json next to it) into the voices folder. An error for the user, or empty.
QString addVoice(const QString &onnxPath);
QString removeVoice(const QString &onnxPath);
// "it_IT-paola-medium" → "it_IT paola (medium)".
QString voiceName(const QString &onnxPath);
} // namespace piper

// Speech of a text with a voice, as a WAV file in the cache (the same text and voice are made once).
class SpeechSynthesis : public AiTask
{
    Q_OBJECT

public:
    SpeechSynthesis(QString text, QString voice, QObject *parent = nullptr);
    QString title() const override;
    QString outputPath() const;

protected:
    QString run() override;

private:
    QString m_text;
    QString m_voice;
};

} // namespace velacut::ai
