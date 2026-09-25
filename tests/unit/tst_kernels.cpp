// SPDX-License-Identifier: GPL-3.0-or-later
// CPU reference kernels of Phase 2 (SPEC 1bis rule 1): transform, colour, spatial filters, transitions, audio gain.
#include "fx/Audio.h"
#include "fx/Color.h"
#include "fx/Enhance.h"
#include "fx/Library.h"
#include "fx/Transform.h"
#include "fx/Transition.h"

#include <QSet>
#include <QTest>

#include <array>
#include <cmath>
#include <vector>

using namespace vedit::fx;

namespace {

// A small RGBA image owning its pixels.
struct Buffer
{
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> pixels;

    Buffer(int w, int h, std::array<std::uint8_t, 4> fill = {0, 0, 0, 0})
        : width(w)
        , height(h)
        , pixels(static_cast<size_t>(w * h * 4))
    {
        for (int i = 0; i < w * h; ++i) {
            std::copy(fill.begin(), fill.end(), pixels.begin() + i * 4);
        }
    }
    ImageView view() { return {pixels.data(), width, height, width * 4}; }
    ConstImageView constView() const { return {pixels.data(), width, height, width * 4}; }
    std::array<int, 4> at(int x, int y) const
    {
        const std::uint8_t *p = pixels.data() + (y * width + x) * 4;
        return {p[0], p[1], p[2], p[3]};
    }
    void set(int x, int y, std::array<std::uint8_t, 4> v)
    {
        std::copy(v.begin(), v.end(), pixels.begin() + (y * width + x) * 4);
    }
};

// A deterministic opaque test pattern: every pixel different.
Buffer pattern(int w, int h)
{
    Buffer b(w, h);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            b.set(x, y, {std::uint8_t(x * 37 % 256), std::uint8_t(y * 53 % 256), std::uint8_t((x + y) * 11 % 256), 255});
        }
    }
    return b;
}

bool near(std::array<int, 4> a, std::array<int, 4> b, int tolerance = 1)
{
    for (int i = 0; i < 4; ++i) {
        if (std::abs(a[i] - b[i]) > tolerance) {
            return false;
        }
    }
    return true;
}

} // namespace

class TestKernels : public QObject
{
    Q_OBJECT

private slots:
    // ---- transform --------------------------------------------------------------------------------------------
    void identityCopiesExactly()
    {
        const Buffer src = pattern(16, 9);
        Buffer dst(16, 9);
        drawAffine(dst.view(), src.constView(), Affine{}, SourceWindow{0, 0, 16, 9});
        QCOMPARE(dst.pixels, src.pixels);
    }

    void integerTranslation()
    {
        const Buffer src = pattern(8, 8);
        Buffer dst(8, 8);
        // Destination (x, y) shows source (x - 2, y - 1): the image moved right 2 and down 1.
        drawAffine(dst.view(), src.constView(), Affine::translation(-2, -1), SourceWindow{0, 0, 8, 8});
        QCOMPARE(dst.at(5, 4), src.at(3, 3));
        QCOMPARE(dst.at(1, 4)[3], 0); // uncovered: transparent
        QCOMPARE(dst.at(4, 0)[3], 0);
    }

    void rotationByNinetyDegrees()
    {
        // 4×4 source rotated 90° clockwise about the centre of a 4×4 destination.
        const Buffer src = pattern(4, 4);
        Buffer dst(4, 4);
        const Affine toCentre = Affine::translation(-2, -2);
        const Affine forward = Affine::translation(2, 2) * Affine::rotation(90) * toCentre; // source → destination
        drawAffine(dst.view(), src.constView(), forward.inverted(), SourceWindow{0, 0, 4, 4});
        // Clockwise: the top-left source pixel ends up top-right.
        QVERIFY(near(dst.at(3, 0), src.at(0, 0)));
        QVERIFY(near(dst.at(0, 0), src.at(0, 3)));
        QVERIFY(near(dst.at(2, 1), src.at(1, 1)));
    }

