// SPDX-License-Identifier: GPL-3.0-or-later
#include "Captions.h"

#include <QRegularExpression>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace vedit::captions {

QStringList splitWords(const QString &text)
{
    static const QRegularExpression space(u"\\s+"_s);
    return text.split(space, Qt::SkipEmptyParts);
}

std::vector<TimedWord> timedWords(const SubtitleClipData &line, const RationalTime &duration)
{
    const QStringList words = splitWords(line.text);
    bool matching = line.words.size() == static_cast<size_t>(words.size()) && !words.isEmpty();
    for (size_t i = 0; matching && i < line.words.size(); ++i) {
        matching = line.words[i].text == words[static_cast<qsizetype>(i)];
    }
    if (matching) {
        return line.words;
    }
    std::vector<TimedWord> result;
    if (words.isEmpty() || duration.value() <= 0) {
        return result;
    }
    // In proportion to the length of each word (+1 for the pause after it): long words take longer to say.
    qint64 total = 0;
    for (const QString &word : words) {
        total += word.size() + 1;
    }
    const Rational rate = duration.rate();
    qint64 done = 0;
    for (const QString &word : words) {
        const qint64 from = duration.value() * done / total;
        done += word.size() + 1;
        const qint64 to = duration.value() * done / total;
        result.push_back(TimedWord{word, RationalTime(from, rate), RationalTime(std::max(to, from + 1), rate)});
    }
    result.back().end = duration;
    return result;
}

std::pair<SubtitleClipData, SubtitleClipData> split(const SubtitleClipData &line, const RationalTime &duration,
                                                    const RationalTime &offset)
{
    SubtitleClipData first = line;
    SubtitleClipData second = line;
    first.words.clear();
    second.words.clear();
    QStringList firstText;
    QStringList secondText;
    // Word times can come from files at any rate: the cut is computed at the clip's rate.
    const Rational rate = duration.rate();
    const RationalTime cut = offset.rescaled(rate, Rounding::NearestEven);
    for (const TimedWord &word : timedWords(line, duration)) {
        const RationalTime start = word.start.rescaled(rate, Rounding::NearestEven);
        const RationalTime end = word.end.rescaled(rate, Rounding::NearestEven);
        // A word goes where most of it is said.
        if (start + end < cut * 2) {
            first.words.push_back(TimedWord{word.text, start, std::min(end, cut)});
            firstText << word.text;
        } else {
            const RationalTime from = std::max(start, cut) - cut;
            second.words.push_back(TimedWord{word.text, from, std::max(from, end - cut)});
            secondText << word.text;
        }
    }
    first.text = firstText.join(u' ');
    second.text = secondText.join(u' ');
    return {first, second};
}

SubtitleClipData withText(const SubtitleClipData &line, const QString &text)
{
    SubtitleClipData result = line;
    const QStringList words = splitWords(text);
    result.text = words.join(u' ');
    if (static_cast<size_t>(words.size()) == line.words.size()) {
        for (size_t i = 0; i < result.words.size(); ++i) {
            result.words[i].text = words[static_cast<qsizetype>(i)];
        }
    } else {
        result.words.clear();
    }
    return result;
}

SubtitleClipData merged(const SubtitleClipData &first, const RationalTime &firstDuration, const SubtitleClipData &second,
                        const RationalTime &secondDuration, const RationalTime &gap)
{
    SubtitleClipData result = first;
    result.words = timedWords(first, firstDuration);
    const RationalTime offset = firstDuration + gap.rescaled(firstDuration.rate(), Rounding::NearestEven);
    for (const TimedWord &word : timedWords(second, secondDuration)) {
        result.words.push_back(TimedWord{word.text, word.start.rescaled(offset.rate(), Rounding::NearestEven) + offset,
                                         word.end.rescaled(offset.rate(), Rounding::NearestEven) + offset});
    }
    result.text = QStringList{first.text, second.text}.join(u' ').trimmed();
    return result;
}

std::vector<WordGroup> groupsOf(const std::vector<TimedWord> &words, int maxWords, const RationalTime &duration)
{
    std::vector<WordGroup> groups;
    if (words.empty()) {
        return groups;
    }
    const auto sentenceEnd = [](const QString &word) {
        static const QRegularExpression end(u"[.!?\u2026][\"'\u201D\u00BB)]*$"_s);
        return end.match(word).hasMatch();
    };
    const int count = static_cast<int>(words.size());
    for (int i = 0; i < count; ++i) {
        const bool full = maxWords > 0 && !groups.empty() && groups.back().count >= maxWords;
        const bool afterSentence = maxWords > 0 && i > 0 && sentenceEnd(words[static_cast<size_t>(i - 1)].text);
        if (groups.empty() || full || afterSentence) {
            groups.push_back(WordGroup{i, 0, words[static_cast<size_t>(i)].start, duration});
        }
        ++groups.back().count;
    }
    groups.front().start = RationalTime(0, duration.rate());
    for (size_t i = 0; i + 1 < groups.size(); ++i) {
        groups[i + 1].start = std::clamp(groups[i + 1].start, groups[i].start, duration);
        groups[i].end = groups[i + 1].start;
    }
    return groups;
}

int activeWord(const std::vector<TimedWord> &words, const RationalTime &time)
{
    int active = -1;
    for (size_t i = 0; i < words.size() && words[i].start <= time; ++i) {
        active = static_cast<int>(i);
    }
    return active;
}

std::vector<SubtitleEntry> entriesOf(const Track &track)
{
    std::vector<SubtitleEntry> entries;
    for (const Clip &clip : track.clips) {
        if (const SubtitleClipData *line = clip.subtitle()) {
            entries.push_back(SubtitleEntry{clip.start, clip.end(), line->text});
        }
    }
    return entries;
}

std::vector<CaptionLine> linesOf(const std::vector<SubtitleEntry> &entries)
{
    std::vector<CaptionLine> lines;
    for (const SubtitleEntry &entry : entries) {
        if (entry.end > entry.start && !entry.text.trimmed().isEmpty()) {
            // Subtitle files break long lines with newlines: on screen the caption style decides.
            lines.push_back(CaptionLine{entry.start, entry.end, splitWords(entry.text).join(u' '), {}});
        }
    }
    return lines;
}

} // namespace vedit::captions
