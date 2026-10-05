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

// The lines of a caption track as subtitle file entries (SRT/VTT export).
std::vector<SubtitleEntry> entriesOf(const Track &track);
// Entries of a subtitle file as caption lines.
std::vector<CaptionLine> linesOf(const std::vector<SubtitleEntry> &entries);

} // namespace vedit::captions