    void scalingUpInterpolates()
    {
        Buffer src(2, 1);
        src.set(0, 0, {0, 0, 0, 255});
        src.set(1, 0, {200, 200, 200, 255});
        Buffer dst(4, 1);
        drawAffine(dst.view(), src.constView(), Affine::scaling(0.5, 1.0), SourceWindow{0, 0, 2, 1});
        // Pixel centres 0.25 and 0.75 in source space: left edge region, then a ramp.
        QVERIFY(dst.at(1, 0)[0] > 0 && dst.at(1, 0)[0] < 200);
        QVERIFY(dst.at(2, 0)[0] > dst.at(1, 0)[0]);
    }

    void cropWindowHidesOutside()
    {
        const Buffer src = pattern(8, 8);
        Buffer dst(8, 8);
        drawAffine(dst.view(), src.constView(), Affine{}, SourceWindow{2, 0, 6, 8}); // left and right 2 px cropped
        QCOMPARE(dst.at(1, 3)[3], 0);
        QCOMPARE(dst.at(6, 3)[3], 0);
        QCOMPARE(dst.at(3, 3), src.at(3, 3));
    }

    void opacityAndBlendingOverTheBackground()
    {
        Buffer src(2, 2, {255, 255, 255, 255});
        Buffer dst(2, 2, {0, 0, 0, 255});
        drawAffine(dst.view(), src.constView(), Affine{}, SourceWindow{0, 0, 2, 2}, 0.5);
        QVERIFY(near(dst.at(0, 0), {128, 128, 128, 255}));
    }

    void resizeKeepsAFlatColour()
    {
        Buffer src(40, 20, {10, 200, 30, 255});
        Buffer small(7, 5);
        resizeBilinear(small.view(), src.constView());
        QVERIFY(near(small.at(3, 2), {10, 200, 30, 255}));
        Buffer big(90, 50);
        resizeBilinear(big.view(), src.constView());
        QVERIFY(near(big.at(89, 49), {10, 200, 30, 255}));
    }

    // ---- colour -------------------------------------------------------------------------------------------------
    void neutralAdjustmentChangesNothing()
    {
        Buffer image = pattern(33, 17);
        const Buffer original = image;
        ColorLut().apply(image.view());
        for (int y = 0; y < image.height; ++y) {
            for (int x = 0; x < image.width; ++x) {
                QVERIFY2(near(image.at(x, y), original.at(x, y)), qPrintable(QStringLiteral("%1,%2").arg(x).arg(y)));
            }
        }
        QVERIFY(ColorAdjust{}.isIdentity());
    }

    void adjustmentsGoTheRightWay()
    {
        const auto eval = [](ColorAdjust a, std::array<double, 3> c) { return ColorLut::evaluate(a, c); };
        const std::array<double, 3> grey{0.5, 0.5, 0.5};
        const std::array<double, 3> orange{0.9, 0.5, 0.1};
        ColorAdjust a;
        a.exposure = 1;
        QVERIFY(eval(a, grey)[0] > 0.6);
        a = {};
        a.saturation = -1;
        const auto mono = eval(a, orange);
        QVERIFY(std::abs(mono[0] - mono[1]) < 1e-9 && std::abs(mono[1] - mono[2]) < 1e-9);
        a = {};
        a.contrast = 1;
        QVERIFY(eval(a, {0.3, 0.3, 0.3})[0] < 0.3 && eval(a, {0.7, 0.7, 0.7})[0] > 0.7);
        a = {};
        a.temperature = 1;
        const auto warm = eval(a, grey);
        QVERIFY(warm[0] > warm[2]);
        a = {};
        a.brightness = 1;
        QVERIFY(eval(a, grey)[0] > 0.5);
        QCOMPARE(eval(a, {0, 0, 0})[0], 0.0); // black stays black
        a = {};
        a.fade = 1;
        QVERIFY(eval(a, {0, 0, 0})[0] > 0.1);
        a = {};
        a.curve = {{0.25, 0.15}, {0.75, 0.85}};
        QVERIFY(eval(a, {0.25, 0.25, 0.25})[0] < 0.2);
    }

