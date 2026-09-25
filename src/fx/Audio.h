// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>

namespace vedit::fx {

// Volume of a clip's audio (SPEC §5.9): a gain ramping linearly from `gainStart` to `gainEnd` (linear factors) over
// the block, and a constant-power pan −1 (left) … +1 (right) for stereo. Interleaved float samples, in place.
// Returns the peak of the result (0…1+), for level meters.
float applyGain(float *samples, int channels, int frames, float gainStart, float gainEnd, float pan);

// dB → linear factor (−60 dB and below = silence).
float dbToGain(double db);

} // namespace vedit::fx
