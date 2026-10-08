// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <vector>

namespace velacut::fx {

// Where the subject of a picture is, for "Auto reframe" (SPEC §5.12, 0bis rule 9): what moves and what has detail,
// slightly favouring the centre, from two consecutive grey frames of `width` × `height`. x and y are shares of the
// picture (0…1); `weight` says how sure (0 = nothing stands out: keep the previous framing).
struct SubjectPoint
{
    float x = 0.5f;
    float y = 0.5f;
    float weight = 0.0f;
};
SubjectPoint findSubject(const std::vector<std::uint8_t> &previous, const std::vector<std::uint8_t> &current, int width,
                         int height);

// A steady path for the framing: unsure points lean on their neighbours, then a Gaussian of `sigma` samples smooths
// the rest (a camera operator follows the subject calmly, without jerks).
std::vector<SubjectPoint> smoothSubjectPath(const std::vector<SubjectPoint> &points, double sigma);

} // namespace velacut::fx
