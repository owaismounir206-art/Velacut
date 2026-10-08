// SPDX-License-Identifier: GPL-3.0-or-later
#include "Audio.h"

#include <algorithm>
#include <cmath>

namespace velacut::fx {

float dbToGain(double db)
{
    return db <= -60.0 ? 0.0f : static_cast<float>(std::pow(10.0, db / 20.0));
}

float applyGain(float *samples, int channels, int frames, float gainStart, float gainEnd, float pan)
{
    float left = 1.0f;
    float right = 1.0f;
    if (channels == 2 && pan != 0.0f) {
        // Constant power: at the centre both channels are at 1 (no level change for the default).
        const float angle = (std::clamp(pan, -1.0f, 1.0f) + 1.0f) * float(M_PI) / 4.0f; // 0…π/2
        left = std::cos(angle) * float(M_SQRT2);
        right = std::sin(angle) * float(M_SQRT2);
        left = std::min(left, 1.0f);
        right = std::min(right, 1.0f);
    }
    float peak = 0.0f;
    for (int i = 0; i < frames; ++i) {
        const float gain = frames > 1 ? gainStart + (gainEnd - gainStart) * i / float(frames - 1) : gainStart;
        for (int c = 0; c < channels; ++c) {
            float &s = samples[static_cast<std::size_t>(i * channels + c)];
            const float channelGain = channels == 2 ? (c == 0 ? left : right) : 1.0f;
            s *= gain * channelGain;
            peak = std::max(peak, std::abs(s));
        }
    }
    return peak;
}

} // namespace velacut::fx
