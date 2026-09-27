// SPDX-License-Identifier: GPL-3.0-or-later
#include "MotionBlur.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <vector>

namespace vedit::fx {

namespace {

inline std::array<float, 4> sampleBilinear(const ConstImageView &src, float x, float y)
{
    const int w = src.width;
    const int h = src.height;

    const float cx = std::clamp(x, 0.0f, static_cast<float>(w - 1));
    const float cy = std::clamp(y, 0.0f, static_cast<float>(h - 1));

    const int x0 = static_cast<int>(cx);
    const int y0 = static_cast<int>(cy);
    const int x1 = std::min(x0 + 1, w - 1);
    const int y1 = std::min(y0 + 1, h - 1);

    const float fx = cx - static_cast<float>(x0);
    const float fy = cy - static_cast<float>(y0);

    const std::uint8_t *p00 = src.row(y0) + x0 * 4;
    const std::uint8_t *p10 = src.row(y0) + x1 * 4;
    const std::uint8_t *p01 = src.row(y1) + x0 * 4;
    const std::uint8_t *p11 = src.row(y1) + x1 * 4;

    const float w00 = (1.0f - fx) * (1.0f - fy);
    const float w10 = fx * (1.0f - fy);
    const float w01 = (1.0f - fx) * fy;
    const float w11 = fx * fy;

    return {
        p00[0] * w00 + p10[0] * w10 + p01[0] * w01 + p11[0] * w11,
        p00[1] * w00 + p10[1] * w10 + p01[1] * w01 + p11[1] * w11,
        p00[2] * w00 + p10[2] * w10 + p01[2] * w01 + p11[2] * w11,
        p00[3] * w00 + p10[3] * w10 + p01[3] * w01 + p11[3] * w11
    };
}

} // namespace

void applyMotionBlur(const ImageView &dest, const ConstImageView &src, const MotionBlurSettings &settings, int begin, int end)
{
    const int w = src.width;
    const int h = src.height;
    if (w <= 0 || h <= 0 || dest.width != w || dest.height != h) {
        return;
    }

    const int rBegin = std::clamp(begin, 0, h);
    const int rEnd = std::clamp(end, rBegin, h);
    if (rBegin >= rEnd) {
        return;
    }

    const double intensity = std::clamp(settings.intensity, 0.0, 1.0);
    if (intensity < 1e-4) {
        // Identity copy
        for (int y = rBegin; y < rEnd; ++y) {
            std::memcpy(dest.row(y), src.row(y), static_cast<size_t>(w * 4));
        }
        return;
    }

    const double rad = settings.angle * (M_PI / 180.0);
    const double maxDim = std::max(w, h);
    const double radius = intensity * maxDim * 0.04;
    const float dx = static_cast<float>(std::cos(rad) * radius);
    const float dy = static_cast<float>(std::sin(rad) * radius);

    int samples = std::clamp(settings.samples, 3, 17);
    if ((samples % 2) == 0) {
        samples += 1;
    }

    std::vector<float> tapT(static_cast<size_t>(samples));
    std::vector<float> tapW(static_cast<size_t>(samples));
    float totalW = 0.0f;
    for (int i = 0; i < samples; ++i) {
        const float t = -1.0f + 2.0f * (static_cast<float>(i) / static_cast<float>(samples - 1));
        tapT[static_cast<size_t>(i)] = t;
        const float weight = 1.0f - 0.4f * std::abs(t); // slightly weighted towards center
        tapW[static_cast<size_t>(i)] = weight;
        totalW += weight;
    }
    const float invTotalW = (totalW > 1e-6f) ? (1.0f / totalW) : 1.0f;

    for (int y = rBegin; y < rEnd; ++y) {
        std::uint8_t *dstRow = dest.row(y);
        for (int x = 0; x < w; ++x) {
            float sumR = 0.0f;
            float sumG = 0.0f;
            float sumB = 0.0f;
            float sumA = 0.0f;

            for (int s = 0; s < samples; ++s) {
                const float t = tapT[static_cast<size_t>(s)];
                const float weight = tapW[static_cast<size_t>(s)];
                const float sampleX = static_cast<float>(x) + t * dx;
                const float sampleY = static_cast<float>(y) + t * dy;

                const auto rgba = sampleBilinear(src, sampleX, sampleY);
                sumR += rgba[0] * weight;
                sumG += rgba[1] * weight;
                sumB += rgba[2] * weight;
                sumA += rgba[3] * weight;
            }

            const int offset = x * 4;
            dstRow[offset + 0] = static_cast<std::uint8_t>(std::clamp(std::round(sumR * invTotalW), 0.0f, 255.0f));
            dstRow[offset + 1] = static_cast<std::uint8_t>(std::clamp(std::round(sumG * invTotalW), 0.0f, 255.0f));
            dstRow[offset + 2] = static_cast<std::uint8_t>(std::clamp(std::round(sumB * invTotalW), 0.0f, 255.0f));
            dstRow[offset + 3] = static_cast<std::uint8_t>(std::clamp(std::round(sumA * invTotalW), 0.0f, 255.0f));
        }
    }
}

void applyMotionBlur(const ImageView &view, const MotionBlurSettings &settings)
{
    if (view.width <= 0 || view.height <= 0 || settings.intensity < 1e-4) {
        return;
    }
    const size_t totalBytes = static_cast<size_t>(view.height * view.stride);
    std::vector<std::uint8_t> copy(totalBytes);
    std::memcpy(copy.data(), view.data, totalBytes);
    const ConstImageView src{copy.data(), view.width, view.height, view.stride};
    applyMotionBlur(view, src, settings, 0, view.height);
}

} // namespace vedit::fx
