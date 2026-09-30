// SPDX-License-Identifier: GPL-3.0-or-later
#include "BeatDetection.h"

#include <algorithm>
#include <cmath>

namespace vedit::engine {

std::vector<double> detectBeats(const Spectrum &spectrum, double sensitivity, double minInterval)
{
    std::vector<double> beats;
    const int frames = spectrum.frameCount();
    const int bands = spectrum.bands;
    if (frames < 3 || bands <= 0 || spectrum.framesPerSecond <= 0) {
        return beats;
    }
    const auto *levels = reinterpret_cast<const unsigned char *>(spectrum.levels.constData());
    // Onset strength: how much the bands rose since the previous frame (falls are ignored).
    std::vector<double> flux(static_cast<size_t>(frames), 0.0);
    for (int f = 1; f < frames; ++f) {
        double rise = 0.0;
        for (int b = 0; b < bands; ++b) {
            rise += std::max(0, levels[f * bands + b] - levels[(f - 1) * bands + b]);
        }
        flux[static_cast<size_t>(f)] = rise / (255.0 * bands);
    }
    const int half = std::max(2, spectrum.framesPerSecond / 2);
    const double k = 1.5 / std::clamp(sensitivity, 0.1, 10.0);
    double last = -1e9;
    for (int f = 1; f + 1 < frames; ++f) {
        const double value = flux[static_cast<size_t>(f)];
        if (value < flux[static_cast<size_t>(f) - 1] || value < flux[static_cast<size_t>(f) + 1]) {
            continue;
        }
        const int from = std::max(0, f - half);
        const int to = std::min(frames - 1, f + half);
        double mean = 0.0;
        for (int i = from; i <= to; ++i) {
            mean += flux[static_cast<size_t>(i)];
        }
        mean /= (to - from + 1);
        double variance = 0.0;
        for (int i = from; i <= to; ++i) {
            variance += std::pow(flux[static_cast<size_t>(i)] - mean, 2.0);
        }
        const double deviation = std::sqrt(variance / (to - from + 1));
        // The small absolute floor keeps silence and steady tones free of beats.
        if (value <= mean + k * deviation || value < 0.01) {
            continue;
        }
        const double seconds = static_cast<double>(f) / spectrum.framesPerSecond;
        if (seconds - last >= minInterval) {
            beats.push_back(seconds);
            last = seconds;
        }
    }
    return beats;
}

} // namespace vedit::engine
