// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace velacut::fx {

struct LoudnessResult
{
    double integratedLufs = -70.0;
    double momentaryMaxLufs = -70.0;
    double shortTermMaxLufs = -70.0;
    double truePeakDb = -100.0;
};

// Measures ITU-R BS.1770-4 / EBU R128 integrated loudness for interleaved float audio samples.
LoudnessResult measureLoudness(const float *samples, int channels, int sampleRate, std::int64_t numFrames);

inline LoudnessResult measureLoudness(const std::vector<float> &samples, int channels, int sampleRate)
{
    if (channels <= 0 || samples.empty()) {
        return LoudnessResult{};
    }
    return measureLoudness(samples.data(), channels, sampleRate, static_cast<std::int64_t>(samples.size() / channels));
}

// Calculates dB gain adjustment needed to reach target LUFS (default -14 LUFS for YouTube / TikTok / Reels).
inline double gainAdjustmentForTargetLufs(double currentLufs, double targetLufs = -14.0)
{
    if (currentLufs <= -70.0) {
        return 0.0;
    }
    return targetLufs - currentLufs;
}

} // namespace velacut::fx
