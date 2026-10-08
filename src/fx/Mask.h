// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "fx/Image.h"

#include <vector>

namespace velacut::fx {

enum class MaskShape
{
    Linear,
    Mirror,
    Circle,
    Rectangle,
    Heart,
    Star,
    Path,
};

struct MaskPoint
{
    double x = 0.0;
    double y = 0.0;
    double inX = 0.0;
    double inY = 0.0;
    double outX = 0.0;
    double outY = 0.0;
};

struct MaskParams
{
    MaskShape shape = MaskShape::Rectangle;
    double centerX = 0.0;
    double centerY = 0.0;
    double sizeX = 0.5;
    double sizeY = 0.5;
    double rotation = 0.0;  // degrees clockwise
    double roundness = 0.0; // 0..1 (for rectangle)
    double feather = 0.0;   // 0..1 (fraction of dimension)
    bool invert = false;
    std::vector<MaskPoint> points;
};

// Evaluates the mask alpha [0..1] at normalized source coordinates (u, v) in [-0.5, 0.5].
double evaluateMask(const MaskParams &mask, double u, double v, int width, int height);

// Applies a list of masks combined by union in source coordinate space to the image.
// Modifies image alpha channel in place (straight alpha).
void applyMasks(ImageView image, const std::vector<MaskParams> &masks, int rowBegin, int rowEnd);

inline void applyMasks(ImageView image, const std::vector<MaskParams> &masks)
{
    applyMasks(image, masks, 0, image.height);
}

} // namespace velacut::fx
