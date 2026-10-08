// SPDX-License-Identifier: GPL-3.0-or-later
#include "Param.h"

#include <algorithm>
#include <cmath>

namespace velacut {

namespace {

int hexDigit(QChar c)
{
    if (c >= QLatin1Char('0') && c <= QLatin1Char('9')) {
        return c.unicode() - '0';
    }
    if (c >= QLatin1Char('a') && c <= QLatin1Char('f')) {
        return c.unicode() - 'a' + 10;
    }
    if (c >= QLatin1Char('A') && c <= QLatin1Char('F')) {
        return c.unicode() - 'A' + 10;
    }
    return -1;
}

double srgbToLinear(double c)
{
    return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
}

double linearToSrgb(double c)
{
    return c <= 0.0031308 ? c * 12.92 : 1.055 * std::pow(c, 1.0 / 2.4) - 0.055;
}

std::uint8_t toByte(double unit)
{
    return static_cast<std::uint8_t>(std::clamp(std::lround(unit * 255.0), 0L, 255L));
}

} // namespace

QString Color::toString() const
{
    return QString::asprintf("#%02X%02X%02X%02X", r, g, b, a);
}

std::optional<Color> Color::fromString(QStringView text)
{
    if (text.size() != 9 || text.front() != QLatin1Char('#')) {
        return std::nullopt;
    }
    std::uint8_t bytes[4];
    for (int i = 0; i < 4; ++i) {
        const int high = hexDigit(text[1 + 2 * i]);
        const int low = hexDigit(text[2 + 2 * i]);
        if (high < 0 || low < 0) {
            return std::nullopt;
        }
        bytes[i] = static_cast<std::uint8_t>(high * 16 + low);
    }
    return Color{bytes[0], bytes[1], bytes[2], bytes[3]};
}

Color Color::interpolate(const Color &from, const Color &to, double t)
{
    const auto channel = [t](std::uint8_t a, std::uint8_t b) {
        const double la = srgbToLinear(a / 255.0);
        const double lb = srgbToLinear(b / 255.0);
        return toByte(linearToSrgb(std::clamp(la + (lb - la) * t, 0.0, 1.0)));
    };
    const double alpha = from.a / 255.0 + (to.a / 255.0 - from.a / 255.0) * t;
    return Color{channel(from.r, to.r), channel(from.g, to.g), channel(from.b, to.b),
                 toByte(std::clamp(alpha, 0.0, 1.0))};
}

bool paramValueIsAnimatable(const ParamValue &value)
{
    return std::holds_alternative<double>(value) || std::holds_alternative<Color>(value) ||
           std::holds_alternative<Vec2>(value);
}

void Param::setStaticValue(ParamValue value)
{
    m_value = std::move(value);
    m_keyframes.clear();
}

bool Param::setKeyframes(std::vector<Keyframe> keyframes)
{
    for (size_t i = 0; i < keyframes.size(); ++i) {
        if (!paramValueIsAnimatable(keyframes[i].value)) {
            return false;
        }
        if (i > 0) {
            if (keyframes[i].value.index() != keyframes[0].value.index()) {
                return false;
            }
            if (!(keyframes[i - 1].time < keyframes[i].time)) {
                return false;
            }
        }
    }
    // Canonical form: the easing is meaningful only for Bézier segments (and is not stored otherwise).
    for (Keyframe &keyframe : keyframes) {
        if (keyframe.interpolation != Interpolation::Bezier) {
            keyframe.easing = Easing();
        }
    }
    if (!keyframes.empty()) {
        m_value = keyframes.front().value;
    }
    m_keyframes = std::move(keyframes);
    return true;
}

ParamValue Param::valueAt(const RationalTime &time) const
{
    if (m_keyframes.empty()) {
        return m_value;
    }
    if (time <= m_keyframes.front().time) {
        return m_keyframes.front().value;
    }
    if (time >= m_keyframes.back().time) {
        return m_keyframes.back().value;
    }
    // First keyframe strictly after `time`.
    const auto next = std::upper_bound(m_keyframes.begin(), m_keyframes.end(), time,
                                       [](const RationalTime &t, const Keyframe &k) { return t < k.time; });
    const Keyframe &b = *next;
    const Keyframe &a = *(next - 1);
    if (a.interpolation == Interpolation::Hold) {
        return a.value;
    }
    const Rational span = b.time.seconds() - a.time.seconds();
    const Rational elapsed = time.seconds() - a.time.seconds();
    double progress = (elapsed / span).toDouble();
    if (a.interpolation == Interpolation::Bezier) {
        progress = a.easing.apply(progress);
    }
    if (const auto *from = std::get_if<double>(&a.value)) {
        const double to = std::get<double>(b.value);
        return *from + (to - *from) * progress;
    }
    if (const auto *from = std::get_if<Vec2>(&a.value)) {
        const Vec2 to = std::get<Vec2>(b.value);
        return Vec2{from->x + (to.x - from->x) * progress, from->y + (to.y - from->y) * progress};
    }
    if (const auto *from = std::get_if<Color>(&a.value)) {
        return Color::interpolate(*from, std::get<Color>(b.value), progress);
    }
    return a.value;
}

double Param::numberAt(const RationalTime &time, double fallback) const
{
    const ParamValue value = valueAt(time);
    if (const auto *number = std::get_if<double>(&value)) {
        return *number;
    }
    return fallback;
}

} // namespace velacut
