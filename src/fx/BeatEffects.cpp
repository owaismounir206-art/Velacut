// SPDX-License-Identifier: GPL-3.0-or-later
#include "BeatEffects.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace velacut::fx {

namespace {

inline uint8_t sampleBilinear(const uint8_t *src, int w, int h, double x, double y, int ch)
{
    const double cx = std::clamp(x, 0.0, static_cast<double>(w - 1));
    const double cy = std::clamp(y, 0.0, static_cast<double>(h - 1));

    const int x0 = static_cast<int>(cx);
    const int y0 = static_cast<int>(cy);
    const int x1 = std::min(x0 + 1, w - 1);
    const int y1 = std::min(y0 + 1, h - 1);

    const double fx = cx - x0;
    const double fy = cy - y0;

    const double v00 = src[(y0 * w + x0) * 4 + ch];
    const double v10 = src[(y0 * w + x1) * 4 + ch];
    const double v01 = src[(y1 * w + x0) * 4 + ch];
    const double v11 = src[(y1 * w + x1) * 4 + ch];

    const double top = v00 * (1.0 - fx) + v10 * fx;
    const double bot = v01 * (1.0 - fx) + v11 * fx;
    const double val = top * (1.0 - fy) + bot * fy;

    return static_cast<uint8_t>(std::clamp(std::round(val), 0.0, 255.0));
}

} // namespace

double computeBeatPulse(double timeSeconds, const std::vector<double> &beats, double decaySeconds)
{
    if (decaySeconds <= 0.001) {
        decaySeconds = 0.18;
    }
    // Binary search for greatest beat <= timeSeconds
    auto it = std::upper_bound(beats.begin(), beats.end(), timeSeconds);
    if (it == beats.begin()) {
        return 0.0;
    }
    --it;
    const double delta = timeSeconds - *it;
    if (delta >= 0.0 && delta < decaySeconds) {
        return std::exp(-4.0 * delta / decaySeconds);
    }
    return 0.0;
}

void applyBeatFlash(uint8_t *rgba, int width, int height, double pulse, double intensity)
{
    const double strength = std::clamp(pulse * intensity, 0.0, 1.0);
    if (strength <= 0.001) {
        return;
    }

    const int totalPixels = width * height;
    const double invStrength = 1.0 - strength;
    const double flashVal = 255.0 * strength;

    for (int i = 0; i < totalPixels; ++i) {
        uint8_t *px = rgba + i * 4;
        px[0] = static_cast<uint8_t>(std::clamp(px[0] * invStrength + flashVal, 0.0, 255.0));
        px[1] = static_cast<uint8_t>(std::clamp(px[1] * invStrength + flashVal, 0.0, 255.0));
        px[2] = static_cast<uint8_t>(std::clamp(px[2] * invStrength + flashVal, 0.0, 255.0));
    }
}

void applyBeatZoom(uint8_t *dst, const uint8_t *src, int width, int height, double pulse,
                   double zoomFactor)
{
    const double zoom = 1.0 + std::max(0.0, pulse * zoomFactor);
    if (zoom <= 1.001 || width <= 0 || height <= 0) {
        if (dst != src) {
            std::memcpy(dst, src, static_cast<size_t>(width * height * 4));
        }
        return;
    }

    const double cx = width * 0.5;
    const double cy = height * 0.5;
    const double invZoom = 1.0 / zoom;

    for (int y = 0; y < height; ++y) {
        const double srcY = (static_cast<double>(y) - cy) * invZoom + cy;
        for (int x = 0; x < width; ++x) {
            const double srcX = (static_cast<double>(x) - cx) * invZoom + cx;
            uint8_t *d = dst + (y * width + x) * 4;
            d[0] = sampleBilinear(src, width, height, srcX, srcY, 0);
            d[1] = sampleBilinear(src, width, height, srcX, srcY, 1);
            d[2] = sampleBilinear(src, width, height, srcX, srcY, 2);
            d[3] = sampleBilinear(src, width, height, srcX, srcY, 3);
        }
    }
}

void applyBeatShake(uint8_t *dst, const uint8_t *src, int width, int height, double pulse,
                    double timeSeconds, double maxShakePixels)
{
    const double amount = pulse * maxShakePixels;
    if (amount <= 0.05 || width <= 0 || height <= 0) {
        if (dst != src) {
            std::memcpy(dst, src, static_cast<size_t>(width * height * 4));
        }
        return;
    }

    constexpr double kPi = 3.14159265358979323846;
    const double dx = amount * std::sin(2.0 * kPi * 28.0 * timeSeconds);
    const double dy = amount * std::cos(2.0 * kPi * 22.0 * timeSeconds);

    for (int y = 0; y < height; ++y) {
        const double srcY = static_cast<double>(y) - dy;
        for (int x = 0; x < width; ++x) {
            const double srcX = static_cast<double>(x) - dx;
            uint8_t *d = dst + (y * width + x) * 4;
            d[0] = sampleBilinear(src, width, height, srcX, srcY, 0);
            d[1] = sampleBilinear(src, width, height, srcX, srcY, 1);
            d[2] = sampleBilinear(src, width, height, srcX, srcY, 2);
            d[3] = sampleBilinear(src, width, height, srcX, srcY, 3);
        }
    }
}

} // namespace velacut::fx
