// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ai/AiTask.h"

#include <QString>

namespace velacut::ai {

// Demucs (MIT, models MIT: docs/MODELS.md) as an optional external program for "Separate voice and music".
namespace demucs {
QString executable();
QString installCommand();
} // namespace demucs

// The voice and the rest (music, sounds) of a media file's sound, as two WAV files of the same length in the cache
// (by fingerprint): Demucs `--two-stems=vocals` on the sound extracted by ffmpeg. Made once per file.
class VoiceSeparation : public AiTask
{
    Q_OBJECT

public:
    VoiceSeparation(QString path, QString fingerprint, QObject *parent = nullptr);
    QString title() const override;
    QString voicePath() const;
    QString musicPath() const;
    static QString voicePathOf(const QString &fingerprint);
    static QString musicPathOf(const QString &fingerprint);

protected:
    QString run() override;

private:
    QString m_path;
    QString m_fingerprint;
};

} // namespace velacut::ai
