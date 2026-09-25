// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "fx/Image.h"
#include <cstdint>

namespace vedit::fx {

struct ChromaKeySettings
{
    std::uint8_t keyR = 0;
    std::uint8_t keyG = 255;
    std::uint8_t keyB = 0;
    double similarity = 0.4; // 0..1 threshold
    double smoothness = 0.1; // 0..1 feather / transition zone
    double spill = 0.5;      // 0..1 spill suppression
};

// Applies chroma keying in place on RGBA image: evaluates color distance in UV plane,
// modulates alpha, and suppresses green/blue spill.
void applyChromaKey(ImageView image, const ChromaKeySettings &settings, int rowBegin, int rowEnd);

inline void applyChromaKey(ImageView image, const ChromaKeySettings &settings)
{
    applyChromaKey(image, settings, 0, image.height);
}

} // namespace vedit::fx
