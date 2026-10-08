// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "engine/analysis/Spectrum.h"

#include <vector>

namespace velacut::engine {

// Beats (onsets of the music) in seconds from the start of the audio, from its spectrum: spectral flux (the sum of
// the rises of every band's level) peak-picked above an adaptive threshold (local mean + deviation over ±0.5 s),
// at least `minInterval` apart. `sensitivity` > 1 finds more beats, < 1 only the strongest.
std::vector<double> detectBeats(const Spectrum &spectrum, double sensitivity = 1.0, double minInterval = 0.25);

} // namespace velacut::engine
