// SPDX-License-Identifier: GPL-3.0-or-later
#include "fx/Library.h"
#include "fx/Transition.h"

#include <QImage>
#include <QTest>

#include <cmath>

using namespace velacut::fx;
using namespace Qt::StringLiterals;

class TestTransitions : public QObject
{
    Q_OBJECT

private slots:
    void transitionCPURenders()
    {
        // Create two test images
        QImage imageA(100, 100, QImage::Format_RGBA8888);
        imageA.fill(QColor(255, 0, 0, 255)); // Red

        QImage imageB(100, 100, QImage::Format_RGBA8888);
        imageB.fill(QColor(0, 0, 255, 255)); // Blue

        // Test a simple dissolve transition at 50% progress
        QImage result(100, 100, QImage::Format_RGBA8888);

        renderTransition(TransitionKind::Dissolve,
                        ImageView{result.bits(), result.width(), result.height(), static_cast<int>(result.bytesPerLine())},
                        ConstImageView{imageA.constBits(), imageA.width(), imageA.height(), static_cast<int>(imageA.bytesPerLine())},
                        ConstImageView{imageB.constBits(), imageB.width(), imageB.height(), static_cast<int>(imageB.bytesPerLine())},
                        0.5, TransitionParams{}, 0, result.height());

        // At 50% progress, the middle pixel should be roughly purple (blend of red and blue)
        const QRgb pixel = result.pixel(50, 50);
        const int r = qRed(pixel);
        const int g = qGreen(pixel);
        const int b = qBlue(pixel);

        // Should be approximately (127, 0, 127) for a 50% blend
        QVERIFY(std::abs(r - 127) < 10);
        QVERIFY(g < 10);
        QVERIFY(std::abs(b - 127) < 10);
    }

    void allTransitionsRender()
    {
        const Library &library = Library::core();

        QImage imageA(100, 100, QImage::Format_RGBA8888);
        imageA.fill(QColor(255, 0, 0, 255));

        QImage imageB(100, 100, QImage::Format_RGBA8888);
        imageB.fill(QColor(0, 0, 255, 255));

        QImage result(100, 100, QImage::Format_RGBA8888);

        int tested = 0;
        for (const TransitionPreset &preset : library.transitions()) {
            result.fill(QColor(0, 0, 0, 0));

            renderTransition(preset.kernel,
                           ImageView{result.bits(), result.width(), result.height(), static_cast<int>(result.bytesPerLine())},
                           ConstImageView{imageA.constBits(), imageA.width(), imageA.height(), static_cast<int>(imageA.bytesPerLine())},
                           ConstImageView{imageB.constBits(), imageB.width(), imageB.height(), static_cast<int>(imageB.bytesPerLine())},
                           0.5, TransitionParams{}, 0, result.height());

            // Verify that something was rendered (not all black or all transparent)
            bool hasContent = false;
            for (int y = 0; y < result.height() && !hasContent; ++y) {
                for (int x = 0; x < result.width(); ++x) {
                    const QRgb pixel = result.pixel(x, y);
                    if (qAlpha(pixel) > 0) {
                        hasContent = true;
                        break;
                    }
                }
            }

            QVERIFY2(hasContent, qPrintable(u"Transition %1 rendered all transparent"_s.arg(preset.id)));
            tested++;
        }

        QVERIFY2(tested >= 100, qPrintable(u"Expected >= 100 transitions, got %1"_s.arg(tested)));
    }
};

QTEST_MAIN(TestTransitions)
#include "tst_transitions.moc"
