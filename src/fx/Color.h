// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "fx/Image.h"

#include <array>
#include <functional>
#include <cstdint>
#include <utility>
#include <vector>

namespace vedit::fx {

// Colour adjustments and "looks" (filters). Every value 0 = unchanged; most range −1…+1.
// Filters of the library are sets of these values (data, not code): one kernel renders them all.
struct ColorAdjust
{
    double exposure = 0;    // stops, −3…+3
    double brightness = 0;  // midtones
    double contrast = 0;
    double highlights = 0;
    double shadows = 0;
    double whites = 0;
    double blacks = 0;
    double saturation = 0;  // −1 = black and white
    double vibrance = 0;    // saturation weighted towards the less saturated colours
    double temperature = 0; // + warmer, − cooler
    double tint = 0;        // + magenta, − green
    double fade = 0;        // 0…1: lifted blacks, matte look
    // Split toning: colour offsets (−1…+1 per channel) added to shadows / highlights, times `splitAmount`.
    std::array<double, 3> shadowTone{0, 0, 0};
    std::array<double, 3> highlightTone{0, 0, 0};
    double splitAmount = 0;
    // Master tone curve: (input, output) points in 0…1, sorted; empty = identity.
    std::vector<std::pair<double, double>> curve;

    bool isIdentity() const;
    friend bool operator==(const ColorAdjust &, const ColorAdjust &) = default;
};

// The adjustments as a 3D lookup table (33³ entries), applied with trilinear interpolation.
class ColorLut
{
public:
    static constexpr int kSize = 33;

    ColorLut(); // identity
    explicit ColorLut(const ColorAdjust &adjust);
    // Any colour transform of RGB 0…1 (adjustments, grade and .cube composed: docs/ARCHITECTURE.md §22).
    explicit ColorLut(const std::function<std::array<double, 3>(const std::array<double, 3> &)> &transform);

    // The colour transform on one RGB value 0…1 (the exact function the table samples).
    static std::array<double, 3> evaluate(const ColorAdjust &adjust, std::array<double, 3> rgb);

    // In place, rows [rowBegin, rowEnd); `intensity` 0…1 mixes original and result. Alpha is kept.
    void apply(ImageView image, double intensity, int rowBegin, int rowEnd) const;
    void apply(ImageView image, double intensity = 1.0) const { apply(image, intensity, 0, image.height); }

private:
    std::vector<std::array<float, 3>> m_table; // [r][g][b] order: index (r * kSize + g) * kSize + b
};

// Darkens (amount < 0) or lightens (amount > 0) the corners, amount −1…+1; `size` 0…1 = how far towards the centre.
void vignette(ImageView image, double amount, double size, int rowBegin, int rowEnd);
// Film grain, amount 0…1, deterministic for a given `seed` (e.g. the frame number).
void grain(ImageView image, double amount, std::uint32_t seed, int rowBegin, int rowEnd);
// Unsharp mask 3×3 from `source` into `destination` (same size, different buffers), amount 0…2.
void sharpen(ImageView destination, ConstImageView source, double amount, int rowBegin, int rowEnd);

// Separable box blur (three passes ≈ Gaussian), radius in pixels, in place on the whole image (not sliced: the
// callers blur small downscaled copies).
void boxBlur(ImageView image, int radius);

} // namespace vedit::fx
