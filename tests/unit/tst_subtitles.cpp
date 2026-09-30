// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/subtitle/SubtitleFormat.h"

#include <QTest>

using namespace vedit;
using namespace Qt::StringLiterals;

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
        
        const Rational rate(30);
        const auto entries = SubtitleFormat::parseSRT(srt, rate);
        
        QVERIFY(entries.has_value());
        QCOMPARE(entries->size(), 2u);
        
        QCOMPARE((*entries)[0].text, u"This is the first subtitle"_s);
        QCOMPARE((*entries)[1].text, u"This is the second subtitle\nWith two lines"_s);
    }
    
    void formatSRT()
    {
        std::vector<SubtitleEntry> entries;
        const Rational rate(30);
        
        entries.push_back(SubtitleEntry{
            RationalTime::fromSeconds(Rational(20), rate, Rounding::NearestEven),
            RationalTime::fromSeconds(Rational(24), rate, Rounding::NearestEven),
            u"First subtitle"_s
        });
        
        const QString srt = SubtitleFormat::formatSRT(entries);
        
        QVERIFY(srt.contains(u"1\n"_s));
        QVERIFY(srt.contains(u"00:00:20,000 --> 00:00:24,000"_s));
        QVERIFY(srt.contains(u"First subtitle"_s));
    }
};

QTEST_MAIN(TestSubtitles)
#include "tst_subtitles.moc"
