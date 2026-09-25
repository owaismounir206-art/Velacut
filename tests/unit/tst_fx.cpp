// SPDX-License-Identifier: GPL-3.0-or-later
#include "fx/Composite.h"

#include <QTest>

#include <array>

using namespace vedit::fx;

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
};

QTEST_GUILESS_MAIN(TestFx)
#include "tst_fx.moc"
