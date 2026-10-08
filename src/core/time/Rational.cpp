// SPDX-License-Identifier: GPL-3.0-or-later
#include "Rational.h"

#include <limits>
#include <stdexcept>

namespace velacut {

namespace detail {

std::int64_t narrow(Int128 value)
{
    if (value > std::numeric_limits<std::int64_t>::max() || value < std::numeric_limits<std::int64_t>::min()) {
        throw std::overflow_error("velacut::Rational: value does not fit in 64 bits");
    }
    return static_cast<std::int64_t>(value);
}

Int128 divide(Int128 numerator, Int128 denominator, Rounding rounding)
{
    if (denominator == 0) {
        throw std::invalid_argument("velacut::Rational: division by zero");
    }
    if (denominator < 0) {
        numerator = -numerator;
        denominator = -denominator;
    }
    // C++ division truncates toward zero; compute floor first.
    Int128 quotient = numerator / denominator;
    Int128 remainder = numerator % denominator;
    if (remainder < 0) {
        quotient -= 1;
        remainder += denominator;
    }
    // Now numerator == quotient * denominator + remainder, with 0 <= remainder < denominator.
    switch (rounding) {
    case Rounding::Floor:
        return quotient;
    case Rounding::Ceil:
        return remainder == 0 ? quotient : quotient + 1;
    case Rounding::NearestEven: {
        const Int128 twice = remainder * 2;
        if (twice < denominator) {
            return quotient;
        }
        if (twice > denominator) {
            return quotient + 1;
        }
        return (quotient % 2 == 0) ? quotient : quotient + 1;
    }
    }
    return quotient;
}

namespace {
Int128 gcd(Int128 a, Int128 b)
{
    if (a < 0) {
        a = -a;
    }
    if (b < 0) {
        b = -b;
    }
    while (b != 0) {
        const Int128 t = a % b;
        a = b;
        b = t;
    }
    return a;
}
} // namespace

} // namespace detail

Rational::Rational(std::int64_t num, std::int64_t den)
{
    if (den == 0) {
        throw std::invalid_argument("velacut::Rational: zero denominator");
    }
    *this = fromWide(num, den);
}

Rational Rational::fromWide(detail::Int128 num, detail::Int128 den)
{
    if (den == 0) {
        throw std::invalid_argument("velacut::Rational: zero denominator");
    }
    if (den < 0) {
        num = -num;
        den = -den;
    }
    const detail::Int128 g = detail::gcd(num, den);
    if (g > 1) {
        num /= g;
        den /= g;
    }
    Rational result;
    result.m_num = detail::narrow(num);
    result.m_den = detail::narrow(num == 0 ? 1 : den);
    return result;
}

double Rational::toDouble() const noexcept
{
    return static_cast<double>(m_num) / static_cast<double>(m_den);
}

std::int64_t Rational::toInteger(Rounding rounding) const
{
    return detail::narrow(detail::divide(m_num, m_den, rounding));
}

Rational Rational::inverse() const
{
    if (m_num == 0) {
        throw std::invalid_argument("velacut::Rational: inverse of zero");
    }
    return fromWide(m_den, m_num);
}

QString Rational::toString() const
{
    if (m_den == 1) {
        return QString::number(m_num);
    }
    return QString::number(m_num) + QLatin1Char('/') + QString::number(m_den);
}

namespace {
// Strict decimal integer: optional '-' (if allowed) followed by ASCII digits only.
std::optional<std::int64_t> parseInteger(QStringView text, bool allowNegative)
{
    if (text.isEmpty()) {
        return std::nullopt;
    }
    qsizetype first = 0;
    if (text.front() == QLatin1Char('-')) {
        if (!allowNegative || text.size() == 1) {
            return std::nullopt;
        }
        first = 1;
    }
    for (qsizetype i = first; i < text.size(); ++i) {
        const QChar c = text[i];
        if (c < QLatin1Char('0') || c > QLatin1Char('9')) {
            return std::nullopt;
        }
    }
    bool ok = false;
    const qlonglong value = text.toLongLong(&ok);
    if (!ok) {
        return std::nullopt;
    }
    return static_cast<std::int64_t>(value);
}
} // namespace

std::optional<Rational> Rational::fromString(QStringView text)
{
    const qsizetype slash = text.indexOf(QLatin1Char('/'));
    if (slash < 0) {
        const auto num = parseInteger(text, true);
        if (!num) {
            return std::nullopt;
        }
        return Rational(*num, 1);
    }
    const auto num = parseInteger(text.left(slash), true);
    const auto den = parseInteger(text.mid(slash + 1), false);
    if (!num || !den || *den == 0) {
        return std::nullopt;
    }
    return Rational(*num, *den);
}

Rational Rational::operator+(const Rational &other) const
{
    return fromWide(detail::Int128(m_num) * other.m_den + detail::Int128(other.m_num) * m_den,
                    detail::Int128(m_den) * other.m_den);
}

Rational Rational::operator-(const Rational &other) const
{
    return fromWide(detail::Int128(m_num) * other.m_den - detail::Int128(other.m_num) * m_den,
                    detail::Int128(m_den) * other.m_den);
}

Rational Rational::operator*(const Rational &other) const
{
    return fromWide(detail::Int128(m_num) * other.m_num, detail::Int128(m_den) * other.m_den);
}

Rational Rational::operator/(const Rational &other) const
{
    if (other.m_num == 0) {
        throw std::invalid_argument("velacut::Rational: division by zero");
    }
    return fromWide(detail::Int128(m_num) * other.m_den, detail::Int128(m_den) * other.m_num);
}

Rational Rational::operator-() const
{
    return fromWide(-detail::Int128(m_num), m_den);
}

std::strong_ordering operator<=>(const Rational &a, const Rational &b) noexcept
{
    const detail::Int128 left = detail::Int128(a.m_num) * b.m_den;
    const detail::Int128 right = detail::Int128(b.m_num) * a.m_den;
    if (left < right) {
        return std::strong_ordering::less;
    }
    if (left > right) {
        return std::strong_ordering::greater;
    }
    return std::strong_ordering::equal;
}

} // namespace velacut
