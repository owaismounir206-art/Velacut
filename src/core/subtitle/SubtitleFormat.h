// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/time/RationalTime.h"

#include <QString>
#include <QStringList>

#include <optional>
#include <vector>

namespace vedit {

// A subtitle entry with timing and text.
struct SubtitleEntry
{
    RationalTime start;
    RationalTime end;
    QString text; // can contain multiple lines separated by \n

    friend bool operator==(const SubtitleEntry &, const SubtitleEntry &) = default;
};

// Parse and format subtitle files (SRT, VTT).
class SubtitleFormat
{
public:
    // Parse SRT format. Returns nullopt on parse error.
    static std::optional<std::vector<SubtitleEntry>> parseSRT(const QString &content, const Rational &rate);

    // Parse WebVTT format. Returns nullopt on parse error.
    static std::optional<std::vector<SubtitleEntry>> parseVTT(const QString &content, const Rational &rate);

    // Format as SRT.
    static QString formatSRT(const std::vector<SubtitleEntry> &entries);

    // Format as WebVTT.
    static QString formatVTT(const std::vector<SubtitleEntry> &entries);

private:
    static std::optional<RationalTime> parseTimecode(const QString &timecode, const Rational &rate);
    static QString formatTimecode(const RationalTime &time, bool vtt = false);
};

} // namespace vedit
