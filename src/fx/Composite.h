// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "fx/Image.h"

namespace vedit::fx {

enum class BlendMode
{
    Normal,
    Lighten,
    Screen,
    Multiply,
    Overlay,
    SoftLight,
    HardLight,
    Difference,
    Darken,
    Color,
    Luminosity,
    Add,
    ColorDodge,
    ColorBurn,
    Exclusion,
    Hue,
    Saturation,
};

// CPU reference of the "normal" blend: `source` over `destination` (same size), straight alpha, with an extra
// opacity 0..255. Integer arithmetic with rounding: the result is exact and identical on every machine, so the
// optional GPU path can be checked against it (SPEC 1bis rule 1). Processes rows [rowBegin, rowEnd), so callers
// can split the image across threads.
void compositeOver(ImageView destination, ConstImageView source, int opacity, int rowBegin, int rowEnd);

// Convenience: the whole image.
inline void compositeOver(ImageView destination, ConstImageView source, int opacity = 255)
{
    compositeOver(destination, source, opacity, 0, destination.height);
}

// Composites `source` over `destination` using the specified `mode` and straight alpha compositing
// conforming to W3C Compositing and Blending Level 1.
void compositeBlend(ImageView destination, ConstImageView source, BlendMode mode, int opacity, int rowBegin, int rowEnd);

inline void compositeBlend(ImageView destination, ConstImageView source, BlendMode mode, int opacity = 255)
{
    compositeBlend(destination, source, mode, opacity, 0, destination.height);
}

} // namespace vedit::fx
