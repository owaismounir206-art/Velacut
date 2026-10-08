// SPDX-License-Identifier: GPL-3.0-or-later
#include "Enhance.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace velacut::fx {

namespace {

constexpr double kTargetMean = 0.45;  // average of a well-exposed picture (encoded values)
constexpr double kShare = 0.7;        // how much of each measured error is corrected
constexpr int kBins = 256;

double percentile(const std::array<double, kBins> &histogram, double total, double fraction)
{
    double sum = 0;
    for (int i = 0; i < kBins; ++i) {
        sum += histogram[static_cast<size_t>(i)];
        if (sum >= fraction * total) {
            return i / double(kBins - 1);
        }
    }
    return 1.0;
}

// Keeps small corrections out: a picture that is almost right is left alone.
double significant(double value, double threshold)
{
    return std::abs(value) < threshold ? 0.0 : value;
}

} // namespace

ColorAdjust autoEnhance(std::span<const ConstImageView> frames)
{
    std::array<double, kBins> histogram{};
    double total = 0;
    double sumR = 0, sumG = 0, sumB = 0, sumSaturation = 0;
    for (const ConstImageView &frame : frames) {
        // At most ~64×64 samples per frame: statistics, not pixels.
        const int stepX = std::max(1, frame.width / 64);
        const int stepY = std::max(1, frame.height / 64);
        for (int y = 0; y < frame.height; y += stepY) {
            const std::uint8_t *row = frame.row(y);
            for (int x = 0; x < frame.width; x += stepX) {
                const std::uint8_t *p = row + x * 4;
                if (p[3] == 0) {
                    continue; // transparent borders say nothing about the picture
                }
                const double r = p[0] / 255.0, g = p[1] / 255.0, b = p[2] / 255.0;
                const double luma = 0.2126 * r + 0.7152 * g + 0.0722 * b;
                histogram[static_cast<size_t>(std::lround(luma * (kBins - 1)))] += 1;
                sumR += r;
                sumG += g;
                sumB += b;
                const double maxC = std::max({r, g, b});
                sumSaturation += maxC > 1e-6 ? (maxC - std::min({r, g, b})) / maxC : 0.0;
                total += 1;
            }
        }
    }
    ColorAdjust adjust;
    if (total < 1) {
        return adjust;
    }
    double mean = 0;
    for (int i = 0; i < kBins; ++i) {
        mean += histogram[static_cast<size_t>(i)] * i / double(kBins - 1);
    }
    mean /= total;

    // Exposure acts in linear light: encoded values scale by 2^(stops / 2.2).
    if (mean > 0.01) {
        adjust.exposure = std::clamp(significant(kShare * 2.2 * std::log2(kTargetMean / mean), 0.3), -1.0, 1.5);
    }
    const double low = percentile(histogram, total, 0.02);
    const double high = percentile(histogram, total, 0.98);
    const double range = high - low;
    if (range < 0.7) {
        adjust.contrast = std::min(0.35, kShare * (0.8 - range));
    }
    // More than 5 % of the picture burnt out or crushed: bring the detail back (unless exposure already does).
    if (1.0 - percentile(histogram, total, 0.95) < 0.02 && adjust.exposure >= 0.0) {
        adjust.highlights = -0.3;
    }
    if (percentile(histogram, total, 0.05) < 0.02 && adjust.exposure <= 0.0) {
        adjust.shadows = 0.2;
    }

    // Grey world: the average colour of a scene is neutral. temperature scales R by (1 + 0.18 t) and B by (1 − 0.18 t),
    // tint scales G by (1 − 0.12 t) (ColorLut::evaluate).
    const double r = sumR / total, g = sumG / total, b = sumB / total;
    if (r + b > 0.02) {
        adjust.temperature = std::clamp(significant(kShare * (b - r) / (0.18 * (r + b)), 0.08), -0.6, 0.6);
        const double warmR = r * (1.0 + 0.18 * adjust.temperature);
        const double warmB = b * (1.0 - 0.18 * adjust.temperature);
        if (g > 0.01) {
            adjust.tint = std::clamp(significant(kShare * (1.0 - (warmR + warmB) / (2.0 * g)) / 0.12, 0.08), -0.5, 0.5);
        }
    }
    const double saturation = sumSaturation / total;
    if (saturation > 0.03 && saturation < 0.25) { // dull colours (not black and white)
        adjust.vibrance = 0.2;
    }
    return adjust;
}

double autoGainDb(double peak)
{
    if (peak <= 1e-4) {
        return 0.0;
    }
    return std::clamp(-1.0 - 20.0 * std::log10(peak), -6.0, 12.0);
}

} // namespace velacut::fx
