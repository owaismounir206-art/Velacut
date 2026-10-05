// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/project/Captions.h"
#include "core/subtitle/SubtitleFormat.h"

#include <QTest>

using namespace vedit;
using namespace Qt::StringLiterals;

namespace {

const Rational kRate(30);

RationalTime frames(std::int64_t n)
{
    return RationalTime(n, kRate);
}

} // namespace

class TestSubtitles : public QObject
{
    Q_OBJECT

private slots:
    void parseSRTBasic()
    {
        const QString srt = uR"(1
00:00:20,000 --> 00:00:24,400
This is the first subtitle

2
00:00:30,000 --> 00:00:33,500
This is the second subtitle
With two lines
)"_s;
        const auto entries = SubtitleFormat::parseSRT(srt, kRate);
        QVERIFY(entries.has_value());
        QCOMPARE(entries->size(), 2u);
        QCOMPARE((*entries)[0].text, u"This is the first subtitle"_s);
        QCOMPARE((*entries)[0].start, frames(600));
        QCOMPARE((*entries)[0].end, frames(732));
        QCOMPARE((*entries)[1].text, u"This is the second subtitle\nWith two lines"_s);
        QCOMPARE((*entries)[1].end, frames(1005));
    }

    void parseSRTFromWindowsWithTags()
    {
        // Byte order mark, CRLF, formatting tags, an SSA override, no blank line at the end.
        const QString srt = QChar(0xFEFF) + u"1\r\n00:00:01,000 --> 00:00:02,500\r\n<i>Ciao</i> {\\an8}a <b>tutti</b>\r\n\r\n"
                                            u"2\r\n00:00:03,000 --> 00:00:04,000\r\nFish &amp; chips"_s;
        const auto entries = SubtitleFormat::parse(srt, kRate);
        QVERIFY(entries.has_value());
        QCOMPARE(entries->size(), 2u);
        QCOMPARE((*entries)[0].text, u"Ciao a tutti"_s);
        QCOMPARE((*entries)[0].end, frames(75));
        QCOMPARE((*entries)[1].text, u"Fish & chips"_s);
    }

    void parseVTT()
    {
        const QString vtt = uR"(WEBVTT - with a title

NOTE a comment
that spans two lines

STYLE
::cue { color: yellow }

intro
00:01.000 --> 00:02.000 align:start position:10%
<v Anna>Hello <00:01.500>there</v>

01:00:00.000 --> 01:00:01.000
Late
)"_s;
        const auto entries = SubtitleFormat::parse(vtt, kRate);
        QVERIFY(entries.has_value());
        QCOMPARE(entries->size(), 2u);
        QCOMPARE((*entries)[0].text, u"Hello there"_s);
        QCOMPARE((*entries)[0].start, frames(30));
        QCOMPARE((*entries)[0].end, frames(60));
        QCOMPARE((*entries)[1].start, frames(3600 * 30));
        // Not a WebVTT file.
        QVERIFY(!SubtitleFormat::parseVTT(u"1\n00:00:01,000 --> 00:00:02,000\nx"_s, kRate).has_value());
        // Not a subtitle file at all.
        QVERIFY(!SubtitleFormat::parse(u"just some text\nwithout times"_s, kRate).has_value());
    }

    void formatRoundTrip()
    {
        const std::vector<SubtitleEntry> entries{
            {frames(15), frames(45), u"Primo"_s},
            {RationalTime(30000, Rational(30000, 1001)), RationalTime(30060, Rational(30000, 1001)), u"A < B & C"_s},
        };
        const QString srt = SubtitleFormat::formatSRT(entries);
        QVERIFY(srt.contains(u"1\n00:00:00,500 --> 00:00:01,500\nPrimo\n"_s));
        // 30000 frames at 29.97 fps = 1001 s.
        QVERIFY(srt.contains(u"00:16:41,000 --> 00:16:43,002"_s));
        const QString vtt = SubtitleFormat::formatVTT(entries);
        QVERIFY(vtt.startsWith(u"WEBVTT\n\n00:00:00.500 --> 00:00:01.500\n"_s));
        QVERIFY(vtt.contains(u"A &lt; B &amp; C"_s));
        const auto back = SubtitleFormat::parse(vtt, kRate);
        QVERIFY(back.has_value());
        QCOMPARE(back->size(), 2u);
        QCOMPARE((*back)[0], entries[0]);
        QCOMPARE((*back)[1].text, u"A < B & C"_s);
    }

    void wordsAreSpreadByLength()
    {
        SubtitleClipData line;
        line.text = u"Hi  everybody\nhere"_s;
        const std::vector<TimedWord> words = captions::timedWords(line, frames(40));
        QCOMPARE(words.size(), 3u);
        QCOMPARE(words[0].text, u"Hi"_s);
        QCOMPARE(words[0].start, frames(0));
        // 3 + 10 + 5 = 18 units over 40 frames: "everybody" takes the longest.
        QCOMPARE(words[1].start, frames(6));
        QCOMPARE(words[2].start, frames(28));
        QCOMPARE(words[2].end, frames(40));
        QVERIFY(words[1].end - words[1].start > words[0].end - words[0].start);
    }

    void ownWordsAreKeptWhileTheyMatch()
    {
        SubtitleClipData line;
        line.text = u"one two"_s;
        line.words = {TimedWord{u"one"_s, frames(0), frames(5)}, TimedWord{u"two"_s, frames(20), frames(30)}};
        QCOMPARE(captions::timedWords(line, frames(30)), line.words);
        // An edited text no longer matches its timings: spread again.
        line.text = u"one three"_s;
        const auto words = captions::timedWords(line, frames(30));
        QCOMPARE(words.size(), 2u);
        QCOMPARE(words[1].text, u"three"_s);
        QCOMPARE(words[1].end, frames(30));
    }

    void splitKeepsWordsOnTheirSide()
    {
        SubtitleClipData line;
        line.text = u"uno due tre"_s;
        line.words = {TimedWord{u"uno"_s, frames(0), frames(10)}, TimedWord{u"due"_s, frames(10), frames(20)},
                      TimedWord{u"tre"_s, frames(20), frames(30)}};
        // The cut falls in the second half of "due": it stays in the first part.
        const auto [first, second] = captions::split(line, frames(30), frames(16));
        QCOMPARE(first.text, u"uno due"_s);
        QCOMPARE(second.text, u"tre"_s);
        QCOMPARE(first.words.back().end, frames(16));
        QCOMPARE(second.words.front().start, frames(4));
        QCOMPARE(second.words.front().end, frames(14));
        // Times of words from a file at another rate are converted.
        SubtitleClipData other;
        other.text = u"a b"_s;
        other.words = {TimedWord{u"a"_s, RationalTime(0, Rational(1000)), RationalTime(500, Rational(1000))},
                       TimedWord{u"b"_s, RationalTime(500, Rational(1000)), RationalTime(1000, Rational(1000))}};
        const auto [left, right] = captions::split(other, frames(30), frames(15));
        QCOMPARE(left.text, u"a"_s);
        QCOMPARE(right.words.front().start, frames(0));
        QCOMPARE(right.words.front().end, frames(15));
    }

    void entriesAndLines()
    {
        const std::vector<SubtitleEntry> entries{
            {frames(0), frames(30), u"Due\nrighe"_s},
            {frames(30), frames(30), u"vuota"_s},
            {frames(40), frames(50), u"   "_s},
        };
        const std::vector<captions::CaptionLine> lines = captions::linesOf(entries);
        QCOMPARE(lines.size(), 1u);
        QCOMPARE(lines[0].text, u"Due righe"_s);
        Track track;
        track.kind = TrackKind::Text;
        track.captions = true;
        Clip clip;
        clip.id = ClipId::create();
        clip.start = frames(10);
        clip.duration = frames(20);
        clip.payload = SubtitleClipData{u"Ciao"_s, {}, std::nullopt, {}};
        track.clips.push_back(clip);
        const auto back = captions::entriesOf(track);
        QCOMPARE(back.size(), 1u);
        QCOMPARE(back[0], (SubtitleEntry{frames(10), frames(30), u"Ciao"_s}));
    }
};

QTEST_MAIN(TestSubtitles)
#include "tst_subtitles.moc"
