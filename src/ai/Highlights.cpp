// SPDX-License-Identifier: GPL-3.0-or-later
#include "Highlights.h"

#include "ai/Analysis.h"

#include <algorithm>
#include <cmath>

namespace vedit::ai {

namespace {

constexpr double kShortest = 3.0;
constexpr double kLongest = 15.0;

float percentile(std::vector<float> values, double share)
{
    if (values.empty()) {
        return -100.0f;
    }
    const size_t index = std::min(values.size() - 1, static_cast<size_t>(share * static_cast<double>(values.size())));
    std::nth_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(index), values.end());
    return values[index];
}

} // namespace

std::vector<Span> highlightSegments(const HighlightInput &input)
{
    std::vector<Span> segments;
    if (input.seconds <= 0.0) {
        return segments;
    }
    // Where a piece may start or end: scene changes and the middle of pauses.
    std::vector<double> edges{0.0, input.seconds};
    for (const double cut : input.cuts) {
        if (cut > 0.0 && cut < input.seconds) {
            edges.push_back(cut);
        }
    }
    for (const SourceRange &pause : findPauses(input.levels, input.levelsPerSecond)) {
        const double middle = (pause.start.seconds().toDouble() + pause.end.seconds().toDouble()) / 2.0;
        if (middle > 0.0 && middle < input.seconds) {
            edges.push_back(middle);
        }
    }
    // And where the sound clearly changes (a burst of laughter starts or ends): a jump of more than 8 dB between the
    // half second before and the half second after, the strongest within a second.
    const int half = std::max(1, input.levelsPerSecond / 2);
    const auto count = static_cast<int>(input.levels.size());
    std::vector<double> prefix(static_cast<size_t>(count) + 1, 0.0);
    for (int k = 0; k < count; ++k) {
        prefix[static_cast<size_t>(k) + 1] = prefix[static_cast<size_t>(k)] + input.levels[static_cast<size_t>(k)];
    }
    const auto jumpAt = [&](int k) {
        if (k < half || k + half > count) {
            return 0.0;
        }
        const double before = (prefix[static_cast<size_t>(k)] - prefix[static_cast<size_t>(k - half)]) / half;
        const double after = (prefix[static_cast<size_t>(k + half)] - prefix[static_cast<size_t>(k)]) / half;
        return std::abs(after - before);
    };
    for (int k = half; k + half <= count; k += std::max(1, input.levelsPerSecond / 20)) {
        const double jump = jumpAt(k);
        if (jump < 8.0) {
            continue;
        }
        bool strongest = true;
        for (int other = std::max(half, k - 2 * half); other <= std::min(count - half, k + 2 * half) && strongest; ++other) {
            strongest = jumpAt(other) <= jump || (jumpAt(other) == jump && other >= k);
        }
        if (strongest) {
            edges.push_back(static_cast<double>(k) / input.levelsPerSecond);
        }
    }
    std::sort(edges.begin(), edges.end());
    // Pieces of 3–15 s: short ones joined to the next, long ones cut evenly.
    double start = edges.front();
    for (size_t i = 1; i < edges.size(); ++i) {
        const bool last = i + 1 == edges.size();
        if (edges[i] - start < kShortest && !last) {
            continue;
        }
        const double length = edges[i] - start;
        const int parts = std::max(1, static_cast<int>(std::ceil(length / kLongest)));
        for (int p = 0; p < parts; ++p) {
            segments.push_back(Span{start + length * p / parts, start + length * (p + 1) / parts, 0.0});
        }
        start = edges[i];
    }
    // Scores.
    const float median = percentile(input.levels, 0.5);
    const float loud = std::max(median + 3.0f, percentile(input.levels, 0.9));
    for (Span &segment : segments) {
        const auto first = static_cast<size_t>(std::max(0.0, segment.from * input.levelsPerSecond));
        const auto last = std::min(input.levels.size(), static_cast<size_t>(segment.to * input.levelsPerSecond));
        double sum = 0.0;
        for (size_t k = first; k < last; ++k) {
            sum += input.levels[k];
        }
        const double level = last > first ? sum / static_cast<double>(last - first) : -100.0;
        const double loudness = std::clamp((level - median) / std::max(1.0f, loud - median), -1.0, 1.5);
        double speech = 0.0;
        if (!input.words.empty()) {
            int said = 0;
            for (const auto &[from, to] : input.words) {
                said += from >= segment.from && from < segment.to ? 1 : 0;
            }
            speech = std::min(1.0, said / std::max(0.5, segment.length()) / 3.0); // about 3 words a second is lively
        }
        const bool atCut = std::any_of(input.cuts.begin(), input.cuts.end(),
                                       [&segment](double cut) { return std::abs(cut - segment.from) < 0.05; });
        segment.score = 0.6 * loudness + (input.words.empty() ? 0.0 : 0.4 * speech) + (atCut ? 0.1 : 0.0);
    }
    return segments;
}

std::vector<Span> findHighlights(const HighlightInput &input, double targetSeconds)
{
    std::vector<Span> segments = highlightSegments(input);
    std::vector<Span> chosen;
    std::stable_sort(segments.begin(), segments.end(), [](const Span &a, const Span &b) { return a.score > b.score; });
    double total = 0.0;
    for (const Span &segment : segments) {
        if (total >= targetSeconds) {
            break;
        }
        chosen.push_back(segment);
        total += segment.length();
    }
    std::sort(chosen.begin(), chosen.end(), [](const Span &a, const Span &b) { return a.from < b.from; });
    return chosen;
}

std::vector<Span> findShortClips(const HighlightInput &input, int count, double clipSeconds)
{
    const std::vector<Span> segments = highlightSegments(input);
    const double target = std::clamp(clipSeconds, 15.0, 60.0);
    // Every run of consecutive pieces of about the target length, with its length-weighted score.
    std::vector<Span> windows;
    for (size_t i = 0; i < segments.size(); ++i) {
        double length = 0.0;
        double weighted = 0.0;
        for (size_t j = i; j < segments.size(); ++j) {
            length += segments[j].length();
            weighted += segments[j].score * segments[j].length();
            if (length >= target * 0.8) {
                if (length <= 60.0) {
                    windows.push_back(Span{segments[i].from, segments[j].to, weighted / length});
                }
                break;
            }
        }
    }
    std::stable_sort(windows.begin(), windows.end(), [](const Span &a, const Span &b) { return a.score > b.score; });
    std::vector<Span> chosen;
    for (const Span &window : windows) {
        if (static_cast<int>(chosen.size()) >= count) {
            break;
        }
        const bool overlaps = std::any_of(chosen.begin(), chosen.end(),
                                          [&window](const Span &c) { return window.from < c.to && window.to > c.from; });
        if (!overlaps) {
            chosen.push_back(window);
        }
    }
    return chosen;
}

} // namespace vedit::ai
