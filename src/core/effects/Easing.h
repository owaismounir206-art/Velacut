// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>
#include <QStringList>
#include <QStringView>

#include <array>
#include <optional>

namespace vedit {

// Easing of a keyframe segment: a named preset or a custom cubic Bézier (CSS cubic-bezier semantics).
// Named presets use the exact Penner formulas, so evaluation is deterministic and identical everywhere.
class Easing
{
public:
    enum class Preset
    {
        Linear,
        Ease,
        EaseIn,
        EaseOut,
        EaseInOut,
        EaseInQuad,
        EaseOutQuad,
        EaseInOutQuad,
        EaseInCubic,
        EaseOutCubic,
        EaseInOutCubic,
        EaseInQuart,
        EaseOutQuart,
        EaseInOutQuart,
        EaseInQuint,
        EaseOutQuint,
        EaseInOutQuint,
        EaseInSine,
        EaseOutSine,
        EaseInOutSine,
        EaseInExpo,
        EaseOutExpo,
        EaseInOutExpo,
        EaseInCirc,
        EaseOutCirc,
        EaseInOutCirc,
        EaseInBack,
        EaseOutBack,
        EaseInOutBack,
        EaseInElastic,
        EaseOutElastic,
        EaseInOutElastic,
        EaseInBounce,
        EaseOutBounce,
        EaseInOutBounce,
        Custom, // uses bezier()
    };

    Easing() = default; // Linear
    static Easing preset(Preset preset);
    // Returns nullopt unless x1 and x2 are within [0, 1] (the curve must be a function of time).
    static std::optional<Easing> cubicBezier(double x1, double y1, double x2, double y2);

    Preset presetKind() const noexcept { return m_preset; }
    const std::array<double, 4> &bezier() const noexcept { return m_bezier; }

    // Maps progress in [0, 1] to eased progress (may overshoot [0, 1] for Back/Elastic/custom curves).
    double apply(double progress) const;

    // Preset name as stored in the project file ("easeInOut"); empty for Custom.
    QString name() const;
    static std::optional<Easing> fromName(QStringView name);
    static QStringList presetNames();

    friend bool operator==(const Easing &a, const Easing &b) noexcept = default;

private:
    Preset m_preset = Preset::Linear;
    std::array<double, 4> m_bezier{0.0, 0.0, 1.0, 1.0};
};

} // namespace vedit
