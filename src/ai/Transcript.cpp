// SPDX-License-Identifier: GPL-3.0-or-later
#include "Transcript.h"

#include <QJsonArray>
#include <QRegularExpression>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace vedit::ai {

namespace {

constexpr std::int64_t kPauseMs = 600;
constexpr std::int64_t kHoldMs = 800;
constexpr qsizetype kLineCharacters = 42;

bool endsSentence(const QString &word)
{
    static const QRegularExpression end(u"[.!?\u2026][\"'\u201D\u00BB)]*$"_s);
    return end.match(word).hasMatch();
}

bool isSpecial(const QString &token)
{
    const QString trimmed = token.trimmed();
    return (trimmed.startsWith(u"[_"_s) && trimmed.endsWith(u']')) || trimmed.startsWith(u"<|"_s);
}

std::int64_t offsetOf(const QJsonObject &object, const QString &key)
{
    return static_cast<std::int64_t>(object.value(u"offsets"_s).toObject().value(key).toDouble(-1));
}

} // namespace

QJsonObject Transcript::toJson() const
{
    QJsonArray list;
    for (const Word &word : words) {
        list.append(QJsonArray{word.text, static_cast<qint64>(word.from), static_cast<qint64>(word.to)});
    }
    return {{u"format"_s, u"vedit.transcript"_s}, {u"formatVersion"_s, 1}, {u"language"_s, language}, {u"model"_s, model},
            {u"words"_s, list}};
}

std::optional<Transcript> Transcript::fromJson(const QJsonObject &json)
{
    if (json.value(u"format"_s).toString() != u"vedit.transcript"_s || json.value(u"formatVersion"_s).toInt() != 1) {
        return std::nullopt;
    }
    Transcript transcript;
    transcript.language = json.value(u"language"_s).toString();
    transcript.model = json.value(u"model"_s).toString();
    for (const QJsonValue &value : json.value(u"words"_s).toArray()) {
        const QJsonArray word = value.toArray();
        if (word.size() == 3 && !word[0].toString().isEmpty()) {
            transcript.words.push_back(Word{word[0].toString(), static_cast<std::int64_t>(word[1].toDouble()),
                                            static_cast<std::int64_t>(word[2].toDouble())});
        }
    }
    return transcript;
}

Transcript parseWhisperJson(const QJsonObject &json, std::int64_t offsetMs)
{
    Transcript transcript;
    transcript.language = json.value(u"result"_s).toObject().value(u"language"_s).toString();
    for (const QJsonValue &segmentValue : json.value(u"transcription"_s).toArray()) {
        const QJsonObject segment = segmentValue.toObject();
        const QJsonArray tokens = segment.value(u"tokens"_s).toArray();
        std::vector<Transcript::Word> words;
        if (!tokens.isEmpty()) {
            for (const QJsonValue &tokenValue : tokens) {
                const QJsonObject token = tokenValue.toObject();
                const QString text = token.value(u"text"_s).toString();
                if (text.isEmpty() || isSpecial(text)) {
                    continue;
                }
                const std::int64_t from = offsetOf(token, u"from"_s);
                const std::int64_t to = offsetOf(token, u"to"_s);
                const bool startsWord = text.front().isSpace() || words.empty();
                const QString piece = text.trimmed();
                if (piece.isEmpty()) {
                    continue;
                }
                if (startsWord) {
                    words.push_back(Transcript::Word{piece, from, std::max(from, to)});
                } else {
                    words.back().text += piece;
                    words.back().to = std::max(words.back().to, to);
                }
            }
        } else {
            // No tokens: the words of the text over the segment's time, in proportion to their length.
            const QStringList texts = captions::splitWords(segment.value(u"text"_s).toString());
            const std::int64_t from = offsetOf(segment, u"from"_s);
            const std::int64_t to = offsetOf(segment, u"to"_s);
            qint64 total = 0;
            for (const QString &text : texts) {
                total += text.size() + 1;
            }
            qint64 done = 0;
            for (const QString &text : texts) {
                const std::int64_t a = from + (to - from) * done / std::max<qint64>(1, total);
                done += text.size() + 1;
                const std::int64_t b = from + (to - from) * done / std::max<qint64>(1, total);
                words.push_back(Transcript::Word{text, a, b});
            }
        }
        for (Transcript::Word &word : words) {
            if (word.from < 0 || word.to < word.from) {
                continue; // times whisper.cpp did not give
            }
            word.from += offsetMs;
            word.to += offsetMs;
            transcript.words.push_back(std::move(word));
        }
    }
    // Times go forwards.
    for (size_t i = 1; i < transcript.words.size(); ++i) {
        transcript.words[i].from = std::max(transcript.words[i].from, transcript.words[i - 1].from);
        transcript.words[i].to = std::max(transcript.words[i].to, transcript.words[i].from);
    }
    return transcript;
}

std::vector<captions::CaptionLine> captionLines(const Transcript &transcript, std::int64_t fromMs, std::int64_t toMs,
                                                const std::function<RationalTime(std::int64_t)> &toTimeline)
{
    std::vector<captions::CaptionLine> lines;
    std::vector<const Transcript::Word *> current;
    qsizetype characters = 0;
    const auto close = [&](std::int64_t nextStart) {
        if (current.empty()) {
            return;
        }
        captions::CaptionLine line;
        QStringList texts;
        for (const Transcript::Word *word : current) {
            texts << word->text;
            line.words.push_back(TimedWord{word->text, toTimeline(std::max(word->from, fromMs)), toTimeline(std::min(word->to, toMs))});
        }
        line.text = texts.join(u' ');
        line.start = toTimeline(std::max(current.front()->from, fromMs));
        const std::int64_t end = std::min({nextStart, current.back()->to + kHoldMs, toMs});
        line.end = toTimeline(std::max(end, current.back()->to));
        lines.push_back(std::move(line));
        current.clear();
        characters = 0;
    };
    for (const Transcript::Word &word : transcript.words) {
        if (word.to <= fromMs || word.from >= toMs) {
            continue;
        }
        if (!current.empty()) {
            const bool pause = word.from - current.back()->to > kPauseMs;
            const bool tooLong = characters + 1 + word.text.size() > kLineCharacters;
            if (pause || tooLong || endsSentence(current.back()->text)) {
                close(word.from);
            }
        }
        characters += (current.empty() ? 0 : 1) + word.text.size();
        current.push_back(&word);
    }
    close(toMs);
    return lines;
}

bool isFillerWord(const QString &word)
{
    static const QRegularExpression filler(u"^(e+h*m+|u+h*m+|m+h*m+|e+h+|u+h+|a+h+|h+m+|u+m+|e+r+m*|ehm+|mm+)$"_s,
                                           QRegularExpression::CaseInsensitiveOption);
    QString bare = word;
    static const QRegularExpression punctuation(u"[\\p{P}\\p{S}]"_s);
    bare.remove(punctuation);
    return !bare.isEmpty() && filler.match(bare).hasMatch();
}

} // namespace vedit::ai
