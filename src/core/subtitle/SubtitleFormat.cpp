// SPDX-License-Identifier: GPL-3.0-or-later
#include "SubtitleFormat.h"

#include <QRegularExpression>
#include <QStringList>

using namespace Qt::StringLiterals;

namespace vedit {

namespace {

const Rational kMilliseconds(1000);

// Lines of a file from any system (Windows line ends, byte order mark).
QStringList linesOf(QString content)
{
    if (content.startsWith(QChar(0xFEFF))) {
        content.remove(0, 1);
    }
    content.replace(u"\r\n"_s, u"\n"_s);
    content.replace(u'\r', u'\n');
    return content.split(u'\n');
}

// "00:01:02,500" (SRT), "00:01:02.500" or "01:02.500" (WebVTT, hours optional) → milliseconds.
std::optional<std::int64_t> milliseconds(const QString &timecode)
{
    static const QRegularExpression re(uR"(^(?:(\d+):)?(\d{1,2}):(\d{1,2})[,.](\d{1,3})$)"_s);
    const QRegularExpressionMatch match = re.match(timecode.trimmed());
    if (!match.hasMatch()) {
        return std::nullopt;
    }
    const std::int64_t hours = match.captured(1).toLongLong();
    const std::int64_t minutes = match.captured(2).toLongLong();
    const std::int64_t seconds = match.captured(3).toLongLong();
    QString fraction = match.captured(4);
    while (fraction.size() < 3) {
        fraction += u'0'; // ",5" is half a second
    }
    if (minutes > 59 || seconds > 59) {
        return std::nullopt;
    }
    return ((hours * 60 + minutes) * 60 + seconds) * 1000 + fraction.toLongLong();
}

// The two times of a cue line "start --> end [settings]".
std::optional<std::pair<RationalTime, RationalTime>> cueTimes(const QString &line, const Rational &rate)
{
    const qsizetype arrow = line.indexOf(u"-->"_s);
    if (arrow < 0) {
        return std::nullopt;
    }
    const QString endPart = line.mid(arrow + 3).trimmed();
    const auto start = milliseconds(line.left(arrow));
    const auto end = milliseconds(endPart.section(u' ', 0, 0, QString::SectionSkipEmpty));
    if (!start || !end || *end < *start) {
        return std::nullopt;
    }
    return std::pair{RationalTime::fromSeconds(Rational(*start, 1000), rate, Rounding::NearestEven),
                     RationalTime::fromSeconds(Rational(*end, 1000), rate, Rounding::NearestEven)};
}

// Plain text of a cue: formatting tags (<i>, <b>, <c.yellow>, <v Anna>, WebVTT word times <00:01.200>, SSA
// overrides {\an8}) and entities removed.
QString plainText(QString text)
{
    static const QRegularExpression tags(uR"(<[^>]*>|\{\\[^}]*\})"_s);
    text.remove(tags);
    text.replace(u"&lt;"_s, u"<"_s);
    text.replace(u"&gt;"_s, u">"_s);
    text.replace(u"&nbsp;"_s, u" "_s);
    text.replace(u"&lrm;"_s, QString());
    text.replace(u"&rlm;"_s, QString());
    text.replace(u"&amp;"_s, u"&"_s);
    return text.trimmed();
}

// Cues of SRT and WebVTT alike: a time line, then text lines up to an empty line. Anything else (numbers, cue
// identifiers, NOTE/STYLE/REGION blocks of WebVTT) is skipped.
std::vector<SubtitleEntry> cues(const QStringList &lines, const Rational &rate)
{
    std::vector<SubtitleEntry> entries;
    for (qsizetype i = 0; i < lines.size(); ++i) {
        const auto times = cueTimes(lines[i], rate);
        if (!times) {
            continue;
        }
        QStringList text;
        qsizetype next = i + 1;
        while (next < lines.size() && !lines[next].trimmed().isEmpty() && !cueTimes(lines[next], rate)) {
            text << lines[next];
            ++next;
        }
        i = next - 1;
        const QString plain = plainText(text.join(u'\n'));
        if (!plain.isEmpty()) {
            entries.push_back(SubtitleEntry{times->first, times->second, plain});
        }
    }
    return entries;
}

QString timecode(const RationalTime &time, QChar separator)
{
    const std::int64_t total = std::max<std::int64_t>(0, time.rescaled(kMilliseconds, Rounding::NearestEven).value());
    return u"%1:%2:%3%4%5"_s.arg(total / 3'600'000, 2, 10, u'0')
        .arg(total / 60'000 % 60, 2, 10, u'0')
        .arg(total / 1000 % 60, 2, 10, u'0')
        .arg(separator)
        .arg(total % 1000, 3, 10, u'0');
}

} // namespace

std::optional<std::vector<SubtitleEntry>> SubtitleFormat::parseSRT(const QString &content, const Rational &rate)
{
    std::vector<SubtitleEntry> entries = cues(linesOf(content), rate);
    if (entries.empty() && !content.trimmed().isEmpty()) {
        return std::nullopt; // not a subtitle file
    }
    return entries;
}

std::optional<std::vector<SubtitleEntry>> SubtitleFormat::parseVTT(const QString &content, const Rational &rate)
{
    const QStringList lines = linesOf(content);
    if (lines.isEmpty() || !lines.front().startsWith(u"WEBVTT"_s)) {
        return std::nullopt;
    }
    return cues(lines.mid(1), rate);
}

std::optional<std::vector<SubtitleEntry>> SubtitleFormat::parse(const QString &content, const Rational &rate)
{
    QString start = content.left(16);
    if (start.startsWith(QChar(0xFEFF))) {
        start.remove(0, 1);
    }
    return start.startsWith(u"WEBVTT"_s) ? parseVTT(content, rate) : parseSRT(content, rate);
}

QString SubtitleFormat::formatSRT(const std::vector<SubtitleEntry> &entries)
{
    QString result;
    int index = 1;
    for (const SubtitleEntry &entry : entries) {
        result += QString::number(index++) + u'\n';
        result += timecode(entry.start, u',') + u" --> "_s + timecode(entry.end, u',') + u'\n';
        result += entry.text + u"\n\n"_s;
    }
    return result;
}

QString SubtitleFormat::formatVTT(const std::vector<SubtitleEntry> &entries)
{
    QString result = u"WEBVTT\n\n"_s;
    for (const SubtitleEntry &entry : entries) {
        QString text = entry.text;
        text.replace(u'&', u"&amp;"_s);
        text.replace(u'<', u"&lt;"_s);
        text.replace(u'>', u"&gt;"_s);
        result += timecode(entry.start, u'.') + u" --> "_s + timecode(entry.end, u'.') + u'\n';
        result += text + u"\n\n"_s;
    }
    return result;
}

} // namespace vedit
