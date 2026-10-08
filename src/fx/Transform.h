// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "fx/Image.h"

namespace velacut::fx {

// 2D affine map p' = M p + d (pixel coordinates, y down).
struct Affine
{
    double m11 = 1, m12 = 0, m21 = 0, m22 = 1, dx = 0, dy = 0;

    static Affine translation(double x, double y) { return {1, 0, 0, 1, x, y}; }
    static Affine scaling(double sx, double sy) { return {sx, 0, 0, sy, 0, 0}; }
    // Clockwise on screen (y down), degrees.
    static Affine rotation(double degrees);
    // This map applied after `first`.
    Affine operator*(const Affine &first) const;
    Affine inverted() const;
    void map(double x, double y, double &outX, double &outY) const
    {
        outX = m11 * x + m12 * y + dx;
        outY = m21 * x + m22 * y + dy;
    }
};

// Visible part of the source, in source pixels (crop).
struct SourceWindow
{
    double left = 0, top = 0, right = 0, bottom = 0;
};

// Draws `source` over `destination` through `destinationToSource` (maps the centre of a destination pixel to source
// coordinates), bilinear, with the pixels outside `window` transparent (soft, antialiased edges) and an opacity 0..1.
// Straight alpha in and out; interpolation and blending in premultiplied space (no dark fringes). Rows
// [rowBegin, rowEnd) of the destination. An integer translation is copied exactly.
void drawAffine(ImageView destination, ConstImageView source, const Affine &destinationToSource,
                const SourceWindow &window, double opacity, int rowBegin, int rowEnd);

inline void drawAffine(ImageView destination, ConstImageView source, const Affine &destinationToSource,
                       const SourceWindow &window, double opacity = 1.0)
{
    drawAffine(destination, source, destinationToSource, window, opacity, 0, destination.height);
}

// Bilinear resize of the whole `source` into the whole `destination` (straight alpha, premultiplied filtering).
void resizeBilinear(ImageView destination, ConstImageView source, int rowBegin, int rowEnd);
inline void resizeBilinear(ImageView destination, ConstImageView source)
{
    resizeBilinear(destination, source, 0, destination.height);
}

} // namespace velacut::fx
