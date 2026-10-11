// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "fx/Image.h"

namespace velacut::fx {

enum class BoxRounding
{
    Truncate, // sum / window
    Nearest,  // (sum + window / 2) / window
};

// `iterations` box blurs of radius `radius` each way, in place (three are close to a Gaussian): sliding sums with
// clamped edges, rows in parallel bands, columns in blocks of 16 contiguous pixels (whole cache lines instead of one
// strided column at a time), buffers reused between frames. Bit-exact with the plain per-channel implementation it
// replaces (tst_kernels compares them).
void boxBlurPasses(const ImageView &image, int radius, int iterations, BoxRounding rounding);

// Three box blurs of radius `radius`, truncated (the blur of the video effects). From radius 8 it is computed on a copy
// 2× smaller (from 24: 4× smaller), blurred at the radius scaled down and enlarged back bilinearly: a quarter (a
// sixteenth) of the pixels, a picture within PSNR 42 dB of the exact one on photos (D-59, tst_kernels). Below 8 it is
// the exact boxBlurPasses().
void blur3(const ImageView &image, int radius);

} // namespace velacut::fx
