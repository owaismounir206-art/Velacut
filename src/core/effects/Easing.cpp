// SPDX-License-Identifier: GPL-3.0-or-later
#include "Easing.h"

#include <cmath>
#include <numbers>

using namespace Qt::StringLiterals;

namespace vedit {

namespace {

struct PresetName
{
    Easing::Preset preset;
    QLatin1StringView name;
};

constexpr PresetName kPresetNames[] = {
    {Easing::Preset::Linear, "linear"_L1},
    {Easing::Preset::Ease, "ease"_L1},
    {Easing::Preset::EaseIn, "easeIn"_L1},
    {Easing::Preset::EaseOut, "easeOut"_L1},
    {Easing::Preset::EaseInOut, "easeInOut"_L1},
    {Easing::Preset::EaseInQuad, "easeInQuad"_L1},
    {Easing::Preset::EaseOutQuad, "easeOutQuad"_L1},
    {Easing::Preset::EaseInOutQuad, "easeInOutQuad"_L1},
    {Easing::Preset::EaseInCubic, "easeInCubic"_L1},
    {Easing::Preset::EaseOutCubic, "easeOutCubic"_L1},
    {Easing::Preset::EaseInOutCubic, "easeInOutCubic"_L1},
    {Easing::Preset::EaseInQuart, "easeInQuart"_L1},
    {Easing::Preset::EaseOutQuart, "easeOutQuart"_L1},
    {Easing::Preset::EaseInOutQuart, "easeInOutQuart"_L1},
    {Easing::Preset::EaseInQuint, "easeInQuint"_L1},
    {Easing::Preset::EaseOutQuint, "easeOutQuint"_L1},
    {Easing::Preset::EaseInOutQuint, "easeInOutQuint"_L1},
    {Easing::Preset::EaseInSine, "easeInSine"_L1},
    {Easing::Preset::EaseOutSine, "easeOutSine"_L1},
    {Easing::Preset::EaseInOutSine, "easeInOutSine"_L1},
    {Easing::Preset::EaseInExpo, "easeInExpo"_L1},
    {Easing::Preset::EaseOutExpo, "easeOutExpo"_L1},
    {Easing::Preset::EaseInOutExpo, "easeInOutExpo"_L1},
    {Easing::Preset::EaseInCirc, "easeInCirc"_L1},
    {Easing::Preset::EaseOutCirc, "easeOutCirc"_L1},
    {Easing::Preset::EaseInOutCirc, "easeInOutCirc"_L1},
    {Easing::Preset::EaseInBack, "easeInBack"_L1},
    {Easing::Preset::EaseOutBack, "easeOutBack"_L1},
    {Easing::Preset::EaseInOutBack, "easeInOutBack"_L1},
    {Easing::Preset::EaseInElastic, "easeInElastic"_L1},
    {Easing::Preset::EaseOutElastic, "easeOutElastic"_L1},
    {Easing::Preset::EaseInOutElastic, "easeInOutElastic"_L1},
    {Easing::Preset::EaseInBounce, "easeInBounce"_L1},
    {Easing::Preset::EaseOutBounce, "easeOutBounce"_L1},
    {Easing::Preset::EaseInOutBounce, "easeInOutBounce"_L1},
};

constexpr double kPi = std::numbers::pi;

// CSS cubic-bezier(x1, y1, x2, y2): solve x(s) = progress for s, return y(s).
double evaluateBezier(const std::array<double, 4> &p, double progress)
{
    const double x1 = p[0], y1 = p[1], x2 = p[2], y2 = p[3];
    const auto coord = [](double s, double a, double b) {
        const double inv = 1.0 - s;
        return 3.0 * inv * inv * s * a + 3.0 * inv * s * s * b + s * s * s;
    };
    const auto derivative = [](double s, double a, double b) {
        const double inv = 1.0 - s;
        return 3.0 * inv * inv * a + 6.0 * inv * s * (b - a) + 3.0 * s * s * (1.0 - b);
    };
    // Newton-Raphson from a good initial guess, then bisection as a guaranteed fallback.
    double s = progress;
    for (int i = 0; i < 8; ++i) {
        const double error = coord(s, x1, x2) - progress;
        if (std::abs(error) < 1e-9) {
            return coord(s, y1, y2);
        }
        const double d = derivative(s, x1, x2);
        if (std::abs(d) < 1e-9) {
            break;
        }
        s -= error / d;
    }
    double low = 0.0;
    double high = 1.0;
    s = progress;
    for (int i = 0; i < 64; ++i) {
        const double x = coord(s, x1, x2);
        if (std::abs(x - progress) < 1e-9) {
            break;
        }
        if (x < progress) {
            low = s;
        } else {
            high = s;
        }
        s = 0.5 * (low + high);
    }
    return coord(s, y1, y2);
}

double bounceOut(double t)
{
    constexpr double n1 = 7.5625;
    constexpr double d1 = 2.75;
    if (t < 1.0 / d1) {
        return n1 * t * t;
    }
    if (t < 2.0 / d1) {
        t -= 1.5 / d1;
        return n1 * t * t + 0.75;
    }
    if (t < 2.5 / d1) {
        t -= 2.25 / d1;
        return n1 * t * t + 0.9375;
    }
    t -= 2.625 / d1;
    return n1 * t * t + 0.984375;
}

} // namespace

Easing Easing::preset(Preset preset)
{
    Easing easing;
    easing.m_preset = preset == Preset::Custom ? Preset::Linear : preset;
    switch (easing.m_preset) {
    case Preset::Ease:
        easing.m_bezier = {0.25, 0.1, 0.25, 1.0};
        break;
    case Preset::EaseIn:
        easing.m_bezier = {0.42, 0.0, 1.0, 1.0};
        break;
    case Preset::EaseOut:
        easing.m_bezier = {0.0, 0.0, 0.58, 1.0};
        break;
    case Preset::EaseInOut:
        easing.m_bezier = {0.42, 0.0, 0.58, 1.0};
        break;
    default:
        break;
    }
    return easing;
}

std::optional<Easing> Easing::cubicBezier(double x1, double y1, double x2, double y2)
{
    if (!(x1 >= 0.0 && x1 <= 1.0 && x2 >= 0.0 && x2 <= 1.0) || !std::isfinite(y1) || !std::isfinite(y2)) {
        return std::nullopt;
    }
    Easing easing;
    easing.m_preset = Preset::Custom;
    easing.m_bezier = {x1, y1, x2, y2};
    return easing;
}

double Easing::apply(double t) const
{
    if (t <= 0.0) {
        return 0.0;
    }
    if (t >= 1.0) {
        return 1.0;
    }
    constexpr double c1 = 1.70158;
    constexpr double c2 = c1 * 1.525;
    constexpr double c3 = c1 + 1.0;
    constexpr double c4 = (2.0 * kPi) / 3.0;
    constexpr double c5 = (2.0 * kPi) / 4.5;
    switch (m_preset) {
    case Preset::Linear:
        return t;
    case Preset::Ease:
    case Preset::EaseIn:
    case Preset::EaseOut:
    case Preset::EaseInOut:
    case Preset::Custom:
        return evaluateBezier(m_bezier, t);
    case Preset::EaseInQuad:
        return t * t;
    case Preset::EaseOutQuad:
        return 1.0 - (1.0 - t) * (1.0 - t);
    case Preset::EaseInOutQuad:
        return t < 0.5 ? 2.0 * t * t : 1.0 - std::pow(-2.0 * t + 2.0, 2.0) / 2.0;
    case Preset::EaseInCubic:
        return t * t * t;
    case Preset::EaseOutCubic:
        return 1.0 - std::pow(1.0 - t, 3.0);
    case Preset::EaseInOutCubic:
        return t < 0.5 ? 4.0 * t * t * t : 1.0 - std::pow(-2.0 * t + 2.0, 3.0) / 2.0;
    case Preset::EaseInQuart:
        return t * t * t * t;
    case Preset::EaseOutQuart:
        return 1.0 - std::pow(1.0 - t, 4.0);
    case Preset::EaseInOutQuart:
        return t < 0.5 ? 8.0 * t * t * t * t : 1.0 - std::pow(-2.0 * t + 2.0, 4.0) / 2.0;
    case Preset::EaseInQuint:
        return t * t * t * t * t;
    case Preset::EaseOutQuint:
        return 1.0 - std::pow(1.0 - t, 5.0);
    case Preset::EaseInOutQuint:
        return t < 0.5 ? 16.0 * t * t * t * t * t : 1.0 - std::pow(-2.0 * t + 2.0, 5.0) / 2.0;
    case Preset::EaseInSine:
        return 1.0 - std::cos((t * kPi) / 2.0);
    case Preset::EaseOutSine:
        return std::sin((t * kPi) / 2.0);
    case Preset::EaseInOutSine:
        return -(std::cos(kPi * t) - 1.0) / 2.0;
    case Preset::EaseInExpo:
        return std::pow(2.0, 10.0 * t - 10.0);
    case Preset::EaseOutExpo:
        return 1.0 - std::pow(2.0, -10.0 * t);
    case Preset::EaseInOutExpo:
        return t < 0.5 ? std::pow(2.0, 20.0 * t - 10.0) / 2.0 : (2.0 - std::pow(2.0, -20.0 * t + 10.0)) / 2.0;
    case Preset::EaseInCirc:
        return 1.0 - std::sqrt(1.0 - t * t);
    case Preset::EaseOutCirc:
        return std::sqrt(1.0 - (t - 1.0) * (t - 1.0));
    case Preset::EaseInOutCirc:
        return t < 0.5 ? (1.0 - std::sqrt(1.0 - std::pow(2.0 * t, 2.0))) / 2.0
                       : (std::sqrt(1.0 - std::pow(-2.0 * t + 2.0, 2.0)) + 1.0) / 2.0;
    case Preset::EaseInBack:
        return c3 * t * t * t - c1 * t * t;
    case Preset::EaseOutBack:
        return 1.0 + c3 * std::pow(t - 1.0, 3.0) + c1 * std::pow(t - 1.0, 2.0);
    case Preset::EaseInOutBack:
        return t < 0.5 ? (std::pow(2.0 * t, 2.0) * ((c2 + 1.0) * 2.0 * t - c2)) / 2.0
                       : (std::pow(2.0 * t - 2.0, 2.0) * ((c2 + 1.0) * (t * 2.0 - 2.0) + c2) + 2.0) / 2.0;
    case Preset::EaseInElastic:
        return -std::pow(2.0, 10.0 * t - 10.0) * std::sin((t * 10.0 - 10.75) * c4);
    case Preset::EaseOutElastic:
        return std::pow(2.0, -10.0 * t) * std::sin((t * 10.0 - 0.75) * c4) + 1.0;
    case Preset::EaseInOutElastic:
        return t < 0.5 ? -(std::pow(2.0, 20.0 * t - 10.0) * std::sin((20.0 * t - 11.125) * c5)) / 2.0
                       : (std::pow(2.0, -20.0 * t + 10.0) * std::sin((20.0 * t - 11.125) * c5)) / 2.0 + 1.0;
    case Preset::EaseInBounce:
        return 1.0 - bounceOut(1.0 - t);
    case Preset::EaseOutBounce:
        return bounceOut(t);
    case Preset::EaseInOutBounce:
        return t < 0.5 ? (1.0 - bounceOut(1.0 - 2.0 * t)) / 2.0 : (1.0 + bounceOut(2.0 * t - 1.0)) / 2.0;
    }
    return t;
}

QString Easing::name() const
{
    for (const auto &entry : kPresetNames) {
        if (entry.preset == m_preset) {
            return entry.name;
        }
    }
    return {};
}

std::optional<Easing> Easing::fromName(QStringView name)
{
    for (const auto &entry : kPresetNames) {
        if (name == entry.name) {
            return preset(entry.preset);
        }
    }
    return std::nullopt;
}

QStringList Easing::presetNames()
{
    QStringList names;
    for (const auto &entry : kPresetNames) {
        names.append(entry.name);
    }
    return names;
}

} // namespace vedit
