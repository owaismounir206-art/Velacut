// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/time/Rational.h"

#include <QString>

#include <cstdint>

namespace velacut {

// A frame position as the editor shows it, in real time: the whole seconds elapsed when the frame starts, and the
// frame's number within that second, counted from the first frame that starts in it. Exact at any rate: at 29.97
// (30000/1001) frame 17982 starts at 599.9994 s, so it is 9:59 and frame 29 of that second (not 10:00.12).
struct TimecodeParts
{
    std::int64_t hours = 0;
    std::int64_t minutes = 0; // 0–59
    std::int64_t seconds = 0; // 0–59
    std::int64_t frames = 0;  // 0 … (frames in that second − 1)
};
TimecodeParts timecodeParts(std::int64_t frame, const Rational &rate);
// The first frame that starts at or after `seconds` seconds (where the ruler puts the tick of that second).
std::int64_t firstFrameOfSecond(std::int64_t seconds, const Rational &rate);
// "MM:SS.FF", or "H:MM:SS.FF" from one hour on.
QString formatTimecode(std::int64_t frame, const Rational &rate);
// "M:SS", or "H:MM:SS": the whole seconds only (labels of the ruler).
QString formatSeconds(std::int64_t frame, const Rational &rate);

} // namespace velacut
