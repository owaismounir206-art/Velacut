// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>
#include <QStringView>

#include <compare>
#include <cstdint>
#include <optional>

namespace velacut {

// Rounding policy for every conversion that cannot be exact. NearestEven rounds halves to the even
// neighbour, so repeated conversions do not drift in one direction.
enum class Rounding
{
    NearestEven,
    Floor,
    Ceil,
};

namespace detail {
__extension__ typedef __int128 Int128;

// Narrows an intermediate result, throwing std::overflow_error if it does not fit in 64 bits.
std::int64_t narrow(Int128 value);
// Integer division of numerator/denominator (denominator != 0) with the given rounding.
Int128 divide(Int128 numerator, Int128 denominator, Rounding rounding);
} // namespace detail

// Exact rational number num/den, always normalized (den > 0, gcd(num, den) == 1).
// Used for frame rates, sample rates and exact durations; never use double for time.
class Rational
{
public:
    constexpr Rational() noexcept = default;
    // Throws std::invalid_argument if den == 0.
    Rational(std::int64_t num, std::int64_t den = 1);

    std::int64_t num() const noexcept { return m_num; }
    std::int64_t den() const noexcept { return m_den; }

    bool isZero() const noexcept { return m_num == 0; }
    bool isPositive() const noexcept { return m_num > 0; }
    // Only for display and non-time quantities (e.g. UI sliders); never for time arithmetic.
    double toDouble() const noexcept;
    // Rounds num/den to an integer.
    std::int64_t toInteger(Rounding rounding) const;
    Rational inverse() const;

    // "30000/1001", or "30" when den == 1.
    QString toString() const;
    // Accepts "num/den" or "num" (optional leading '-'); rejects zero denominators and garbage.
    static std::optional<Rational> fromString(QStringView text);

    // Arithmetic throws std::overflow_error if the normalized result does not fit in 64 bits.
    Rational operator+(const Rational &other) const;
    Rational operator-(const Rational &other) const;
    Rational operator*(const Rational &other) const;
    Rational operator/(const Rational &other) const;
    Rational operator-() const;

    friend bool operator==(const Rational &a, const Rational &b) noexcept = default;
    friend std::strong_ordering operator<=>(const Rational &a, const Rational &b) noexcept;

private:
    static Rational fromWide(detail::Int128 num, detail::Int128 den);

    std::int64_t m_num = 0;
    std::int64_t m_den = 1;
};

} // namespace velacut
