// SPDX-License-Identifier: GPL-3.0-or-later
#include "RationalTime.h"

#include <stdexcept>

namespace velacut {

namespace {
// Rates are limited to 31-bit numerator and denominator (48000, 30000/1001 and even 705600000 fit),
// which keeps every intermediate product of the conversions below within 128 bits.
constexpr std::int64_t kMaxRateTerm = 0x7fffffff;

void validateRate(const Rational &rate)
{
    if (!rate.isPositive()) {
        throw std::invalid_argument("velacut::RationalTime: rate must be positive");
    }
    if (rate.num() > kMaxRateTerm || rate.den() > kMaxRateTerm) {
        throw std::invalid_argument("velacut::RationalTime: rate terms must fit in 31 bits");
    }
}
} // namespace

RationalTime::RationalTime(std::int64_t value, Rational rate)
    : m_value(value)
    , m_rate(rate)
{
    validateRate(rate);
}

Rational RationalTime::seconds() const
{
    return Rational(m_value) / m_rate;
}

double RationalTime::toSecondsDouble() const noexcept
{
    return static_cast<double>(m_value) * static_cast<double>(m_rate.den()) / static_cast<double>(m_rate.num());
}

RationalTime RationalTime::rescaled(Rational newRate, Rounding rounding) const
{
    validateRate(newRate);
    if (newRate == m_rate) {
        return *this;
    }
    // value' = value * newRate / rate = value * newNum * den / (newDen * num)
    const detail::Int128 numerator =
        detail::Int128(m_value) * newRate.num() * detail::Int128(m_rate.den());
    const detail::Int128 denominator = detail::Int128(newRate.den()) * m_rate.num();
    return RationalTime(detail::narrow(detail::divide(numerator, denominator, rounding)), newRate);
}

std::optional<RationalTime> RationalTime::rescaledExact(Rational newRate) const
{
    const RationalTime floor = rescaled(newRate, Rounding::Floor);
    if (floor.seconds() != seconds()) {
        return std::nullopt;
    }
    return floor;
}

RationalTime RationalTime::fromSeconds(Rational seconds, Rational rate, Rounding rounding)
{
    const Rational units = seconds * rate;
    return RationalTime(units.toInteger(rounding), rate);
}

QString RationalTime::toString() const
{
    return QString::number(m_value) + QLatin1Char('@') + m_rate.toString();
}

std::optional<RationalTime> RationalTime::fromString(QStringView text)
{
    const qsizetype at = text.indexOf(QLatin1Char('@'));
    if (at <= 0) {
        return std::nullopt;
    }
    // Reuse Rational's strict integer parsing for the value ("123" is a valid Rational with den 1).
    const auto value = Rational::fromString(text.left(at));
    const auto rate = Rational::fromString(text.mid(at + 1));
    if (!value || value->den() != 1 || text.left(at).contains(QLatin1Char('/')) || !rate || !rate->isPositive()) {
        return std::nullopt;
    }
    return RationalTime(value->num(), *rate);
}

void RationalTime::requireSameRate(const RationalTime &other) const
{
    if (m_rate != other.m_rate) {
        throw std::invalid_argument("velacut::RationalTime: arithmetic between different rates");
    }
}

RationalTime RationalTime::operator+(const RationalTime &other) const
{
    requireSameRate(other);
    return RationalTime(detail::narrow(detail::Int128(m_value) + other.m_value), m_rate);
}

RationalTime RationalTime::operator-(const RationalTime &other) const
{
    requireSameRate(other);
    return RationalTime(detail::narrow(detail::Int128(m_value) - other.m_value), m_rate);
}

RationalTime RationalTime::operator-() const
{
    return RationalTime(detail::narrow(-detail::Int128(m_value)), m_rate);
}

RationalTime RationalTime::operator*(std::int64_t factor) const
{
    return RationalTime(detail::narrow(detail::Int128(m_value) * factor), m_rate);
}

RationalTime &RationalTime::operator+=(const RationalTime &other)
{
    *this = *this + other;
    return *this;
}

RationalTime &RationalTime::operator-=(const RationalTime &other)
{
    *this = *this - other;
    return *this;
}

bool operator==(const RationalTime &a, const RationalTime &b)
{
    return (a <=> b) == std::strong_ordering::equal;
}

std::strong_ordering operator<=>(const RationalTime &a, const RationalTime &b)
{
    if (a.m_rate == b.m_rate) {
        return a.m_value <=> b.m_value;
    }
    // a.value * a.den / a.num  vs  b.value * b.den / b.num  (nums > 0)
    const detail::Int128 left = detail::Int128(a.m_value) * a.m_rate.den() * detail::Int128(b.m_rate.num());
    const detail::Int128 right = detail::Int128(b.m_value) * b.m_rate.den() * detail::Int128(a.m_rate.num());
    if (left < right) {
        return std::strong_ordering::less;
    }
    if (left > right) {
        return std::strong_ordering::greater;
    }
    return std::strong_ordering::equal;
}

TimeRange::TimeRange(RationalTime rangeStart, RationalTime rangeDuration)
    : start(rangeStart)
    , duration(rangeDuration)
{
    if (!start.hasSameRate(duration)) {
        throw std::invalid_argument("velacut::TimeRange: start and duration must share the rate");
    }
    if (duration.isNegative()) {
        throw std::invalid_argument("velacut::TimeRange: negative duration");
    }
}

} // namespace velacut