    void lutIntensityMixesWithTheOriginal()
    {
        ColorAdjust mono;
        mono.saturation = -1;
        const ColorLut lut(mono);
        Buffer full(1, 1, {200, 50, 50, 255});
        Buffer none(1, 1, {200, 50, 50, 255});
        Buffer half(1, 1, {200, 50, 50, 255});
        lut.apply(full.view(), 1.0);
        lut.apply(none.view(), 0.0);
        lut.apply(half.view(), 0.5);
        QVERIFY(std::abs(full.at(0, 0)[0] - full.at(0, 0)[1]) <= 1);
        QCOMPARE(none.at(0, 0), (std::array<int, 4>{200, 50, 50, 255}));
        QVERIFY(half.at(0, 0)[0] < 200 && half.at(0, 0)[0] > full.at(0, 0)[0]);
    }

    void spatialFilters()
    {
        // Vignette: corners darker, centre untouched.
        Buffer image(21, 21, {200, 200, 200, 255});
        vignette(image.view(), -1.0, 0.5, 0, 21);
        QVERIFY(image.at(0, 0)[0] < 100);
        QCOMPARE(image.at(10, 10)[0], 200);
        // Grain: deterministic per seed, different between seeds, mean roughly kept.
        Buffer g1(32, 32, {128, 128, 128, 255});
        Buffer g2 = g1;
        Buffer g3 = g1;
        grain(g1.view(), 0.5, 7, 0, 32);
        grain(g2.view(), 0.5, 7, 0, 32);
        grain(g3.view(), 0.5, 8, 0, 32);
        QCOMPARE(g1.pixels, g2.pixels);
        QVERIFY(g1.pixels != g3.pixels);
        long sum = 0;
        for (int i = 0; i < 32 * 32; ++i) {
            sum += g1.pixels[static_cast<size_t>(i * 4)];
        }
        QVERIFY(std::abs(sum / (32 * 32) - 128) < 6);
        // Sharpen: a flat image stays flat, an edge gets overshoot.
        Buffer flat(8, 8, {90, 90, 90, 255});
        Buffer sharpened(8, 8);
        sharpen(sharpened.view(), flat.constView(), 1.0, 0, 8);
        QCOMPARE(sharpened.at(4, 4), (std::array<int, 4>{90, 90, 90, 255}));
        Buffer edge(8, 1, {50, 50, 50, 255});
        for (int x = 4; x < 8; ++x) {
            edge.set(x, 0, {200, 200, 200, 255});
        }
        Buffer edgeOut(8, 1);
        sharpen(edgeOut.view(), edge.constView(), 1.0, 0, 1);
        QVERIFY(edgeOut.at(4, 0)[0] > 200 && edgeOut.at(3, 0)[0] < 50);
        // Box blur: flat stays flat, a dot spreads and keeps its energy approximately.
        Buffer dot(21, 21, {0, 0, 0, 255});
        dot.set(10, 10, {255, 255, 255, 255});
        boxBlur(dot.view(), 2);
        QVERIFY(dot.at(10, 10)[0] < 255 && dot.at(12, 10)[0] > 0);
        Buffer flatBlur(9, 9, {77, 88, 99, 255});
        boxBlur(flatBlur.view(), 3);
        QCOMPARE(flatBlur.at(0, 0), (std::array<int, 4>{77, 88, 99, 255}));
    }

