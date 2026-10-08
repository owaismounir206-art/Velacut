// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/project/Clip.h"
#include "core/time/RationalTime.h"

#include <QString>
#include <QStringList>
#include <utility>
#include <vector>

namespace velacut {

class SpeedCurveUtil {
public:
    // Preset identifiers matching SPEC §5.5:
    // "montage", "hero", "bullet", "jump", "flash_in", "flash_out"
    static SpeedCurve preset(const QString &id);
    static QStringList presetIds();
    static QString presetTitle(const QString &id, const QString &lang = QStringLiteral("it"));

    // Sorts and validates points, ensuring start at x=0 and end at x=1 with speeds clamped >= 0.05.
    static std::vector<std::pair<double, double>> normalizedPoints(const SpeedCurve &curve);

    // Instantaneous speed v(u) for u in [0, 1] using Fritsch-Carlson PCHIP monotonic spline.
    static double speedAt(const SpeedCurve &curve, double u);

    // Exact analytical integral I(u) = \int_0^u v(t) dt.
    static double integratedTime(const SpeedCurve &curve, double u);

    // Average speed over the full duration: I(1.0).
    static double averageSpeed(const SpeedCurve &curve);

    // Inverts I(u) / I(1.0) == normalizedSource to find timeline progress u in [0, 1].
    static double progressAtSource(const SpeedCurve &curve, double normalizedSource);

    // Keyframe and frame mapping:
    static RationalTime sourceTimeAt(const SpeedCurve &curve,
                                     const RationalTime &sourceIn,
                                     const RationalTime &duration,
                                     const RationalTime &timelineOffset,
                                     bool reversed = false);

    static RationalTime timelineOffsetAtSourceTime(const SpeedCurve &curve,
                                                   const RationalTime &sourceIn,
                                                   const RationalTime &duration,
                                                   const RationalTime &sourceTime,
                                                   bool reversed = false);
};

} // namespace velacut
