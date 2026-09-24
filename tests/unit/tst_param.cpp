// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/effects/Easing.h"
#include "core/effects/Param.h"

#include <QTest>

using namespace vedit;
using namespace Qt::StringLiterals;

namespace {
const Rational kRate(30);
RationalTime frame(std::int64_t n)
{
    return RationalTime(n, kRate);
}
} // namespace

class TestParam : public QObject
{
    Q_OBJECT

private slots:
    void easingEndpoints()
    {
        for (const QString &name : Easing::presetNames()) {
            const Easing easing = *Easing::fromName(name);
            QCOMPARE(easing.name(), name);
            QCOMPARE(easing.apply(0.0), 0.0);
            QCOMPARE(easing.apply(1.0), 1.0);
            QVERIFY2(std::isfinite(easing.apply(0.37)), qPrintable(name));
        }
        QVERIFY(!Easing::fromName(u"doesNotExist").has_value());
    }

    void easingValues()
    {
        // Symmetric curves pass through the midpoint.
        QVERIFY(qAbs(Easing::preset(Easing::Preset::EaseInOut).apply(0.5) - 0.5) < 1e-6);
        QVERIFY(qAbs(Easing::preset(Easing::Preset::EaseInOutCubic).apply(0.5) - 0.5) < 1e-12);
        QCOMPARE(Easing::preset(Easing::Preset::EaseInQuad).apply(0.5), 0.25);
        // Ease-in starts slow, ease-out starts fast.
        QVERIFY(Easing::preset(Easing::Preset::EaseIn).apply(0.25) < 0.25);
        QVERIFY(Easing::preset(Easing::Preset::EaseOut).apply(0.25) > 0.25);
        // Back overshoots.
        QVERIFY(Easing::preset(Easing::Preset::EaseInBack).apply(0.2) < 0.0);
        // A linear custom bezier is the identity.
        const Easing identity = *Easing::cubicBezier(0.25, 0.25, 0.75, 0.75);
        for (double t : {0.1, 0.33, 0.5, 0.9}) {
            QVERIFY(qAbs(identity.apply(t) - t) < 1e-7);
        }
        QVERIFY(!Easing::cubicBezier(-0.1, 0, 1, 1).has_value());
        QVERIFY(!Easing::cubicBezier(0, 0, 1.5, 1).has_value());
    }

    void colorStrings()
    {
        const Color color{0x12, 0xAB, 0xFF, 0x80};
        QCOMPARE(color.toString(), u"#12ABFF80"_s);
        QCOMPARE(Color::fromString(u"#12abff80"), std::optional(color));
        QVERIFY(!Color::fromString(u"#12ABFF").has_value());
        QVERIFY(!Color::fromString(u"12ABFF800").has_value());
        QVERIFY(!Color::fromString(u"#12ABFG80").has_value());
    }

    void colorInterpolationIsLinearLight()
    {
        const Color black{0, 0, 0, 255};
        const Color white{255, 255, 255, 255};
        const Color mid = Color::interpolate(black, white, 0.5);
        // 50% linear light is sRGB ~188, not 128.
        QCOMPARE(int(mid.r), 188);
        QCOMPARE(mid.r, mid.g);
        QCOMPARE(Color::interpolate(black, white, 0.0), black);
        QCOMPARE(Color::interpolate(black, white, 1.0), white);
    }

    void staticParam()
    {
        const Param p(0.75);
        QVERIFY(!p.isAnimated());
        QCOMPARE(p.numberAt(frame(100)), 0.75);
    }

    void keyframeInterpolation()
    {
        Param p;
        QVERIFY(p.setKeyframes({
            Keyframe{frame(10), 0.0, Interpolation::Linear, {}},
            Keyframe{frame(20), 1.0, Interpolation::Hold, {}},
            Keyframe{frame(30), 3.0, Interpolation::Bezier, Easing::preset(Easing::Preset::EaseInQuad)},
            Keyframe{frame(40), 5.0, Interpolation::Linear, {}},
        }));
        QCOMPARE(p.numberAt(frame(0)), 0.0);   // before the first keyframe
        QCOMPARE(p.numberAt(frame(15)), 0.5);  // linear
        QCOMPARE(p.numberAt(frame(25)), 1.0);  // hold
        QCOMPARE(p.numberAt(frame(30)), 3.0);  // on a keyframe
        QCOMPARE(p.numberAt(frame(35)), 3.5);  // eased: 3 + 2 * 0.25
        QCOMPARE(p.numberAt(frame(99)), 5.0);  // after the last keyframe
        // Times at another rate are compared exactly: 0.5 s == frame 15 at 30 fps.
        QCOMPARE(p.numberAt(RationalTime(1, Rational(2))), 0.5);
    }

    void vectorAndColorKeyframes()
    {
        Param position;
        QVERIFY(position.setKeyframes({Keyframe{frame(0), Vec2{0.0, 0.0}, Interpolation::Linear, {}},
                                       Keyframe{frame(10), Vec2{1.0, -1.0}, Interpolation::Linear, {}}}));
        QCOMPARE(std::get<Vec2>(position.valueAt(frame(5))), (Vec2{0.5, -0.5}));

        Param color;
        QVERIFY(color.setKeyframes({Keyframe{frame(0), Color{0, 0, 0, 0}, Interpolation::Linear, {}},
                                    Keyframe{frame(10), Color{255, 255, 255, 255}, Interpolation::Linear, {}}}));
        const Color mid = std::get<Color>(color.valueAt(frame(5)));
        QCOMPARE(int(mid.a), 128);
    }

    void invalidKeyframesAreRejected()
    {
        Param p(1.0);
        // Not increasing.
        QVERIFY(!p.setKeyframes({Keyframe{frame(10), 0.0, Interpolation::Linear, {}},
                                 Keyframe{frame(10), 1.0, Interpolation::Linear, {}}}));
        // Mixed types.
        QVERIFY(!p.setKeyframes({Keyframe{frame(0), 0.0, Interpolation::Linear, {}},
                                 Keyframe{frame(10), Vec2{1, 1}, Interpolation::Linear, {}}}));
        // Not animatable.
        QVERIFY(!p.setKeyframes({Keyframe{frame(0), true, Interpolation::Linear, {}}}));
        QVERIFY(!p.isAnimated());
        QCOMPARE(p.numberAt(frame(0)), 1.0);
    }
};

QTEST_GUILESS_MAIN(TestParam)
#include "tst_param.moc"
