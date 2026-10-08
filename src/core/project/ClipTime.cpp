// SPDX-License-Identifier: GPL-3.0-or-later
#include "ClipTime.h"
#include "SpeedCurve.h"

#include <algorithm>
#include <cmath>

namespace velacut {

namespace {

// `time` × `factor` on the grid of `time`, to the nearest frame.
RationalTime scaled(const RationalTime &time, double factor)
{
    return RationalTime(std::llround(static_cast<double>(time.value()) * factor), time.rate());
}

} // namespace

RationalTime keyframeTime(const RationalTime &sourceIn, double speed, bool reversed, const RationalTime &duration,
                          const RationalTime &offset)
{
    const Rational rate = offset.rate();
    const RationalTime in = sourceIn.rescaled(rate, Rounding::NearestEven);
    const RationalTime length = duration.rescaled(rate, Rounding::NearestEven);
    // Backwards, the first frame shows the end of the material used.
    const RationalTime played = reversed ? RationalTime(std::max<std::int64_t>(0, length.value() - 1 - offset.value()), rate)
                                         : offset;
    return in + scaled(played, speed > 0 ? speed : 1.0);
}

RationalTime keyframeTime(const Clip &clip, const RationalTime &offset)
{
    if (const MediaClipData *media = clip.media()) {
        if (media->curve && !media->curve->points.empty()) {
            return SpeedCurveUtil::sourceTimeAt(*media->curve, media->sourceIn, clip.duration, offset, media->reversed);
        }
        return keyframeTime(media->sourceIn, media->speed, media->reversed, clip.duration, offset);
    }
    return offset;
}

RationalTime offsetOfKeyframeTime(const Clip &clip, const RationalTime &time)
{
    const Rational rate = clip.duration.rate();
    const std::int64_t length = clip.duration.value();
    std::int64_t offset = time.rescaled(rate, Rounding::NearestEven).value();
    if (const MediaClipData *media = clip.media()) {
        if (media->curve && !media->curve->points.empty()) {
            return SpeedCurveUtil::timelineOffsetAtSourceTime(*media->curve, media->sourceIn, clip.duration, time, media->reversed);
        }
        const double speed = media->speed <= 0 ? 1.0 : media->speed;
        const std::int64_t played =
            std::llround(static_cast<double>((time - media->sourceIn).rescaled(rate, Rounding::NearestEven).value()) / speed);
        offset = media->reversed ? length - 1 - played : played;
    }
    return RationalTime(std::clamp<std::int64_t>(offset, 0, length), rate);
}

} // namespace velacut
