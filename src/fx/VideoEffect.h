// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "fx/Image.h"

#include <QString>

#include <optional>

namespace velacut::fx {

// The CPU reference kernels of the video effects library (SPEC §5.11, docs/EFFECT_FORMAT.md §9). A preset of the
// library names a kernel and its parameters; several presets share a kernel with different parameters.
// Every kernel works in place on a straight-alpha RGBA image at any size (sizes are relative to the image height, so the
// preview at a reduced size and the export look the same) and is deterministic: the same time gives the same picture.
enum class EffectKernel
{
    Blur,            // amount: radius
    ZoomBlur,        // amount: length of the radial streaks
    SpinBlur,        // amount: angle of the rotational blur
    DirectionalBlur, // amount, angle
    Glow,            // amount: strength, size: threshold (0 = everything glows)
    Dreamy,          // amount: soft light mix of a blurred copy
    RgbSplit,        // amount: offset, angle, speed: pulsing (0 = steady)
    Glitch,          // amount, speed: slices shifted and colour split, changing `speed`×10 times a second
    BlockGlitch,     // amount, speed: blocks moved and channels swapped
    Scanlines,       // amount: darkness, size: line spacing
    Vhs,             // amount, speed: colour bleed, noise, tracking band, wobble
    Noise,           // amount: animated grain, speed
    Pixelate,        // size: block size
    Mirror,          // count: 0 left→right, 1 right→left, 2 top→bottom, 3 four quarters
    Kaleidoscope,    // count: segments, speed: rotation
    Shake,           // amount, speed: camera shake
    ZoomPulse,       // amount, speed: rhythmic zoom
    Strobe,          // amount, speed: flashes of `color`
    Invert,          // amount
    Posterize,       // amount: fewer levels
    Edges,           // amount: white edges on black
    Sketch,          // amount: pencil drawing
    Emboss,          // amount
    PulseVignette,   // amount, speed, color
    HueCycle,        // speed: turns per second, amount: saturation boost
    Duotone,         // color (shadows), color2 (highlights)
    Thermal,         // amount
    NightVision,     // amount
    OldFilm,         // amount: sepia, grain, scratches, flicker
    LightLeak,       // amount, speed, color
    Rain,            // amount: density, speed
    Snow,            // amount: density, speed
    Sparkles,        // amount: density, speed, color
    Bokeh,           // amount: density, speed, color
    Wave,            // amount, speed, size: wavelength, angle (0 horizontal ripples, 90 vertical)
    Swirl,           // amount (negative: the other way), speed
    Bulge,           // amount (negative: pinch)
    Grid,            // count: copies per side (2–4)
    Letterbox,       // size: bar height, color
    Halftone,        // size: dot spacing, color (ink)
    Dither,          // size: pixel size, count: levels per channel
    NeonEdges,       // amount, color
    TiltShift,       // amount: blur, size: sharp band height
    Prism,           // amount: offset of the coloured copies
    LensAberration,  // amount: colour fringes growing towards the corners
    Spotlight,       // amount: darkness, speed: movement, size: radius
    Flicker,         // amount, speed: brightness flicker
    ColorShift,      // amount: RGB channels swapped towards `count` (0 GBR, 1 BRG)
};

// "blur", "zoomBlur", … (the enumerator's name with a lower-case first letter).
std::optional<EffectKernel> effectKernel(const QString &name);
QString effectKernelName(EffectKernel kernel);
// Kernels whose picture changes with time even with fixed parameters (the preview loops them).
bool isAnimatedKernel(EffectKernel kernel);

struct EffectRgb
{
    double r = 1.0, g = 1.0, b = 1.0; // 0–1
};

struct VideoEffectParams
{
    double amount = 0.5;
    double size = 0.5;
    double speed = 1.0;
    double angle = 0.0; // degrees
    int count = 0;
    EffectRgb color{1.0, 1.0, 1.0};
    EffectRgb color2{1.0, 0.8, 0.2};
    double time = 0.0; // seconds from the start of the clip
};

// Applies `kernel` to `image` in place.
void renderVideoEffect(EffectKernel kernel, const ImageView &image, const VideoEffectParams &params);

} // namespace velacut::fx
