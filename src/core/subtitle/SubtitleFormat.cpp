// SPDX-License-Identifier: GPL-3.0-or-later
#include "SubtitleFormat.h"

#include <QRegularExpression>
#include <QStringList>

using namespace Qt::StringLiterals;

namespace vedit {

std::optional<RationalTime> SubtitleFormat::parseTimecode(const QString &timecode, const Rational &rate)
{
    static const QRegularExpression re(uR"((\d{2}):(\d{2}):(\d{2})[,.](\d{3}))"_s);
    const QRegularExpressionMatch match = re.match(timecode);
    if (!match.hasMatch()) {
        return std::nullopt;
    }
    
    const int hours = match.captured(1).toInt();
    const int minutes = match.captured(2).toInt();
    const int seconds = match.captured(3).toInt();
    const int milliseconds = match.captured(4).toInt();
    
    const double totalSeconds = hours * 3600.0 + minutes * 60.0 + seconds + milliseconds / 1000.0;
    return RationalTime::fromSeconds(Rational(static_cast<int>(totalSeconds * 1000), 1000), rate, Rounding::NearestEven);
}

QString SubtitleFormat::formatTimecode(const RationalTime &time, bool vtt)
{
    const Rational secondsRat = time.seconds();
    const double seconds = static_cast<double>(secondsRat.num()) / secondsRat.den();
    
    const int hours = static_cast<int>(seconds / 3600);
    const int minutes = static_cast<int>((seconds - hours * 3600) / 60);
    const int secs = static_cast<int>(seconds - hours * 3600 - minutes * 60);
    const int millis = static_cast<int>((seconds - static_cast<int>(seconds)) * 1000);
    
    const QChar separator = vtt ? u'.' : u',';
    return u"%1:%2:%3%4%5"_s
        .arg(hours, 2, 10, u'0')
        .arg(minutes, 2, 10, u'0')
        .arg(secs, 2, 10, u'0')
        .arg(separator)
        .arg(millis, 3, 10, u'0');
}

std::optional<std::vector<SubtitleEntry>> SubtitleFormat::parseSRT(const QString &content, const Rational &rate)
{
    std::vector<SubtitleEntry> entries;
    const QStringList blocks = content.split(u"\n\n"_s, Qt::SkipEmptyParts);
    
    for (const QString &block : blocks) {
        const QStringList lines = block.split(u'\n', Qt::SkipEmptyParts);
        if (lines.size() < 3) {
            continue;
        }
        
        static const QRegularExpression timeRe(uR"((.+?)\s+-->\s+(.+))"_s);
        const QRegularExpressionMatch match = timeRe.match(lines[1]);
        if (!match.hasMatch()) {
            continue;
        }
        
        const std::optional<RationalTime> start = parseTimecode(match.captured(1).trimmed(), rate);
        const std::optional<RationalTime> end = parseTimecode(match.captured(2).trimmed(), rate);
        
        if (!start || !end) {
            continue;
        }
        
        QString text;
        for (int i = 2; i < lines.size(); ++i) {
            if (i > 2) {
                text += u'\n';
            }
            text += lines[i];
        }
        
        entries.push_back(SubtitleEntry{*start, *end, text});
    }
    
    return entries;
}

std::optional<std::vector<SubtitleEntry>> SubtitleFormat::parseVTT(const QString &content, const Rational &rate)
{
    if (!content.startsWith(u"WEBVTT"_s)) {
        return std::nullopt;
    }
    
    std::vector<SubtitleEntry> entries;
    const QStringList blocks = content.split(u"\n\n"_s, Qt::SkipEmptyParts);
    
    for (const QString &block : blocks) {
        if (block.startsWith(u"WEBVTT"_s) || block.startsWith(u"NOTE"_s)) {
            continue;
        }
        
        const QStringList lines = block.split(u'\n', Qt::SkipEmptyParts);
        if (lines.isEmpty()) {
            continue;
        }
        
        int timeLineIndex = 0;
        if (!lines[0].contains(u"-->"_s)) {
            timeLineIndex = 1;
        }
        
        if (timeLineIndex >= lines.size()) {
            continue;
        }
        
        static const QRegularExpression timeRe(uR"((.+?)\s+-->\s+(.+))"_s);
        const QRegularExpressionMatch match = timeRe.match(lines[timeLineIndex]);
        if (!match.hasMatch()) {
            continue;
        }
        
        const std::optional<RationalTime> start = parseTimecode(match.captured(1).trimmed(), rate);
        const std::optional<RationalTime> end = parseTimecode(match.captured(2).trimmed(), rate);
        
        if (!start || !end) {
            continue;
        }
        
        QString text;
        for (int i = timeLineIndex + 1; i < lines.size(); ++i) {
            if (i > timeLineIndex + 1) {
                text += u'\n';
            }
            text += lines[i];
        }
        
        entries.push_back(SubtitleEntry{*start, *end, text});
    }
    
    return entries;
}

QString SubtitleFormat::formatSRT(const std::vector<SubtitleEntry> &entries)
{
    QString result;
    int index = 1;
    
    for (const SubtitleEntry &entry : entries) {
        result += QString::number(index++) + u'\n';
        result += formatTimecode(entry.start, false) + u" --> "_s + formatTimecode(entry.end, false) + u'\n';
        result += entry.text + u"\n\n"_s;
    }
    
    return result;
}

QString SubtitleFormat::formatVTT(const std::vector<SubtitleEntry> &entries)
{
    QString result = u"WEBVTT\n\n"_s;
    
    for (const SubtitleEntry &entry : entries) {
        result += formatTimecode(entry.start, true) + u" --> "_s + formatTimecode(entry.end, true) + u'\n';
        result += entry.text + u"\n\n"_s;
    }
    
    return result;
}

} // namespace vedit
