// SPDX-License-Identifier: GPL-3.0-or-later
#include "Analysis.h"

#include <algorithm>
#include <cmath>

namespace vedit::ai {

namespace {

float percentile(std::vector<float> values, double share)
{
    if (values.empty()) {
        return 0.0f;
    }
    const size_t index = std::min(values.size() - 1, static_cast<size_t>(share * static_cast<double>(values.size())));
    std::nth_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(index), values.end());
    return values[index];
}

} // namespace

std::vector<SourceRange> findPauses(const std::vector<float> &levels, int windowsPerSecond, const PauseSettings &settings)
{
    std::vector<SourceRange> pauses;
    if (levels.empty() || windowsPerSecond <= 0) {
        return pauses;
    }
    const float floor = percentile(levels, 0.1);
    const float speech = percentile(levels, 0.9);
    if (speech - floor < 10.0f) {
        return pauses; // all at one level: all speech or all silence, nothing to tell apart
    }
    // A third of the way from the noise floor to the speech, never above −25 dBFS nor under −60 dBFS.
    const float threshold = std::clamp(floor + (speech - floor) / 3.0f, -60.0f, -25.0f);
    const Rational rate(windowsPerSecond);
    const auto windows = [&](const Rational &seconds) {
        return (seconds * rate).toInteger(Rounding::NearestEven);
    };
    const std::int64_t minimum = std::max<std::int64_t>(1, windows(settings.minimumSeconds));
    const std::int64_t keep = windows(settings.keepSeconds);
    const auto count = static_cast<std::int64_t>(levels.size());
    std::int64_t i = 0;
    while (i < count) {
        if (levels[static_cast<size_t>(i)] >= threshold) {
            ++i;
            continue;
        }
        std::int64_t end = i;
        while (end < count && levels[static_cast<size_t>(end)] < threshold) {
            ++end;
        }
        if (end - i >= minimum) {
            const std::int64_t from = i == 0 ? 0 : i + keep;
            const std::int64_t to = end == count ? count : end - keep;
            if (to > from) {
                pauses.push_back(SourceRange{RationalTime(from, rate), RationalTime(to, rate)});
            }
        }
        i = end;
    }
    return pauses;
}

std::vector<double> findSceneCuts(const std::vector<float> &differences, const std::vector<double> &times,
                                  double minimumSeconds)
{
    std::vector<double> cuts;
    const size_t count = std::min(differences.size(), times.size());
    constexpr size_t kAround = 15; // frames looked at on each side
    double last = -1e9;
    for (size_t i = 1; i < count; ++i) {
        const float value = differences[i];
        if (value < 0.12f) {
            continue; // too similar to be another shot
        }
        // Stands out: well above the motion of the frames around it (a pan or a whip is not a cut).
        double sum = 0.0;
        double nearby = 0.0; // the frames right next to it: a cut is a single jump, fast motion is several
        size_t n = 0;
        for (size_t j = i > kAround ? i - kAround : 1; j < std::min(count, i + kAround + 1); ++j) {
            if (j != i) {
                sum += differences[j];
                if (j + 2 >= i && j <= i + 2) {
                    nearby = std::max<double>(nearby, differences[j]);
                }
                ++n;
            }
        }
        const double mean = n > 0 ? sum / static_cast<double>(n) : 0.0;
        if (value < 3.0 * mean + 0.05 || value < 2.0 * nearby) {
            continue;
        }
        if (times[i] - last < minimumSeconds || times[i] < minimumSeconds) {
            continue;
        }
        cuts.push_back(times[i]);
        last = times[i];
    }
    return cuts;
}

} // namespace vedit::ai
