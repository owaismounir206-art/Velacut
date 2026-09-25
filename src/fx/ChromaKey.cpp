// SPDX-License-Identifier: GPL-3.0-or-later
#include "ChromaKey.h"

#include <algorithm>
#include <cmath>

namespace vedit::fx {

namespace {

inline void rgbToUv(float r, float g, float b, float &u, float &v)
{
    // ITU-R BT.601 UV conversion
    u = -0.14713f * r - 0.28886f * g + 0.436f * b;
    v = 0.615f * r - 0.51499f * g - 0.10001f * b;
}

} // namespace

void applyChromaKey(ImageView image, const ChromaKeySettings &settings, int rowBegin, int rowEnd)
{
    const int endRow = std::min(rowEnd, image.height);
    const float keyR = settings.keyR / 255.0f;
    const float keyG = settings.keyG / 255.0f;
    const float keyB = settings.keyB / 255.0f;
    float keyU = 0.0f;
    float keyV = 0.0f;
    rgbToUv(keyR, keyG, keyB, keyU, keyV);

    const float similarity = static_cast<float>(std::clamp(settings.similarity, 0.0, 1.0));
    const float smoothness = static_cast<float>(std::clamp(settings.smoothness, 0.0, 1.0));
    const float spill = static_cast<float>(std::clamp(settings.spill, 0.0, 1.0));

    const bool isGreenKey = (settings.keyG > settings.keyR && settings.keyG > settings.keyB);
    const bool isBlueKey = (settings.keyB > settings.keyR && settings.keyB > settings.keyG);

    // Max distance in UV plane is approx 0.8
    constexpr float kMaxUvDist = 0.8f;

    for (int y = std::max(0, rowBegin); y < endRow; ++y) {
        std::uint8_t *pixel = image.row(y);
        for (int x = 0; x < image.width; ++x, pixel += 4) {
            if (pixel[3] == 0) {
                continue;
            }
            const float r = pixel[0] / 255.0f;
            const float g = pixel[1] / 255.0f;
            const float b = pixel[2] / 255.0f;

            float u = 0.0f;
            float v = 0.0f;
            rgbToUv(r, g, b, u, v);

            const float du = u - keyU;
            const float dv = v - keyV;
            const float dist = std::sqrt(du * du + dv * dv) / kMaxUvDist;

            float alphaFactor = 1.0f;
            if (dist < similarity) {
                alphaFactor = 0.0f;
            } else if (smoothness > 0.001f && dist < (similarity + smoothness)) {
                alphaFactor = (dist - similarity) / smoothness;
            }

            pixel[3] = static_cast<std::uint8_t>(std::clamp(std::round(pixel[3] * alphaFactor), 0.0f, 255.0f));

            if (alphaFactor < 0.999f && spill > 0.0f) {
                if (isGreenKey) {
                    const float maxOther = std::max(r, b);
                    if (g > maxOther) {
                        const float newG = g * (1.0f - spill) + maxOther * spill;
                        pixel[1] = static_cast<std::uint8_t>(std::clamp(std::round(newG * 255.0f), 0.0f, 255.0f));
                    }
                } else if (isBlueKey) {
                    const float maxOther = std::max(r, g);
                    if (b > maxOther) {
                        const float newB = b * (1.0f - spill) + maxOther * spill;
                        pixel[2] = static_cast<std::uint8_t>(std::clamp(std::round(newB * 255.0f), 0.0f, 255.0f));
                    }
                }
            }
        }
    }
}

} // namespace vedit::fx
