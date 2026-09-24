// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/effects/Easing.h"
#include "core/time/RationalTime.h"

#include <QJsonValue>
#include <QString>

#include <cstdint>
#include <optional>
#include <variant>
#include <vector>

namespace vedit {

// sRGB color with straight (non-premultiplied) alpha; serialized as "#RRGGBBAA".
struct Color
{
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
    std::uint8_t a = 255;

    QString toString() const;
    static std::optional<Color> fromString(QStringView text);
    // Interpolates in linear light (alpha linearly), as required by docs/FILE_FORMAT.md §3.5.
    static Color interpolate(const Color &from, const Color &to, double t);

    friend bool operator==(const Color &, const Color &) noexcept = default;
};

struct Vec2
{
    double x = 0.0;
    double y = 0.0;

    friend bool operator==(const Vec2 &, const Vec2 &) noexcept = default;
};

// A parameter value. JSON mapping (docs/FILE_FORMAT.md §3.5): number -> double, bool -> bool,
// "#RRGGBBAA" -> Color, other strings -> QString, [x, y] -> Vec2. Any other JSON value is kept verbatim
// (QJsonValue) so that effect parameters unknown to this version are never lost.
using ParamValue = std::variant<double, bool, QString, Color, Vec2, QJsonValue>;

bool paramValueIsAnimatable(const ParamValue &value);

enum class Interpolation
{
    Linear,
    Hold,
    Bezier,
};

struct Keyframe
{
    RationalTime time;
    ParamValue value;
    // Interpolation of the segment that starts at this keyframe.
    Interpolation interpolation = Interpolation::Linear;
    // Used only when interpolation == Bezier.
    Easing easing = Easing::preset(Easing::Preset::EaseInOut);

    friend bool operator==(const Keyframe &, const Keyframe &) = default;
};

// A static value, or a list of keyframes (sorted by time, no duplicates) when animated.
class Param
{
public:
    Param() = default;
    Param(ParamValue value) // NOLINT(google-explicit-constructor): a literal is a valid static parameter
        : m_value(std::move(value))
    {
    }
    Param(double value)
        : m_value(value)
    {
    }

    bool isAnimated() const noexcept { return !m_keyframes.empty(); }
    const ParamValue &staticValue() const noexcept { return m_value; }
    const std::vector<Keyframe> &keyframes() const noexcept { return m_keyframes; }

    void setStaticValue(ParamValue value);
    // Returns false (and leaves the parameter unchanged) if the keyframes are not strictly increasing in time,
    // mix value types, or animate a non-animatable type.
    bool setKeyframes(std::vector<Keyframe> keyframes);

    // Value at `time` (same time space as the keyframes). Constant before the first and after the last keyframe.
    ParamValue valueAt(const RationalTime &time) const;
    // Convenience for numeric parameters; returns `fallback` if the value is not a number.
    double numberAt(const RationalTime &time, double fallback = 0.0) const;

    friend bool operator==(const Param &, const Param &) = default;

private:
    ParamValue m_value = 0.0;
    std::vector<Keyframe> m_keyframes;
};

} // namespace vedit
