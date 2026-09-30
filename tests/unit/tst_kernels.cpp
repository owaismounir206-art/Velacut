// SPDX-License-Identifier: GPL-3.0-or-later
// CPU reference kernels of Phase 2 (SPEC 1bis rule 1): transform, colour, spatial filters, transitions, audio gain.
#include "fx/Audio.h"
#include "fx/ChromaKey.h"
#include "fx/Color.h"
#include "fx/Composite.h"
#include "fx/Enhance.h"
#include "fx/Grade.h"
#include "fx/Library.h"
#include "core/project/SpeedCurve.h"
#include "fx/Mask.h"
#include "fx/MotionBlur.h"
#include "fx/Transform.h"
#include "fx/Transition.h"
#include "fx/VideoEffect.h"
#include "fx/Graphic.h"

#include <QSet>
#include <QTest>

#include <array>
#include <cmath>
#include <vector>

using vedit::Rational;
using vedit::RationalTime;
using vedit::SpeedCurve;
using vedit::SpeedCurveUtil;
using namespace vedit::fx;
using namespace Qt::StringLiterals;

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

    void gradeHslCurvesWheels()
    {
        const auto near3 = [](std::array<double, 3> a, std::array<double, 3> b, double tolerance) {
            return std::abs(a[0] - b[0]) <= tolerance && std::abs(a[1] - b[1]) <= tolerance && std::abs(a[2] - b[2]) <= tolerance;
        };
        // Neutral: nothing changes.
        const Grade neutral;
        QVERIFY(neutral.isIdentity());
        for (const std::array<double, 3> c : {std::array<double, 3>{0.1, 0.5, 0.9}, {1, 0, 0}, {0.3, 0.3, 0.3}}) {
            QVERIFY(near3(applyGrade(neutral, c), c, 1e-9));
        }
        // HSL: the reds lose their colour, the blues stay; greys are never touched.
        Grade hsl;
        hsl.hsl[0].saturation = -1;
        QVERIFY(near3(applyGrade(hsl, {0.8, 0.1, 0.1}), {0.45, 0.45, 0.45}, 0.01));
        QVERIFY(near3(applyGrade(hsl, {0.1, 0.1, 0.8}), {0.1, 0.1, 0.8}, 1e-6));
        QVERIFY(near3(applyGrade(hsl, {0.5, 0.5, 0.5}), {0.5, 0.5, 0.5}, 1e-9));
        // Hue: greens turn towards aqua (more blue).
        Grade hue;
        hue.hsl[3].hue = 1;
        const std::array<double, 3> turned = applyGrade(hue, {0.1, 0.8, 0.1});
        QVERIFY(turned[2] > 0.3 && turned[1] > 0.6);
        // Wheels: lifted shadows, lower highlights, warmer midtones.
        Grade wheels;
        wheels.shadows.level = 0.4;
        wheels.highlights.level = -0.4;
        wheels.midtones.colour = {0.3, 0, -0.3};
        const std::array<double, 3> black = applyGrade(wheels, {0, 0, 0});
        const std::array<double, 3> white = applyGrade(wheels, {1, 1, 1});
        const std::array<double, 3> grey = applyGrade(wheels, {0.5, 0.5, 0.5});
        QVERIFY(black[1] > 0.05);
        QVERIFY(white[1] < 0.9);
        QVERIFY(grey[0] > grey[1] && grey[1] > grey[2]);
        // Balance: a blue cast removed.
        Grade balance;
        balance.balance = {1.2, 1.0, 0.8};
        QVERIFY(near3(applyGrade(balance, {0.5, 0.5, 0.625}), {0.6, 0.5, 0.5}, 1e-9));
        // Curves go through their points, smoothly and without overshoot.
        const Grade::Curve curve{{0.25, 0.1}, {0.5, 0.6}, {0.75, 0.9}};
        QCOMPARE(evaluateCurve(curve, 0.5), 0.6);
        QCOMPARE(evaluateCurve(curve, 0.0), 0.0);
        QCOMPARE(evaluateCurve(curve, 1.0), 1.0);
        double previous = 0;
        for (int i = 0; i <= 100; ++i) {
            const double y = evaluateCurve(curve, i / 100.0);
            QVERIFY(y >= previous - 1e-12 && y <= 1.0);
            previous = y;
        }
        // JSON round trip (the params of "vedit.grade").
        Grade full = wheels;
        full.hsl[5] = {0.2, -0.3, 0.1};
        full.master = curve;
        full.channel[2] = {{0.5, 0.4}};
        full.balance = {1.1, 1.0, 0.9};
        QCOMPARE(Grade::fromJson(full.toJson()), full);
        QVERIFY(Grade::fromJson({}).isIdentity());
    }

    void cubeLuts()
    {
        // Identity 2×2×2 (red fastest), with a title and comments.
        const QByteArray identity = "# made by hand\nTITLE \"Identity\"\nLUT_3D_SIZE 2\n"
                                    "0 0 0\n1 0 0\n0 1 0\n1 1 0\n0 0 1\n1 0 1\n0 1 1\n1 1 1\n";
        QString error;
        const std::optional<CubeLut> lut = CubeLut::parse(identity, &error);
        QVERIFY2(lut, qPrintable(error));
        QCOMPARE(lut->title(), u"Identity"_s);
        QCOMPARE(lut->size(), 2);
        const std::array<double, 3> c{0.2, 0.6, 0.9};
        const std::array<double, 3> out = lut->sample(c);
        QVERIFY(std::abs(out[0] - 0.2) < 1e-6 && std::abs(out[1] - 0.6) < 1e-6 && std::abs(out[2] - 0.9) < 1e-6);
        // A 17³ look (a smooth S curve and a warm tint) composed into the 33³ LUT: under 1/255 from the cube itself.
        QByteArray look = "LUT_3D_SIZE 17\n";
        const auto shape = [](double v) { return v * v * (3 - 2 * v); };
        for (int b = 0; b < 17; ++b) {
            for (int g = 0; g < 17; ++g) {
                for (int r = 0; r < 17; ++r) {
                    look += QByteArray::number(std::min(1.0, shape(r / 16.0) * 1.05)) + ' ' + QByteArray::number(shape(g / 16.0)) + ' ' +
                            QByteArray::number(shape(b / 16.0) * 0.95) + '\n';
                }
            }
        }
        const std::optional<CubeLut> cube = CubeLut::parse(look, &error);
        QVERIFY2(cube, qPrintable(error));
        const ColorLut composed([&cube](const std::array<double, 3> &rgb) { return cube->sample(rgb); });
        Buffer image = pattern(64, 64);
        Buffer expected = image;
        composed.apply(image.view());
        int worst = 0;
        for (int y = 0; y < 64; ++y) {
            for (int x = 0; x < 64; ++x) {
                const std::array<int, 4> px = expected.at(x, y);
                const std::array<double, 3> want = cube->sample({px[0] / 255.0, px[1] / 255.0, px[2] / 255.0});
                const std::array<int, 4> got = image.at(x, y);
                for (int i = 0; i < 3; ++i) {
                    worst = std::max(worst, std::abs(got[static_cast<size_t>(i)] - static_cast<int>(std::lround(want[static_cast<size_t>(i)] * 255))));
                }
            }
        }
        qInfo("composed .cube: worst %d/255", worst);
        QVERIFY2(worst <= 2, qPrintable(QString::number(worst)));
        // 1D LUTs and domains.
        const std::optional<CubeLut> oneD = CubeLut::parse("LUT_1D_SIZE 2\nDOMAIN_MIN 0 0 0\nDOMAIN_MAX 1 1 1\n1 1 1\n0 0 0\n");
        QVERIFY(oneD && !oneD->is3d());
        QVERIFY(std::abs(oneD->sample({0.25, 0.5, 1})[0] - 0.75) < 1e-6);
        // Errors: no size, too few values, garbage.
        QVERIFY(!CubeLut::parse("0 0 0\n", &error));
        QVERIFY(!CubeLut::parse("LUT_3D_SIZE 2\n0 0 0\n", &error));
        QVERIFY(error.contains(u"instead of"_s));
        QVERIFY(!CubeLut::parse("LUT_3D_SIZE 2\nhello world !\n", &error));
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
        for (int k = int(TransitionKind::Dissolve); k <= int(TransitionKind::FoldOver); ++k) {
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
    // Every video effect kernel: a name that maps back to it, a deterministic picture (the same time gives the same
    // frame), the alpha left as it is, and a visible change.
    void everyVideoEffect()
    {
        Buffer photo(64, 36);
        for (int y = 0; y < 36; ++y) {
            for (int x = 0; x < 64; ++x) {
                const bool disc = (x - 40) * (x - 40) + (y - 15) * (y - 15) < 64;
                photo.set(x, y, {std::uint8_t(disc ? 250 : 40 + x * 2), std::uint8_t(disc ? 240 : 60 + y * 3),
                                 std::uint8_t(disc ? 200 : 150 - x), 255});
            }
        }
        for (int k = int(EffectKernel::Blur); k <= int(EffectKernel::ColorShift); ++k) {
            const auto kernel = static_cast<EffectKernel>(k);
            const QString name = effectKernelName(kernel);
            QVERIFY(!name.isEmpty());
            QCOMPARE(effectKernel(name), kernel);
            bool changed = false;
            for (const double time : {0.05, 0.37, 1.1}) {
                VideoEffectParams params;
                params.amount = 0.7;
                params.time = time;
                Buffer first = photo;
                renderVideoEffect(kernel, first.view(), params);
                Buffer second = photo;
                renderVideoEffect(kernel, second.view(), params);
                QVERIFY2(first.pixels == second.pixels, qPrintable(name));
                for (int i = 3; i < int(first.pixels.size()); i += 4) {
                    QCOMPARE(first.pixels[size_t(i)], std::uint8_t(255));
                }
                changed = changed || first.pixels != photo.pixels;
            }
            QVERIFY2(changed, qPrintable(name + u" changes nothing"_s));
        }
        QVERIFY(!effectKernel(u"noSuchEffect"_s));
    }

    // Graphic elements: numbers that count to their target and hold it, clocks, a bar filling with the clip, marks that
    // are drawn over time (nothing at the start, the whole mark once drawn).
    void graphicElements()
    {
        GraphicParams counter;
        counter.from = 0;
        counter.to = 100;
        QCOMPARE(graphicText(counter, 0.0, 3.0), u"0"_s);
        QCOMPARE(graphicText(counter, 2.4, 3.0), u"100"_s);
        QCOMPARE(graphicText(counter, 3.0, 3.0), u"100"_s);
        const int middle = graphicText(counter, 1.2, 3.0).toInt();
        QVERIFY2(middle > 50 && middle < 100, qPrintable(QString::number(middle))); // eases out: past half at half time
        counter.decimals = 1;
        counter.to = 9.9;
        QCOMPARE(graphicText(counter, 3.0, 3.0), u"9.9"_s);
        GraphicParams clock;
        clock.type = GraphicType::Countdown;
        QCOMPARE(graphicText(clock, 0.0, 65.0), u"1:05"_s);
        QCOMPARE(graphicText(clock, 64.5, 65.0), u"0:01"_s);
        clock.type = GraphicType::Timer;
        QCOMPARE(graphicText(clock, 61.9, 65.0), u"1:01"_s);

        const QSize canvas(160, 90);
        const auto painted = [](const QImage &image) {
            int count = 0;
            for (int y = 0; y < image.height(); ++y) {
                for (int x = 0; x < image.width(); ++x) {
                    count += qAlpha(image.pixel(x, y)) > 0 ? 1 : 0;
                }
            }
            return count;
        };
        GraphicParams bar;
        bar.type = GraphicType::ProgressBar;
        bar.color = Qt::red;
        bar.color2 = QColor(255, 255, 255, 80);
        const auto redPixels = [](const QImage &image) {
            int count = 0;
            for (int y = 0; y < image.height(); ++y) {
                for (int x = 0; x < image.width(); ++x) {
                    const QRgb p = image.pixel(x, y);
                    count += qRed(p) > 200 && qGreen(p) < 80 && qAlpha(p) > 200 ? 1 : 0;
                }
            }
            return count;
        };
        const int quarter = redPixels(renderGraphic(bar, 1.0, 4.0, canvas, {}));
        const int full = redPixels(renderGraphic(bar, 4.0, 4.0, canvas, {}));
        QVERIFY2(full > quarter * 3 && full < quarter * 5, qPrintable(u"%1 %2"_s.arg(quarter).arg(full)));
        for (const GraphicType type : {GraphicType::Arrow, GraphicType::Circle, GraphicType::Underline, GraphicType::Highlighter,
                                       GraphicType::Check, GraphicType::Cross}) {
            GraphicParams mark;
            mark.type = type;
            QCOMPARE(painted(renderGraphic(mark, 0.0, 3.0, canvas, {})), 0);
            const int half = painted(renderGraphic(mark, mark.drawSeconds / 2, 3.0, canvas, {}));
            const int done = painted(renderGraphic(mark, 2.0, 3.0, canvas, {}));
            QVERIFY2(half > 0 && done > half, qPrintable(u"%1: %2 %3"_s.arg(int(type)).arg(half).arg(done)));
        }
    }

    void coreLibraryLoads()
    {
        const Library &library = Library::core();
        QVERIFY2(library.errors().isEmpty(), qPrintable(library.errors().join(u'\n')));
        QCOMPARE(library.packId(), QStringLiteral("vedit.core"));
        QVERIFY(library.filters().size() >= 30);
        QVERIFY(library.transitions().size() >= 100);
        QVERIFY(library.textStyles().size() >= 40); // SPEC §5.7: at least 40 styles
        // … and at least 50 animated text templates, with their text in both languages.
        const auto templates = std::count_if(library.textStyles().begin(), library.textStyles().end(),
                                             [](const TextStylePreset &t) { return !t.animation.isEmpty(); });
        QVERIFY2(templates >= 50, qPrintable(QString::number(templates)));
        for (const TextStylePreset &t : library.textStyles()) {
            QVERIFY2(t.animation.isEmpty() || (!t.sampleText.en.isEmpty() && !t.sampleText.it.isEmpty()), qPrintable(t.id));
        }
        // Preset animations: at least 30 entry, 30 exit and 30 loop animations (SPEC §5.6).
        for (const char *category : {"in", "out", "loop"}) {
            const auto count = std::count_if(library.animations().begin(), library.animations().end(),
                                             [category](const AnimationPreset &a) { return a.category == QLatin1StringView(category); });
            QVERIFY2(count >= 30, category);
        }
        QVERIFY(library.animation(QStringLiteral("animations/in/fade")));
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
        // Video effects (SPEC §5.11: at least 80): each a kernel that exists, or an effect on the beat; every kernel used.
        QVERIFY2(library.videoEffects().size() >= 80, qPrintable(QString::number(library.videoEffects().size())));
        check(library.videoEffects(), library.videoEffectCategories());
        QSet<QString> usedKernels;
        for (const VideoEffectPreset &effect : library.videoEffects()) {
            if (effect.type == u"vedit.effect"_s) {
                QVERIFY2(effectKernel(effect.kernel), qPrintable(effect.id));
                usedKernels.insert(effect.kernel);
            } else {
                QVERIFY2(effect.type.startsWith(u"vedit.beat."_s), qPrintable(effect.id));
            }
        }
        for (int k = int(EffectKernel::Blur); k <= int(EffectKernel::ColorShift); ++k) {
            QVERIFY2(usedKernels.contains(effectKernelName(EffectKernel(k))), qPrintable(effectKernelName(EffectKernel(k))));
        }
        // Every kernel is used by a transition; every filter changes something.
        for (int k = int(TransitionKind::Dissolve); k <= int(TransitionKind::FoldOver); ++k) {
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
        QVERIFY(library.effect(QStringLiteral("vedit.chroma_key")));
        QVERIFY(library.effect(QStringLiteral("vedit.chroma_key"))->params.size() >= 3);
    }

    // ---- blend modes --------------------------------------------------------------------------------------------
    void blendModes()
    {
        // Multiply: 128 * 128 / 255 = 64
        Buffer dstM(2, 2, {128, 128, 128, 255});
        Buffer srcM(2, 2, {128, 128, 128, 255});
        compositeBlend(dstM.view(), srcM.constView(), BlendMode::Multiply, 255);
        QCOMPARE(dstM.at(0, 0)[0], 64);
        QCOMPARE(dstM.at(0, 0)[3], 255);

        // Screen: 128 + 128 - 64 = 192
        Buffer dstS(2, 2, {128, 128, 128, 255});
        Buffer srcS(2, 2, {128, 128, 128, 255});
        compositeBlend(dstS.view(), srcS.constView(), BlendMode::Screen, 255);
        QCOMPARE(dstS.at(0, 0)[0], 192);

        // Add: 100 + 100 = 200
        Buffer dstA(2, 2, {100, 100, 100, 255});
        Buffer srcA(2, 2, {100, 100, 100, 255});
        compositeBlend(dstA.view(), srcA.constView(), BlendMode::Add, 255);
        QCOMPARE(dstA.at(0, 0)[0], 200);

        // Difference: |200 - 50| = 150
        Buffer dstD(2, 2, {200, 200, 200, 255});
        Buffer srcD(2, 2, {50, 50, 50, 255});
        compositeBlend(dstD.view(), srcD.constView(), BlendMode::Difference, 255);
        QCOMPARE(dstD.at(0, 0)[0], 150);

        // All 16 blend modes execute deterministically on a test pattern without crash or bounds violations
        const BlendMode modes[] = {
            BlendMode::Normal, BlendMode::Lighten, BlendMode::Screen, BlendMode::Multiply,
            BlendMode::Overlay, BlendMode::SoftLight, BlendMode::HardLight, BlendMode::Difference,
            BlendMode::Darken, BlendMode::Color, BlendMode::Luminosity, BlendMode::Add,
            BlendMode::ColorDodge, BlendMode::ColorBurn, BlendMode::Exclusion, BlendMode::Hue,
            BlendMode::Saturation
        };
        for (BlendMode mode : modes) {
            Buffer p1 = pattern(16, 16);
            Buffer p2 = pattern(16, 16);
            compositeBlend(p1.view(), p2.constView(), mode, 200);
            QCOMPARE(p1.width, 16);
        }
    }

    // ---- chroma key ---------------------------------------------------------------------------------------------
    void chromaKey()
    {
        // 4x4 image: top half green screen (0, 255, 0), bottom half subject (200, 100, 80)
        Buffer img(4, 4);
        for (int y = 0; y < 2; ++y) {
            for (int x = 0; x < 4; ++x) {
                img.set(x, y, {0, 255, 0, 255});
            }
        }
        for (int y = 2; y < 4; ++y) {
            for (int x = 0; x < 4; ++x) {
                img.set(x, y, {200, 100, 80, 255});
            }
        }

        ChromaKeySettings settings;
        settings.keyR = 0;
        settings.keyG = 255;
        settings.keyB = 0;
        settings.similarity = 0.35;
        settings.smoothness = 0.05;
        settings.spill = 0.5;

        applyChromaKey(img.view(), settings);

        // Green screen pixels keyed out (alpha = 0)
        QCOMPARE(img.at(0, 0)[3], 0);
        QCOMPARE(img.at(3, 1)[3], 0);

        // Non-green subject pixels preserved (alpha = 255)
        QCOMPARE(img.at(0, 2)[3], 255);
        QCOMPARE(img.at(0, 2)[0], 200);
    }

    // ---- masks --------------------------------------------------------------------------------------------------
    void maskRectangleAndCircle()
    {
        Buffer img(100, 100, {255, 255, 255, 255});
        MaskParams rect;
        rect.shape = MaskShape::Rectangle;
        rect.centerX = 0.0;
        rect.centerY = 0.0;
        rect.sizeX = 0.5; // 50 pixels wide: x from 25 to 75
        rect.sizeY = 0.5; // 50 pixels high: y from 25 to 75
        rect.feather = 0.0;

        applyMasks(img.view(), {rect});
        // Inside
        QCOMPARE(img.at(50, 50)[3], 255);
        QCOMPARE(img.at(30, 30)[3], 255);
        // Outside
        QCOMPARE(img.at(5, 5)[3], 0);
        QCOMPARE(img.at(90, 90)[3], 0);

        // Test invert
        Buffer inv(100, 100, {255, 255, 255, 255});
        rect.invert = true;
        applyMasks(inv.view(), {rect});
        QCOMPARE(inv.at(50, 50)[3], 0);
        QCOMPARE(inv.at(5, 5)[3], 255);

        // Test Circle
        Buffer circle(100, 100, {255, 255, 255, 255});
        MaskParams circ;
        circ.shape = MaskShape::Circle;
        circ.centerX = 0.0;
        circ.centerY = 0.0;
        circ.sizeX = 0.5; // radius 25
        circ.sizeY = 0.5;
        circ.feather = 0.0;
        applyMasks(circle.view(), {circ});
        QCOMPARE(circle.at(50, 50)[3], 255);
        QCOMPARE(circle.at(70, 50)[3], 255); // distance 20 < 25
        QCOMPARE(circle.at(80, 50)[3], 0);   // distance 30 > 25
        QCOMPARE(circle.at(5, 5)[3], 0);
    }

    void maskFeatherLinearAndMirror()
    {
        // Linear mask: top half inside, bottom half outside
        Buffer img(100, 100, {255, 255, 255, 255});
        MaskParams linear;
        linear.shape = MaskShape::Linear;
        linear.feather = 0.1; // 10% feather
        applyMasks(img.view(), {linear});
        QCOMPARE(img.at(50, 20)[3], 255);
        QCOMPARE(img.at(50, 80)[3], 0);
        // Middle has feather gradient
        const uint8_t midAlpha = img.at(50, 50)[3];
        QVERIFY(midAlpha > 100 && midAlpha < 155);

        // Mirror mask: center horizontal band inside
        Buffer mirrorImg(100, 100, {255, 255, 255, 255});
        MaskParams mirror;
        mirror.shape = MaskShape::Mirror;
        mirror.sizeY = 0.3; // 30 pixels tall centered at y=50 (from y=35 to y=65)
        mirror.feather = 0.0;
        applyMasks(mirrorImg.view(), {mirror});
        QCOMPARE(mirrorImg.at(50, 50)[3], 255);
        QCOMPARE(mirrorImg.at(50, 10)[3], 0);
        QCOMPARE(mirrorImg.at(50, 90)[3], 0);
    }

    void maskHeartStarAndPath()
    {
        Buffer img(100, 100, {255, 255, 255, 255});
        MaskParams heart;
        heart.shape = MaskShape::Heart;
        heart.sizeX = 0.6;
        heart.sizeY = 0.6;
        applyMasks(img.view(), {heart});
        QCOMPARE(img.at(50, 50)[3], 255); // center inside
        QCOMPARE(img.at(5, 95)[3], 0);    // bottom corners outside

        Buffer starImg(100, 100, {255, 255, 255, 255});
        MaskParams star;
        star.shape = MaskShape::Star;
        star.sizeX = 0.7;
        star.sizeY = 0.7;
        applyMasks(starImg.view(), {star});
        QCOMPARE(starImg.at(50, 50)[3], 255);
        QCOMPARE(starImg.at(5, 5)[3], 0);

        // Path mask: triangle with points at (-0.2, -0.2), (0.2, -0.2), (0.0, 0.2)
        Buffer pathImg(100, 100, {255, 255, 255, 255});
        MaskParams path;
        path.shape = MaskShape::Path;
        path.points = {{-0.2, -0.2, 0, 0, 0, 0}, {0.2, -0.2, 0, 0, 0, 0}, {0.0, 0.2, 0, 0, 0, 0}};
        applyMasks(pathImg.view(), {path});
        QCOMPARE(pathImg.at(50, 40)[3], 255); // inside triangle
        QCOMPARE(pathImg.at(10, 10)[3], 0);   // outside triangle

        // Union of two masks: left rectangle + right rectangle
        Buffer unionImg(100, 100, {255, 255, 255, 255});
        MaskParams left;
        left.centerX = -0.3;
        left.sizeX = 0.3;
        left.sizeY = 0.3;
        MaskParams right;
        right.centerX = 0.3;
        right.sizeX = 0.3;
        right.sizeY = 0.3;
        applyMasks(unionImg.view(), {left, right});
        QCOMPARE(unionImg.at(20, 50)[3], 255); // left mask
        QCOMPARE(unionImg.at(80, 50)[3], 255); // right mask
        QCOMPARE(unionImg.at(50, 50)[3], 0);   // gap in the middle
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

    // ---- speed curves (SPEC §5.5) -------------------------------------------------------------------------------
    void speedCurves()
    {
        const QStringList presetIds = SpeedCurveUtil::presetIds();
        QCOMPARE(presetIds.size(), 7);
        QVERIFY(presetIds.contains(QStringLiteral("montage")));
        QVERIFY(presetIds.contains(QStringLiteral("hero")));
        QVERIFY(presetIds.contains(QStringLiteral("bullet")));
        QVERIFY(presetIds.contains(QStringLiteral("jump")));
        QVERIFY(presetIds.contains(QStringLiteral("flash_in")));
        QVERIFY(presetIds.contains(QStringLiteral("flash_out")));
        QVERIFY(presetIds.contains(QStringLiteral("custom")));

        // Preset titles in both Italian and English
        for (const QString &id : presetIds) {
            const QString itTitle = SpeedCurveUtil::presetTitle(id, QStringLiteral("it"));
            const QString enTitle = SpeedCurveUtil::presetTitle(id, QStringLiteral("en"));
            QVERIFY(!itTitle.isEmpty());
            QVERIFY(!enTitle.isEmpty());
        }

        // Test each preset mathematical properties
        for (const QString &id : presetIds) {
            const SpeedCurve curve = SpeedCurveUtil::preset(id);
            const auto norm = SpeedCurveUtil::normalizedPoints(curve);
            QVERIFY(norm.size() >= 2);
            QCOMPARE(norm.front().first, 0.0);
            QCOMPARE(norm.back().first, 1.0);

            // Integrated time must be monotonic and non-negative
            double prevIntegral = 0.0;
            for (int step = 0; step <= 20; ++step) {
                const double u = static_cast<double>(step) / 20.0;
                const double speed = SpeedCurveUtil::speedAt(curve, u);
                QVERIFY(speed >= 0.05);

                const double integral = SpeedCurveUtil::integratedTime(curve, u);
                QVERIFY(integral >= prevIntegral - 1e-6);
                prevIntegral = integral;

                if (step == 0) {
                    QCOMPARE(integral, 0.0);
                }
            }

            const double avgSpeed = SpeedCurveUtil::averageSpeed(curve);
            QVERIFY(avgSpeed >= 0.05);
            QVERIFY(std::abs(SpeedCurveUtil::integratedTime(curve, 1.0) - avgSpeed) < 1e-6);

            // Invertibility: progressAtSource should invert I(u)/I(1)
            for (int step = 1; step < 10; ++step) {
                const double u = static_cast<double>(step) / 10.0;
                const double normSource = SpeedCurveUtil::integratedTime(curve, u) / avgSpeed;
                const double recoveredU = SpeedCurveUtil::progressAtSource(curve, normSource);
                QVERIFY2(std::abs(recoveredU - u) < 1e-3,
                         qPrintable(QStringLiteral("Preset %1: u=%2, normSource=%3, recoveredU=%4")
                                        .arg(id).arg(u).arg(normSource).arg(recoveredU)));
            }

            // Keyframe time conversions
            const RationalTime sourceIn(0, Rational(30));
            const RationalTime duration(100, Rational(30));
            const RationalTime tOffset(50, Rational(30));
            const RationalTime sTime = SpeedCurveUtil::sourceTimeAt(curve, sourceIn, duration, tOffset, false);
            const RationalTime recoveredOffset = SpeedCurveUtil::timelineOffsetAtSourceTime(curve, sourceIn, duration, sTime, false);
            QVERIFY(std::abs(recoveredOffset.toSecondsDouble() - tOffset.toSecondsDouble()) < 0.1);
        }
    }

    // ---- motion blur (SPEC §5.5) --------------------------------------------------------------------------------
    void motionBlur()
    {
        // 16x16 image with a high-contrast dot in the middle
        Buffer img(16, 16, {0, 0, 0, 255});
        img.set(8, 8, {255, 255, 255, 255});

        MotionBlurSettings zeroBlur;
        zeroBlur.intensity = 0.0;
        applyMotionBlur(img.view(), zeroBlur);
        QCOMPARE(img.at(8, 8)[0], 255);
        QCOMPARE(img.at(9, 8)[0], 0);

        // Horizontal motion blur along 0 degrees
        Buffer blurredH(16, 16, {0, 0, 0, 255});
        blurredH.set(8, 8, {255, 255, 255, 255});
        MotionBlurSettings blurH;
        blurH.intensity = 0.5;
        blurH.angle = 0.0;
        blurH.samples = 9;
        applyMotionBlur(blurredH.view(), blurH);

        // Center pixel energy should spread to horizontal neighbors (x=7, x=9)
        QVERIFY(blurredH.at(7, 8)[0] > 0);
        QVERIFY(blurredH.at(9, 8)[0] > 0);
        // Vertical neighbors (x=8, y=7 and y=9) should have almost zero contribution
        QVERIFY(blurredH.at(8, 7)[0] < blurredH.at(7, 8)[0]);

        // Diagonal motion blur (45 degrees)
        Buffer blurredDiag(16, 16, {0, 0, 0, 255});
        blurredDiag.set(8, 8, {255, 255, 255, 255});
        MotionBlurSettings blurDiag;
        blurDiag.intensity = 0.8;
        blurDiag.angle = 45.0;
        blurDiag.samples = 11;
        applyMotionBlur(blurredDiag.view(), blurDiag);
        QVERIFY(blurredDiag.at(8, 8)[0] > 0);
        QVERIFY(blurredDiag.at(8, 8)[3] == 255);
    }
};

QTEST_GUILESS_MAIN(TestKernels)
#include "tst_kernels.moc"