    // ---- transitions --------------------------------------------------------------------------------------------
    void autoEnhance()
    {
        // A grey ramp over the whole range: already right, nothing to correct.
        const auto ramp = [](double scale, std::array<double, 3> tint) {
            Buffer b(256, 16);
            for (int y = 0; y < 16; ++y) {
                for (int x = 0; x < 256; ++x) {
                    b.set(x, y, {std::uint8_t(std::lround(x * scale * tint[0])), std::uint8_t(std::lround(x * scale * tint[1])),
                                 std::uint8_t(std::lround(x * scale * tint[2])), 255});
                }
            }
            return b;
        };
        const auto meanOf = [](const Buffer &b) {
            std::array<double, 3> sum{};
            for (int i = 0; i < b.width * b.height; ++i) {
                for (int c = 0; c < 3; ++c) {
                    sum[static_cast<size_t>(c)] += b.pixels[static_cast<size_t>(i * 4 + c)] / 255.0;
                }
            }
            for (double &v : sum) {
                v /= b.width * b.height;
            }
            return sum;
        };
        const Buffer good = ramp(1.0, {1, 1, 1});
        const std::array<ConstImageView, 1> goodFrames{good.constView()};
        QVERIFY(vedit::fx::autoEnhance(goodFrames).isIdentity());

        // Dark and bluish: brighter and warmer, and the result is closer to neutral grey at a middle level.
        Buffer dark = ramp(0.4, {0.75, 0.9, 1.0});
        const std::array<ConstImageView, 1> darkFrames{dark.constView()};
        const ColorAdjust fix = vedit::fx::autoEnhance(darkFrames);
        QVERIFY(fix.exposure > 0.3);
        QVERIFY(fix.temperature > 0.1);
        QVERIFY(fix.contrast > 0.0);
        const std::array<double, 3> before = meanOf(dark);
        ColorLut(fix).apply(dark.view());
        const std::array<double, 3> after = meanOf(dark);
        const auto luma = [](const std::array<double, 3> &c) { return 0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2]; };
        QVERIFY(std::abs(luma(after) - 0.45) < std::abs(luma(before) - 0.45));
        QVERIFY(std::abs(after[2] / after[0] - 1.0) < 0.5 * std::abs(before[2] / before[0] - 1.0));

        // Nothing to measure: no change.
        const Buffer empty(8, 8);
        const std::array<ConstImageView, 1> emptyFrames{empty.constView()};
        QVERIFY(vedit::fx::autoEnhance(emptyFrames).isIdentity());

