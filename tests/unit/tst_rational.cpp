// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/time/Rational.h"
#include "core/time/RationalTime.h"

#include <QTest>

#include <stdexcept>

using namespace vedit;
using namespace Qt::StringLiterals;

class TestRational : public QObject
{
    Q_OBJECT

private slots:
    void normalization()
    {
        const Rational r(60000, 2002);
        QCOMPARE(r.num(), 30000);
        QCOMPARE(r.den(), 1001);
        const Rational negativeDen(3, -6);
        QCOMPARE(negativeDen.num(), -1);
        QCOMPARE(negativeDen.den(), 2);
        const Rational zero(0, 17);
        QCOMPARE(zero.num(), 0);
        QCOMPARE(zero.den(), 1);
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, Rational(1, 0));
    }

    void arithmetic()
    {
        const Rational a(1, 3);
        const Rational b(1, 6);
        QCOMPARE(a + b, Rational(1, 2));
        QCOMPARE(a - b, Rational(1, 6));
        QCOMPARE(a * b, Rational(1, 18));
        QCOMPARE(a / b, Rational(2));
        QCOMPARE(-a, Rational(-1, 3));
        QVERIFY(Rational(30000, 1001) < Rational(30));
        QVERIFY(Rational(-1, 2) < Rational(0));
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, a / Rational(0));
    }

    void overflowIsDetected()
    {
        const Rational big(std::numeric_limits<std::int64_t>::max() / 2 + 1);
        QVERIFY_THROWS_EXCEPTION(std::overflow_error, big + big);
    }

    void rounding_data()
    {
        QTest::addColumn<qint64>("num");
        QTest::addColumn<qint64>("den");
        QTest::addColumn<qint64>("nearest");
        QTest::addColumn<qint64>("floor");
        QTest::addColumn<qint64>("ceil");
        QTest::newRow("exact") << qint64(6) << qint64(3) << qint64(2) << qint64(2) << qint64(2);
        QTest::newRow("below half") << qint64(7) << qint64(5) << qint64(1) << qint64(1) << qint64(2);
        QTest::newRow("half to even (down)") << qint64(5) << qint64(2) << qint64(2) << qint64(2) << qint64(3);
        QTest::newRow("half to even (up)") << qint64(7) << qint64(2) << qint64(4) << qint64(3) << qint64(4);
        QTest::newRow("negative half") << qint64(-5) << qint64(2) << qint64(-2) << qint64(-3) << qint64(-2);
        QTest::newRow("negative") << qint64(-7) << qint64(5) << qint64(-1) << qint64(-2) << qint64(-1);
    }

    void rounding()
    {
        QFETCH(qint64, num);
        QFETCH(qint64, den);
        QFETCH(qint64, nearest);
        QFETCH(qint64, floor);
        QFETCH(qint64, ceil);
        const Rational r(num, den);
        QCOMPARE(r.toInteger(Rounding::NearestEven), nearest);
        QCOMPARE(r.toInteger(Rounding::Floor), floor);
        QCOMPARE(r.toInteger(Rounding::Ceil), ceil);
    }

    void rationalStrings()
    {
        QCOMPARE(Rational(30000, 1001).toString(), u"30000/1001"_s);
        QCOMPARE(Rational(25).toString(), u"25"_s);
        QCOMPARE(Rational::fromString(u"30000/1001"), std::optional(Rational(30000, 1001)));
        QCOMPARE(Rational::fromString(u"-3/6"), std::optional(Rational(-1, 2)));
        QCOMPARE(Rational::fromString(u"48000"), std::optional(Rational(48000)));
        for (const QString &bad : {u""_s, u"/"_s, u"1/"_s, u"/2"_s, u"1/0"_s, u"1/-2"_s, u" 1"_s, u"1 "_s,
                                   u"+1"_s, u"1.5"_s, u"a/b"_s, u"-"_s, u"1/2/3"_s}) {
            QVERIFY2(!Rational::fromString(bad).has_value(), qPrintable(bad));
        }
    }

    void timeStrings()
    {
        const RationalTime t(150, Rational(30000, 1001));
        QCOMPARE(t.toString(), u"150@30000/1001"_s);
        QVERIFY(RationalTime::fromString(u"150@30000/1001")->isIdenticalTo(t));
        QVERIFY(RationalTime::fromString(u"-3@25")->isIdenticalTo(RationalTime(-3, Rational(25))));
        for (const QString &bad : {u"150"_s, u"@30"_s, u"1@0"_s, u"1@-30"_s, u"1.5@30"_s, u"1/2@30"_s,
                                   u"1@"_s, u"1@30@30"_s}) {
            QVERIFY2(!RationalTime::fromString(bad).has_value(), qPrintable(bad));
        }
    }

    void ntscRates()
    {
        // 29.97 fps: 30000 frames last exactly 1001 seconds.
        const RationalTime frames(30000, Rational(30000, 1001));
        QCOMPARE(frames.seconds(), Rational(1001));
        // 1 second of 48 kHz audio at 23.976 fps is 23.976 frames: rounding is explicit.
        const RationalTime oneSecond(48000, Rational(48000));
        const Rational fps23976(24000, 1001);
        QCOMPARE(oneSecond.rescaled(fps23976, Rounding::Floor).value(), 23);
        QCOMPARE(oneSecond.rescaled(fps23976, Rounding::NearestEven).value(), 24);
        QVERIFY(!oneSecond.rescaledExact(fps23976).has_value());
        // 1001 seconds are exactly 24000 frames at 23.976.
        const RationalTime exact(1001, Rational(1));
        QCOMPARE(exact.rescaledExact(fps23976)->value(), 24000);
        // 59.94 <-> 29.97: every other frame is exact.
        const RationalTime f5994(120, Rational(60000, 1001));
        QCOMPARE(f5994.rescaledExact(Rational(30000, 1001))->value(), 60);
    }

    void noDriftOverLongDurations()
    {
        // Adding one 29.97 frame 1'000'000 times stays exact (a double would drift).
        const Rational rate(30000, 1001);
        RationalTime t(0, rate);
        const RationalTime oneFrame(1, rate);
        for (int i = 0; i < 1000000; ++i) {
            t += oneFrame;
        }
        QCOMPARE(t.value(), 1000000);
        QCOMPARE(t.seconds(), Rational(1000000LL * 1001, 30000));
    }

    void comparisonAcrossRates()
    {
        QVERIFY(RationalTime(1, Rational(1)) == RationalTime(30, Rational(30)));
        QVERIFY(!RationalTime(1, Rational(1)).isIdenticalTo(RationalTime(30, Rational(30))));
        QVERIFY(RationalTime(29, Rational(30)) < RationalTime(1, Rational(1)));
        QVERIFY(RationalTime(1001, Rational(30000, 1001)) > RationalTime(1001, Rational(30)));
    }

    void arithmeticRequiresSameRate()
    {
        const RationalTime a(10, Rational(25));
        const RationalTime b(10, Rational(30));
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, a + b);
        QCOMPARE((a + a).value(), 20);
        QCOMPARE((a - a * 3).value(), -20);
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, RationalTime(1, Rational(0)));
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, RationalTime(1, Rational(-25)));
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, RationalTime(1, Rational(1LL << 40)));
    }

    void timeRange()
    {
        const Rational rate(25);
        const TimeRange range(RationalTime(10, rate), RationalTime(5, rate));
        QCOMPARE(range.end().value(), 15);
        QVERIFY(range.contains(RationalTime(10, rate)));
        QVERIFY(range.contains(RationalTime(14, rate)));
        QVERIFY(!range.contains(RationalTime(15, rate)));
        QVERIFY(range.intersects(TimeRange(RationalTime(14, rate), RationalTime(3, rate))));
        QVERIFY(!range.intersects(TimeRange(RationalTime(15, rate), RationalTime(3, rate))));
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, TimeRange(RationalTime(0, rate), RationalTime(-1, rate)));
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument,
                                 TimeRange(RationalTime(0, rate), RationalTime(1, Rational(30))));
    }
};

QTEST_GUILESS_MAIN(TestRational)
#include "tst_rational.moc"
