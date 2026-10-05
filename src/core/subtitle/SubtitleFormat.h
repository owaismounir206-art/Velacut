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

// Reads and writes subtitle files (SRT, WebVTT). Times are rounded to the nearest unit of `rate`; formatting tags
// are dropped (the caption style of the track decides the look).
class SubtitleFormat
{
public:
    // nullopt when the text is not a subtitle file of that format.
    static std::optional<std::vector<SubtitleEntry>> parseSRT(const QString &content, const Rational &rate);
    static std::optional<std::vector<SubtitleEntry>> parseVTT(const QString &content, const Rational &rate);
    // WebVTT when the text starts with "WEBVTT", otherwise SRT.
    static std::optional<std::vector<SubtitleEntry>> parse(const QString &content, const Rational &rate);

    static QString formatSRT(const std::vector<SubtitleEntry> &entries);
    static QString formatVTT(const std::vector<SubtitleEntry> &entries);
};

} // namespace vedit