        QCOMPARE(vedit::fx::autoGainDb(1.0), -1.0);
        QVERIFY(std::abs(vedit::fx::autoGainDb(0.5) - 5.0206) < 1e-3);
        QCOMPARE(vedit::fx::autoGainDb(0.01), 12.0);
        QCOMPARE(vedit::fx::autoGainDb(0.0), 0.0);
    }

    void everyTransitionStartsOnAAndEndsOnB()
    {
        const Buffer a = pattern(24, 16);
        Buffer b(24, 16);
        for (int y = 0; y < 16; ++y) {
            for (int x = 0; x < 24; ++x) {
                b.set(x, y, {std::uint8_t(255 - x * 7), std::uint8_t(x * 3 + y), 90, 255});
            }
        }
        for (int k = int(TransitionKind::Dissolve); k <= int(TransitionKind::ZoomIn); ++k) {
            const auto kind = static_cast<TransitionKind>(k);
            const QByteArray name(transitionKindName(kind).data(), qsizetype(transitionKindName(kind).size()));
            QCOMPARE(transitionKindFromName(transitionKindName(kind)), kind);
            Buffer out(24, 16);
            renderTransition(kind, out.view(), a.constView(), b.constView(), 0.0, {}, 0, 16);
            for (int y = 0; y < 16; ++y) {
                for (int x = 0; x < 24; ++x) {
                    QVERIFY2(near(out.at(x, y), a.at(x, y)), name.constData());
                }
            }
            renderTransition(kind, out.view(), a.constView(), b.constView(), 1.0, {}, 0, 16);
            for (int y = 0; y < 16; ++y) {
                for (int x = 0; x < 24; ++x) {
                    QVERIFY2(near(out.at(x, y), b.at(x, y)), name.constData());
                }
            }
        }
    }

    void transitionsHalfway()
    {
        Buffer a(20, 10, {0, 0, 0, 255});
        Buffer b(20, 10, {200, 100, 50, 255});
        Buffer out(20, 10);
        renderTransition(TransitionKind::Dissolve, out.view(), a.constView(), b.constView(), 0.5, {}, 0, 10);
        QVERIFY(near(out.at(5, 5), {100, 50, 25, 255}));
        renderTransition(TransitionKind::DipToWhite, out.view(), a.constView(), b.constView(), 0.5, {}, 0, 10);
        QVERIFY(near(out.at(5, 5), {255, 255, 255, 255}));
        // Wipe left at half: right half is B, left half A (edge in the middle).
        renderTransition(TransitionKind::WipeLeft, out.view(), a.constView(), b.constView(), 0.5, {0.0}, 0, 10);
        QVERIFY(near(out.at(15, 5), {200, 100, 50, 255}));
        QVERIFY(near(out.at(4, 5), {0, 0, 0, 255}));
        // Slide left at half: B covers the right half.
        renderTransition(TransitionKind::SlideLeft, out.view(), a.constView(), b.constView(), 0.5, {}, 0, 10);
        QVERIFY(near(out.at(15, 5), {200, 100, 50, 255}));
        QVERIFY(near(out.at(4, 5), {0, 0, 0, 255}));
        // Iris: the centre opens first.
        renderTransition(TransitionKind::Iris, out.view(), a.constView(), b.constView(), 0.3, {0.0}, 0, 10);
        QVERIFY(near(out.at(10, 5), {200, 100, 50, 255}));
        QVERIFY(near(out.at(0, 0), {0, 0, 0, 255}));
        QCOMPARE(ease(Easing::EaseInOut, 0.5), 0.5);
        QVERIFY(ease(Easing::EaseIn, 0.3) < 0.3 && ease(Easing::EaseOut, 0.3) > 0.3);
    }

    // ---- library ------------------------------------------------------------------------------------------------
    void coreLibraryLoads()
    {
        const Library &library = Library::core();
        QVERIFY2(library.errors().isEmpty(), qPrintable(library.errors().join(u'\n')));
        QCOMPARE(library.packId(), QStringLiteral("vedit.core"));
        QVERIFY(library.filters().size() >= 30);
        QVERIFY(library.transitions().size() >= 18);
        QVERIFY(library.textStyles().size() >= 24);
        // Every item has a category that exists, a name in both languages, a unique id.
        QSet<QString> ids;
        const auto check = [&](const auto &items, const std::vector<Category> &categories) {
            for (const auto &item : items) {
                QVERIFY2(!ids.contains(item.id), qPrintable(item.id));
                ids.insert(item.id);
                QVERIFY2(!item.name.en.isEmpty() && !item.name.it.isEmpty(), qPrintable(item.id));
                QVERIFY2(std::any_of(categories.begin(), categories.end(), [&](const Category &c) { return c.id == item.category; }),
                         qPrintable(item.id));
            }
        };
        check(library.filters(), library.filterCategories());
        check(library.transitions(), library.transitionCategories());
        check(library.textStyles(), library.textStyleCategories());
        // Every kernel is used by a transition; every filter changes something.
        for (int k = int(TransitionKind::Dissolve); k <= int(TransitionKind::ZoomIn); ++k) {
            QVERIFY(std::any_of(library.transitions().begin(), library.transitions().end(),
                                [k](const TransitionPreset &t) { return int(t.kernel) == k; }));
        }
        for (const FilterPreset &filter : library.filters()) {
            QVERIFY2(!filter.look.isIdentity() || filter.vignette != 0 || filter.grain != 0, qPrintable(filter.id));
        }
        QVERIFY(library.filter(QStringLiteral("filters/bw")));
        QCOMPARE(library.filter(QStringLiteral("filters/bw"))->look.saturation, -1.0);
        QVERIFY(library.effect(QStringLiteral("vedit.adjust.basic")));
        QVERIFY(library.effect(QStringLiteral("vedit.adjust.basic"))->params.size() >= 12);
    }

    // ---- audio --------------------------------------------------------------------------------------------------
    void gainRampAndPan()
    {
        std::vector<float> samples(8, 1.0f); // 4 stereo frames of full scale
        const float peak = applyGain(samples.data(), 2, 4, 0.0f, 1.0f, 0.0f);
        QCOMPARE(samples[0], 0.0f);
        QCOMPARE(samples[7], 1.0f);
        QCOMPARE(peak, 1.0f);
        std::vector<float> panned(4, 1.0f);
        applyGain(panned.data(), 2, 2, 1.0f, 1.0f, -1.0f);
        QCOMPARE(panned[0], 1.0f);
        QVERIFY(std::abs(panned[1]) < 1e-6f); // hard left: right channel silent
        QCOMPARE(dbToGain(0), 1.0f);
        QCOMPARE(dbToGain(-80), 0.0f);
        QVERIFY(std::abs(dbToGain(-6) - 0.501f) < 0.01f);
    }
};

QTEST_GUILESS_MAIN(TestKernels)
#include "tst_kernels.moc"
