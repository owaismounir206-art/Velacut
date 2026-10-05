// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/project/Captions.h"

#include <QJsonObject>
#include <QString>

#include <cstdint>
#include <optional>
#include <vector>

namespace vedit::ai {

// What is said in a media file, word by word, in the file's own time (milliseconds). Made by speech recognition
// (whisper.cpp); kept in the cache by fingerprint (FILE_FORMAT §6), never in the project: captions, cuts and markers
// made from it are ordinary project objects.
struct Transcript
{
    struct Word
    {
        QString text;          // with its punctuation, without spaces
        std::int64_t from = 0; // ms of the file
        std::int64_t to = 0;
        friend bool operator==(const Word &, const Word &) = default;
    };
    QString language; // ISO 639-1 code found or asked ("it", "en"…)
    QString model;    // the model that made it ("base", "small"…)
    std::vector<Word> words;

    QJsonObject toJson() const;
    static std::optional<Transcript> fromJson(const QJsonObject &json);
    friend bool operator==(const Transcript &, const Transcript &) = default;
};

// The full JSON output of whisper.cpp (`whisper-cli --output-json-full`): segments with their tokens and times.
// Tokens that start with a space start a word, the others (pieces of words, punctuation) join the word before;
// special tokens ("[_BEG_]", "[_TT_…]") are dropped. A segment without tokens has its words spread over its time.
// `offsetMs` is added to every time (the part of the file that was given to whisper.cpp).
Transcript parseWhisperJson(const QJsonObject &json, std::int64_t offsetMs = 0);

// Caption lines from the words said in [fromMs, toMs) of the file, placed on the timeline by `toTimeline` (file ms →
// timeline time). A line ends at the end of a sentence, at a pause of more than 0.6 s, or before it gets longer than
// 42 characters; it stays on screen until the next one starts (at most 0.8 s after its last word).
std::vector<captions::CaptionLine> captionLines(const Transcript &transcript, std::int64_t fromMs, std::int64_t toMs,
                                                const std::function<RationalTime(std::int64_t)> &toTimeline);

// Hesitations that carry no meaning ("ehm", "uhm", "um", "uh", "eh"…), in any language: the words "Remove filler
// words" takes out. Real words that are often fillers ("cioè", "like") are left to the user.
bool isFillerWord(const QString &word);

} // namespace vedit::ai
