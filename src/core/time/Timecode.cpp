// SPDX-License-Identifier: GPL-3.0-or-later
#include "Timecode.h"

#include <algorithm>

namespace velacut {

namespace {

// Whole seconds elapsed at the start of `frame` (frame × den / num, rounded down).
std::int64_t secondsAt(std::int64_t frame, const Rational &rate)
{
    return rate.num() > 0 ? frame * rate.den() / rate.num() : 0;
}

} // namespace

std::int64_t firstFrameOfSecond(std::int64_t seconds, const Rational &rate)
{
    // ⌈seconds × num / den⌉
    return rate.den() > 0 ? (seconds * rate.num() + rate.den() - 1) / rate.den() : 0;
}

TimecodeParts timecodeParts(std::int64_t frame, const Rational &rate)
{
    frame = std::max<std::int64_t>(0, frame);
    TimecodeParts parts;
    const std::int64_t total = secondsAt(frame, rate);
    parts.frames = frame - firstFrameOfSecond(total, rate);
    parts.hours = total / 3600;
    parts.minutes = total / 60 % 60;
    parts.seconds = total % 60;
    return parts;
}

QString formatTimecode(std::int64_t frame, const Rational &rate)
{
    const TimecodeParts t = timecodeParts(frame, rate);
    const QString minutesSeconds = QStringLiteral("%1:%2.%3")
                                       .arg(t.minutes, 2, 10, QLatin1Char('0'))
                                       .arg(t.seconds, 2, 10, QLatin1Char('0'))
                                       .arg(t.frames, 2, 10, QLatin1Char('0'));
    return t.hours > 0 ? QString::number(t.hours) + QLatin1Char(':') + minutesSeconds : minutesSeconds;
}

QString formatSeconds(std::int64_t frame, const Rational &rate)
{
    const TimecodeParts t = timecodeParts(frame, rate);
    if (t.hours > 0) {
        return QStringLiteral("%1:%2:%3").arg(t.hours).arg(t.minutes, 2, 10, QLatin1Char('0')).arg(t.seconds, 2, 10, QLatin1Char('0'));
    }
    return QStringLiteral("%1:%2").arg(t.minutes).arg(t.seconds, 2, 10, QLatin1Char('0'));
}

} // namespace velacut
