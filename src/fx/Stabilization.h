// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "fx/Image.h"

#include <QByteArray>

#include <array>
#include <cstdint>
#include <vector>

namespace vedit::fx {

// How the camera moved from one frame to the next: a translation (shares of the picture's width and height) and a
// rotation (degrees, clockwise on screen). Small grey frames are enough: a camera shake moves the whole picture.
struct CameraStep
{
    float dx = 0.0f;
    float dy = 0.0f;
    float angle = 0.0f;

    friend bool operator==(const CameraStep &, const CameraStep &) = default;
};

// The step between two consecutive grey frames of `width` × `height` (8 bits per pixel, rows packed): translation of
// four regions found by matching, coarse then fine with sub-pixel refinement; their median is the translation, how
// they turn around the centre the rotation.
CameraStep estimateCameraStep(const std::vector<std::uint8_t> &previous, const std::vector<std::uint8_t> &current,
                              int width, int height);

// The steps of a whole shot in a few bytes per frame, for the project file (int16 per value: 1/10000 of the picture,
// 1/1000 of a degree; base64).
QByteArray encodeCameraSteps(const std::vector<CameraStep> &steps);
std::vector<CameraStep> decodeCameraSteps(const QByteArray &base64);

// What to do to each frame to steady the shot: move it back by how far the camera's path is from a smoothed path
// (translation; the rotation is kept in the steps but not corrected yet).
// `strength` 0…1: how much of the shake goes (1 = about a second of smoothing, a steady camera feel). `zoom` is the
// enlargement that keeps the borders out of the picture (the same for the whole shot, at most 1.25).
struct Stabilization
{
    std::vector<CameraStep> corrections; // per frame, same units as the steps
    double zoom = 1.0;
};
Stabilization stabilize(const std::vector<CameraStep> &steps, double framesPerSecond, double strength);

// Draws `source` steadied by `correction` and enlarged by `zoom` around the centre into `destination` (same size).
void applyStabilization(ImageView destination, ConstImageView source, const CameraStep &correction, double zoom);

} // namespace vedit::fx
