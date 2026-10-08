// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "fx/Image.h"

namespace velacut::fx {

struct MotionBlurSettings {
    double intensity = 0.5; // 0.0 (off) to 1.0 (maximum blur)
    double angle = 0.0;     // Direction in degrees (0 = horizontal, 90 = vertical)
    int samples = 7;        // Number of taps (3..17)
};

// Sliced multi-threaded directional motion blur reading from `src` and writing to `dest`.
void applyMotionBlur(const ImageView &dest, const ConstImageView &src, const MotionBlurSettings &settings, int begin, int end);

// Full image motion blur on straight-alpha RGBA pixels in-place.
void applyMotionBlur(const ImageView &view, const MotionBlurSettings &settings);

} // namespace velacut::fx
