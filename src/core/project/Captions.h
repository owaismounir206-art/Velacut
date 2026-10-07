// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/project/Clip.h"
#include "core/project/Track.h"
#include "core/subtitle/SubtitleFormat.h"

#include <QStringList>

#include <utility>
#include <vector>

namespace vedit::captions {

// A line of captions on the timeline, with times from the start of the sequence (from a subtitle file, a transcription
// or a script). `words` are optional (absolute times too).
struct CaptionLine
{
    RationalTime start;
    RationalTime end;
    QString text;
    std::vector<TimedWord> words;
    QString translation{}; // bilingual captions
};

// The words of a text (split on white space, punctuation kept with its word).
QStringList splitWords(const QString &text);

// The words of a line with their times from the clip's start: its own timed words when they still match its text
// (same words in the same order), else the words of the text spread over `duration` in proportion to their length
// (so an edited line, or one imported from SRT, still highlights word by word).
std::vector<TimedWord> timedWords(const SubtitleClipData &line, const RationalTime &duration);

// A line cut at `offset` from its start: the words before go to the first half, the others (times shifted) to the
// second; the texts follow the words.
std::pair<SubtitleClipData, SubtitleClipData> split(const SubtitleClipData &line, const RationalTime &duration,
                                                    const RationalTime &offset);

// The line with another text: its word timings are kept when the text still has as many words (a word corrected),
// otherwise they are spread again over the line (timedWords).
SubtitleClipData withText(const SubtitleClipData &line, const QString &text);

// Two consecutive lines as one: `first` (of `firstDuration`) followed after `gap` by `second`.
SubtitleClipData merged(const SubtitleClipData &first, const RationalTime &firstDuration, const SubtitleClipData &second,
                        const RationalTime &secondDuration, const RationalTime &gap);

// Words shown together on screen ("words per line" of the caption style), with their time on screen from the clip's
// start: groups of `maxWords` words (0 = the whole line), a sentence end (. ! ? …) also closing a group. Each group
// stays until the next one starts; the first starts with the clip, the last ends with it.
struct WordGroup
{
    int first = 0;
    int count = 0;
    RationalTime start;
    RationalTime end;
};
std::vector<WordGroup> groupsOf(const std::vector<TimedWord> &words, int maxWords, const RationalTime &duration);

// The word being said at `time` (the last one started, so a pause keeps the previous word), -1 before the first.
int activeWord(const std::vector<TimedWord> &words, const RationalTime &time);

// The lines of a caption track as subtitle file entries (SRT/VTT export).
std::vector<SubtitleEntry> entriesOf(const Track &track);
// The same for a part of the timeline (an export between In and Out): the lines that show during `range`, cut to it
// and timed from its start. An empty range is the whole track.
std::vector<SubtitleEntry> entriesOf(const Track &track, const TimeRange &range);
// Entries of a subtitle file as caption lines.
std::vector<CaptionLine> linesOf(const std::vector<SubtitleEntry> &entries);

} // namespace vedit::captions
