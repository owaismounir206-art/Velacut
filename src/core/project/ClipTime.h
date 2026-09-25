// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/project/Clip.h"

namespace vedit {

// Keyframe time (D-05, docs/FILE_FORMAT.md §5.4), the time of keyframes and clip markers: for media clips the time of
// the source at 1x (it stays on the content when the clip is trimmed, sped up or reversed), for generated clips the
// time from the clip's start. Offsets are timeline time from the clip's start; results are on the grid of `offset`.

// The keyframe time shown `offset` after the start of a clip of `duration` that plays from `sourceIn` at `speed`
// (backwards if `reversed`). The low-level form, for the renderer that knows the numbers but not the clip.
RationalTime keyframeTime(const RationalTime &sourceIn, double speed, bool reversed, const RationalTime &duration,
                          const RationalTime &offset);
RationalTime keyframeTime(const Clip &clip, const RationalTime &offset);

// The offset from the clip's start where keyframe time `time` is shown, clamped to the clip ([0, duration]).
RationalTime offsetOfKeyframeTime(const Clip &clip, const RationalTime &time);

} // namespace vedit
