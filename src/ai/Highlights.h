// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <utility>
#include <vector>

namespace vedit::ai {

// What the highlights of a long video are found from (SPEC §5.12: audio, scenes and speech): the loudness of its sound
// (dBFS, `levelsPerSecond` a second), its scene changes and, if known, the times of its words (seconds of the file).
struct HighlightInput
{
    std::vector<float> levels;
    int levelsPerSecond = 100;
    std::vector<double> cuts;
    std::vector<std::pair<double, double>> words;
    double seconds = 0.0;
};

struct Span
{
    double from = 0.0;
    double to = 0.0;
    double score = 0.0;
    double length() const { return to - from; }
};

// The pieces of the video between scene changes and pauses, 3–15 s each, scored: louder than usual (laughter,
// cheering, emphasis) and full of speech is better, silence worse.
std::vector<Span> highlightSegments(const HighlightInput &input);

// "Highlights": the best pieces adding up to about `targetSeconds` (never under it by more than a piece), in their
// order in the video.
std::vector<Span> findHighlights(const HighlightInput &input, double targetSeconds);

// "Long video to short clips": up to `count` clips of about `clipSeconds` (15–60 s), each a run of consecutive pieces
// with a good score, not overlapping; best first.
std::vector<Span> findShortClips(const HighlightInput &input, int count, double clipSeconds);

} // namespace vedit::ai
