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
