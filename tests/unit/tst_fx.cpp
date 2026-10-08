// SPDX-License-Identifier: GPL-3.0-or-later
#include "fx/Composite.h"
#include "fx/Loudness.h"

#include <QTest>

#include <array>
#include <cmath>
#include <vector>

using namespace velacut::fx;

namespace {
using Pixel = std::array<std::uint8_t, 4>;

Pixel over(Pixel destination, Pixel source, int opacity = 255)
{
    compositeOver(ImageView{destination.data(), 1, 1, 4}, ConstImageView{source.data(), 1, 1, 4}, opacity);
    return destination;
}
} // namespace

class TestFx : public QObject
{
    Q_OBJECT

private slots:
    void opaqueSourceReplaces() { QCOMPARE(over({10, 20, 30, 255}, {200, 100, 50, 255}), (Pixel{200, 100, 50, 255})); }

    void transparentSourceKeepsDestination()
    {
        QCOMPARE(over({10, 20, 30, 255}, {200, 100, 50, 0}), (Pixel{10, 20, 30, 255}));
        QCOMPARE(over({10, 20, 30, 255}, {200, 100, 50, 255}, 0), (Pixel{10, 20, 30, 255}));
    }

    void halfAlphaBlendsOnOpaque()
    {
        // 50% of white over black = mid grey (128 with rounding of 255 * 128/255).
        const Pixel result = over({0, 0, 0, 255}, {255, 255, 255, 128});
        QCOMPARE(result[3], std::uint8_t(255));
        QCOMPARE(int(result[0]), 128);
    }

    void opacityScalesSourceAlpha()
    {
        QCOMPARE(over({0, 0, 0, 255}, {255, 255, 255, 255}, 128), over({0, 0, 0, 255}, {255, 255, 255, 128}));
    }

    void overTransparentDestinationKeepsSourceColor()
    {
        // Straight alpha: colour is not darkened by the empty destination.
        const Pixel result = over({0, 0, 0, 0}, {200, 100, 50, 100});
        QCOMPARE(result, (Pixel{200, 100, 50, 100}));
    }

    void semiTransparentLayersCombine()
    {
        const Pixel result = over({255, 0, 0, 128}, {0, 0, 255, 128});
        // outA = 128 + 128 * 127/255 ≈ 192
        QCOMPARE(int(result[3]), 192);
        QVERIFY(result[2] > result[0]); // the top layer dominates
    }

    void rowsAndStrides()
    {
        // 3x2 destination inside a wider buffer (stride 16): only the requested rows change.
        std::array<std::uint8_t, 32> destination{};
        std::array<std::uint8_t, 24> source{};
        source.fill(255);
        compositeOver(ImageView{destination.data(), 3, 2, 16}, ConstImageView{source.data(), 3, 2, 12}, 255, 1, 2);
        QCOMPARE(int(destination[0]), 0);   // row 0 untouched
        QCOMPARE(int(destination[16]), 255); // row 1 composited
        QCOMPARE(int(destination[12]), 0);   // padding of row 0 untouched
    }

    void loudnessSilence()
    {
        std::vector<float> silence(48000 * 2, 0.0f);
        const auto res = measureLoudness(silence, 2, 48000);
        QVERIFY(res.integratedLufs <= -70.0);
        QCOMPARE(gainAdjustmentForTargetLufs(res.integratedLufs, -14.0), 0.0);
    }

    void loudnessSineTone()
    {
        const int sampleRate = 48000;
        const int seconds = 2;
        const int numFrames = sampleRate * seconds;
        std::vector<float> samples(numFrames * 2);
        const double amp = std::pow(10.0, -20.0 / 20.0);
        for (int i = 0; i < numFrames; ++i) {
            const float val = static_cast<float>(amp * std::sin(2.0 * M_PI * 1000.0 * i / sampleRate));
            samples[2 * i] = val;
            samples[2 * i + 1] = val;
        }
        const auto res = measureLoudness(samples, 2, sampleRate);
        QVERIFY(res.integratedLufs > -30.0 && res.integratedLufs < -10.0);
        QVERIFY(res.truePeakDb > -21.0 && res.truePeakDb < -19.0);
        const double adj = gainAdjustmentForTargetLufs(res.integratedLufs, -14.0);
        QVERIFY(std::abs((res.integratedLufs + adj) - (-14.0)) < 0.001);
    }
};

QTEST_GUILESS_MAIN(TestFx)
#include "tst_fx.moc"
