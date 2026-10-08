// SPDX-License-Identifier: GPL-3.0-or-later
// SPEC §5.11bis and §8 (Phase 5 criterion): "every transition passes the CPU/GPU rendering test". Each of the 114
// transitions is drawn by the CPU reference kernel and by the GPU path (GLSL 1.00/1.10 in an offscreen OpenGL
// context) at several moments; the pictures must agree within a PSNR tolerance. Run twice by CTest: on the machine's
// GPU and on Mesa's software rasterizer (llvmpipe), as SPEC 1bis "Verifica" asks.
#include "engine/gpu/GpuTransitions.h"
#include "fx/Transition.h"

#include <QGuiApplication>
#include <QTest>

#include <cmath>
#include <vector>

using namespace velacut;
using namespace Qt::StringLiterals;

namespace {

double psnr(const std::vector<std::uint8_t> &a, const std::vector<std::uint8_t> &b)
{
    double sum = 0;
    for (size_t i = 0; i < a.size(); ++i) {
        const double d = double(a[i]) - double(b[i]);
        sum += d * d;
    }
    const double mse = sum / double(a.size());
    return mse <= 0 ? 99.0 : 10.0 * std::log10(255.0 * 255.0 / mse);
}

} // namespace

class TestGpuTransitions : public QObject
{
    Q_OBJECT

    static constexpr int kWidth = 192;
    static constexpr int kHeight = 108;
    std::vector<std::uint8_t> m_a;
    std::vector<std::uint8_t> m_b;

private slots:
    void initTestCase()
    {
        engine::GpuTransitions::initialize();
        // Two pictures with detail everywhere (gradients, a checkerboard, hard edges) and a half-transparent corner.
        m_a.resize(kWidth * kHeight * 4);
        m_b.resize(kWidth * kHeight * 4);
        for (int y = 0; y < kHeight; ++y) {
            for (int x = 0; x < kWidth; ++x) {
                std::uint8_t *a = &m_a[(y * kWidth + x) * 4];
                std::uint8_t *b = &m_b[(y * kWidth + x) * 4];
                a[0] = static_cast<std::uint8_t>(x * 255 / kWidth);
                a[1] = static_cast<std::uint8_t>(y * 255 / kHeight);
                a[2] = static_cast<std::uint8_t>((x + y) % 64 < 32 ? 220 : 40);
                a[3] = 255;
                b[0] = static_cast<std::uint8_t>(((x / 12 + y / 12) % 2) * 180 + 30);
                b[1] = static_cast<std::uint8_t>(255 - x * 255 / kWidth);
                b[2] = static_cast<std::uint8_t>(y * 2 % 256);
                b[3] = static_cast<std::uint8_t>(x < kWidth / 4 && y < kHeight / 4 ? 128 : 255);
            }
        }
    }

    void everyTransitionMatchesTheCpu()
    {
        engine::GpuTransitions *gpu = engine::GpuTransitions::instance();
        QVERIFY(gpu);
        // A trial render tells whether this machine has an OpenGL context at all (then the GPU path must work).
        std::vector<std::uint8_t> probe(kWidth * kHeight * 4);
        const fx::ConstImageView a(m_a.data(), kWidth, kHeight, kWidth * 4);
        const fx::ConstImageView b(m_b.data(), kWidth, kHeight, kWidth * 4);
        if (!gpu->render(fx::TransitionKind::Dissolve, fx::ImageView{probe.data(), kWidth, kHeight, kWidth * 4}, a, b, 0.5, {})) {
            if (gpu->failure().contains(u"no OpenGL context"_s)) {
                QSKIP("no OpenGL context on this machine: the transitions run on the CPU (always correct)");
            }
            QFAIL(qPrintable(u"GPU path refused: "_s + gpu->failure()));
        }
        qInfo().noquote() << "GPU:" << gpu->description();

        std::vector<std::uint8_t> cpu(kWidth * kHeight * 4);
        std::vector<std::uint8_t> onGpu(kWidth * kHeight * 4);
        double worst = 99.0;
        QString worstName;
        int compared = 0;
        for (int k = int(fx::TransitionKind::Dissolve); k <= int(fx::TransitionKind::FoldOver); ++k) {
            const auto kind = static_cast<fx::TransitionKind>(k);
            const QString name = QString::fromLatin1(fx::transitionKindName(kind).data(),
                                                     static_cast<qsizetype>(fx::transitionKindName(kind).size()));
            fx::TransitionParams params;
            params.color = {0.9f, 0.2f, 0.4f, 1.0f};
            for (const double progress : {0.2, 0.5, 0.8}) {
                fx::renderTransition(kind, fx::ImageView{cpu.data(), kWidth, kHeight, kWidth * 4}, a, b, progress, params, 0, kHeight);
                QVERIFY2(gpu->render(kind, fx::ImageView{onGpu.data(), kWidth, kHeight, kWidth * 4}, a, b, progress, params),
                         qPrintable(name + u": "_s + gpu->failure()));
                const double quality = psnr(cpu, onGpu);
                if (quality < worst) {
                    worst = quality;
                    worstName = u"%1 at %2"_s.arg(name).arg(progress);
                }
                QVERIFY2(quality >= 40.0, qPrintable(u"%1 at %2: PSNR %3 dB"_s.arg(name).arg(progress).arg(quality, 0, 'f', 1)));
                ++compared;
            }
        }
        QCOMPARE(compared, 114 * 3);
        qInfo().noquote() << u"%1 comparisons, worst PSNR %2 dB (%3)"_s.arg(compared).arg(worst, 0, 'f', 1).arg(worstName);
    }

    void cleanupTestCase() { engine::GpuTransitions::shutdown(); }
};

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    TestGpuTransitions test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_gputransitions.moc"
