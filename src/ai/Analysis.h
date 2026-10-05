// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/time/RationalTime.h"

#include <vector>

namespace vedit::ai {

// A stretch of a media file, in its own time.
struct SourceRange
{
    RationalTime start;
    RationalTime end;

    friend bool operator==(const SourceRange &, const SourceRange &) = default;
};

// "Remove pauses": the quiet stretches of an audio track from its levels (dBFS per 1/`windowsPerSecond` s,
// engine::extractLevels). The threshold follows the recording: between its noise floor and its speech level, so a
// noisy room and a studio both work. A pause is at least `minimumSeconds` long; `keepSeconds` of it stay on each
// side (breathing room), and a pause at the very start or end loses all of its outer side.
struct PauseSettings
{
    Rational minimumSeconds{6, 10};
    Rational keepSeconds{15, 100};
};
std::vector<SourceRange> findPauses(const std::vector<float> &levels, int windowsPerSecond, const PauseSettings &settings = {});

// "Split scenes": the times (seconds of the file, as decoded) where the picture changes to another shot, from the
// difference of each frame with the previous one (engine::extractFrameDifferences). A cut stands out from the
// differences around it and is at least `minimumSeconds` from the previous one.
std::vector<double> findSceneCuts(const std::vector<float> &differences, const std::vector<double> &times,
                                  double minimumSeconds = 0.6);

} // namespace vedit::ai
