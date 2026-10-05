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

namespace {

// A word for comparing: lower case, without punctuation or accents.
QString comparable(const QString &word)
{
    QString text = word.normalized(QString::NormalizationForm_D).toLower();
    QString result;
    for (const QChar c : text) {
        if (c.isLetterOrNumber()) {
            result += c;
        }
    }
    return result;
}

} // namespace

std::vector<SpokenWord> alignScript(const QString &script, const std::vector<SpokenWord> &spoken)
{
    const QStringList written = captions::splitWords(script);
    std::vector<SpokenWord> result;
    if (written.isEmpty()) {
        return result;
    }
    const size_t n = static_cast<size_t>(written.size());
    const size_t m = spoken.size();
    std::vector<QString> a(n);
    std::vector<QString> b(m);
    for (size_t i = 0; i < n; ++i) {
        a[i] = comparable(written[static_cast<qsizetype>(i)]);
    }
    for (size_t j = 0; j < m; ++j) {
        b[j] = comparable(spoken[j].text);
    }
    // Fewest insertions, deletions and substitutions (Levenshtein on words), with the path kept.
    std::vector<std::uint32_t> cost((n + 1) * (m + 1));
    const auto at = [m](size_t i, size_t j) { return i * (m + 1) + j; };
    for (size_t i = 0; i <= n; ++i) {
        cost[at(i, 0)] = static_cast<std::uint32_t>(i);
    }
    for (size_t j = 0; j <= m; ++j) {
        cost[at(0, j)] = static_cast<std::uint32_t>(j);
    }
    for (size_t i = 1; i <= n; ++i) {
        for (size_t j = 1; j <= m; ++j) {
            const std::uint32_t same = a[i - 1] == b[j - 1] ? 0 : 2; // a substitution costs as much as two gaps
            cost[at(i, j)] = std::min({cost[at(i - 1, j - 1)] + same, cost[at(i - 1, j)] + 1, cost[at(i, j - 1)] + 1});
        }
    }
    // Each written word: the spoken word it is matched or substituted with, or none.
    std::vector<long> match(n, -1);
    size_t i = n;
    size_t j = m;
    while (i > 0 && j > 0) {
        const std::uint32_t same = a[i - 1] == b[j - 1] ? 0 : 2;
        if (cost[at(i, j)] == cost[at(i - 1, j - 1)] + same) {
            match[i - 1] = static_cast<long>(j - 1);
            --i;
            --j;
        } else if (cost[at(i, j)] == cost[at(i - 1, j)] + 1) {
            --i;
        } else {
            --j;
        }
    }
    // Times: the matched word's; the others between the neighbours that have one.
    result.resize(n);
    for (size_t k = 0; k < n; ++k) {
        result[k].text = written[static_cast<qsizetype>(k)];
        if (match[k] >= 0) {
            result[k].from = spoken[static_cast<size_t>(match[k])].from;
            result[k].to = spoken[static_cast<size_t>(match[k])].to;
        }
    }
    size_t k = 0;
    while (k < n) {
        if (match[k] >= 0) {
            ++k;
            continue;
        }
        size_t end = k;
        while (end < n && match[end] < 0) {
            ++end;
        }
        const std::int64_t before = k > 0 ? result[k - 1].to : (m > 0 ? spoken.front().from : 0);
        const std::int64_t after = end < n ? result[end].from : (m > 0 ? spoken.back().to : before + static_cast<std::int64_t>(end - k) * 400);
        const auto count = static_cast<std::int64_t>(end - k);
        for (size_t g = k; g < end; ++g) {
            const auto index = static_cast<std::int64_t>(g - k);
            result[g].from = before + (after - before) * index / count;
            result[g].to = before + (after - before) * (index + 1) / count;
        }
        k = end;
    }
    return result;
}

std::vector<Chapter> findChapters(const std::vector<SpokenWord> &words, std::int64_t durationMs)
{
    constexpr std::int64_t kMinimum = 10'000;
    constexpr std::int64_t kPerChapter = 60'000;
    std::vector<Chapter> chapters;
    if (durationMs < 3 * kMinimum || words.empty()) {
        return chapters;
    }
    // Sentence starts, with the pause before them.
    struct Start
    {
        size_t word;
        std::int64_t pause;
    };
    std::vector<Start> starts;
    for (size_t i = 1; i < words.size(); ++i) {
        if (endsSentence(words[i - 1].text)) {
            starts.push_back({i, words[i].from - words[i - 1].to});
        }
    }
    const auto titleFrom = [&words](size_t first) {
        QStringList title;
        for (size_t i = first; i < words.size() && title.size() < 6; ++i) {
            title << words[i].text;
            if (endsSentence(words[i].text)) {
                break;
            }
        }
        QString text = title.join(u' ');
        static const QRegularExpression trailing(u"[\\p{P}]+$"_s);
        text.remove(trailing);
        if (!text.isEmpty()) {
            text[0] = text[0].toUpper();
        }
        return text;
    };
    const int count = static_cast<int>(std::clamp<std::int64_t>(durationMs / kPerChapter, 3, 12));
    chapters.push_back(Chapter{0, titleFrom(0)});
    for (int k = 1; k < count; ++k) {
        const std::int64_t ideal = durationMs * k / count;
        const std::int64_t window = durationMs / count * 3 / 10;
        const Start *best = nullptr;
        double bestScore = -1.0;
        for (const Start &start : starts) {
            const std::int64_t at = words[start.word].from;
            if (std::abs(at - ideal) > window || at - chapters.back().start < kMinimum || durationMs - at < kMinimum) {
                continue;
            }
            // Long pauses close to the ideal point.
            const double score = static_cast<double>(start.pause) - 0.3 * static_cast<double>(std::abs(at - ideal));
            if (score > bestScore) {
                bestScore = score;
                best = &start;
            }
        }
        if (best) {
            chapters.push_back(Chapter{words[best->word].from, titleFrom(best->word)});
        }
    }
    if (chapters.size() < 3) {
        chapters.clear();
    }
    return chapters;
}

QString chapterList(const std::vector<Chapter> &chapters, std::int64_t durationMs)
{
    QStringList lines;
    const bool hours = durationMs >= 3'600'000;
    for (const Chapter &chapter : chapters) {
        const std::int64_t seconds = chapter.start / 1000;
        const QString time = hours ? u"%1:%2:%3"_s.arg(seconds / 3600).arg(seconds / 60 % 60, 2, 10, u'0').arg(seconds % 60, 2, 10, u'0')
                                   : u"%1:%2"_s.arg(seconds / 60, 2, 10, u'0').arg(seconds % 60, 2, 10, u'0');
        lines << time + u' ' + chapter.title;
    }
    return lines.join(u'\n');
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
