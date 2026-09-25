// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "fx/Color.h"
#include "fx/Image.h"

#include <span>

namespace vedit::fx {

// "Migliora automaticamente" for light and colour (SPEC 0bis rule 9): adjustments computed from frames of a clip
// (exposure towards a middle-grey average, contrast when the tones are squeezed, recovered clipped highlights and
// crushed shadows, white balance by the grey-world assumption, a little vibrance on dull pictures). Gentle by design:
// each correction goes only part of the way and is limited, so a well-exposed, neutral picture stays as it is, and
// the result is plain adjustment values the user can change.
ColorAdjust autoEnhance(std::span<const ConstImageView> frames);

// Gain in dB that brings the loudest peak (0…1 of full scale) to −1 dBFS, limited to −6…+12 dB; 0 when silent.
double autoGainDb(double peak);

} // namespace vedit::fx
