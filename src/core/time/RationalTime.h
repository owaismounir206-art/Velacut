// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "Rational.h"

#include <QString>
#include <QStringView>

#include <compare>
#include <cstdint>
#include <optional>

namespace velacut {

// A point in time (or a duration) expressed as an integer number of units at an exact rate:
// `value` frames at `rate` frames per second (or samples at a sample rate). Never a double.
//
// Arithmetic between two times requires the same rate (throws std::invalid_argument otherwise):
// callers convert explicitly with rescaled(), choosing the rounding. Comparison works across rates
// and compares the exact instant (1@1 == 30@30).
class RationalTime
{
public:
    RationalTime() = default; // 0@1
    // Throws std::invalid_argument if rate <= 0.
    RationalTime(std::int64_t value, Rational rate);

    std::int64_t value() const noexcept { return m_value; }
    Rational rate() const noexcept { return m_rate; }

    // Exact time in seconds.
    Rational seconds() const;
    // For display only.
    double toSecondsDouble() const noexcept;

    // Same instant at another rate; exact when possible, otherwise rounded as requested.
    RationalTime rescaled(Rational newRate, Rounding rounding) const;
    // Same instant at another rate only if it is representable exactly.
    std::optional<RationalTime> rescaledExact(Rational newRate) const;
    static RationalTime fromSeconds(Rational seconds, Rational rate, Rounding rounding);

    bool hasSameRate(const RationalTime &other) const noexcept { return m_rate == other.m_rate; }
    bool isZero() const noexcept { return m_value == 0; }
    bool isNegative() const noexcept { return m_value < 0; }

    // "150@30000/1001", or "150@30" for integer rates. See docs/FILE_FORMAT.md §3.2.
    QString toString() const;
    static std::optional<RationalTime> fromString(QStringView text);

    RationalTime operator+(const RationalTime &other) const;
    RationalTime operator-(const RationalTime &other) const;
    RationalTime operator-() const;
    RationalTime operator*(std::int64_t factor) const;
    RationalTime &operator+=(const RationalTime &other);
    RationalTime &operator-=(const RationalTime &other);

    // Exact temporal comparison, valid across different rates.
    friend bool operator==(const RationalTime &a, const RationalTime &b);
    friend std::strong_ordering operator<=>(const RationalTime &a, const RationalTime &b);

    // True only if value and rate are both identical (stricter than ==).
    bool isIdenticalTo(const RationalTime &other) const noexcept
    {
        return m_value == other.m_value && m_rate == other.m_rate;
    }

private:
    void requireSameRate(const RationalTime &other) const;

    std::int64_t m_value = 0;
    Rational m_rate{1};
};

// Half-open interval [start, start + duration) on a single rate.
struct TimeRange
{
    RationalTime start;
    RationalTime duration;

    TimeRange() = default;
    // Throws std::invalid_argument if the rates differ or the duration is negative.
    TimeRange(RationalTime start, RationalTime duration);

    RationalTime end() const { return start + duration; }
    bool isEmpty() const noexcept { return duration.isZero(); }
    bool contains(const RationalTime &time) const { return time >= start && time < end(); }
    bool intersects(const TimeRange &other) const { return start < other.end() && other.start < end(); }

    friend bool operator==(const TimeRange &a, const TimeRange &b) = default;
};

} // namespace velacut
