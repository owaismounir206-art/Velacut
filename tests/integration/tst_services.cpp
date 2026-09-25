// SPDX-License-Identifier: GPL-3.0-or-later
// vedit's own MLT services on real frames: transform, adjust, gain, transition, text (docs/ARCHITECTURE.md §5.2).
#include "TestMedia.h"

#include "engine/mlt/MltRuntime.h"
#include "engine/mlt/Services.h"
#include "engine/playback/AudioMeters.h"
#include "engine/text/TextRenderer.h"

#include <QTemporaryDir>
#include <QTest>

#include <mlt++/Mlt.h>

#include <cmath>

using namespace vedit;
using namespace vedit::engine;
using namespace vedit::test;
using namespace Qt::StringLiterals;

namespace {

std::unique_ptr<Mlt::Profile> profile320()
{
    auto profile = std::make_unique<Mlt::Profile>();
    profile->set_width(320);
    profile->set_height(180);
    profile->set_frame_rate(30, 1);
    profile->set_sample_aspect(1, 1);
    profile->set_display_aspect(16, 9);
    profile->set_progressive(1);
    profile->set_explicit(1);
    return profile;
}

QImage frameImage(Mlt::Producer &producer, int position, int width = 320, int height = 180)
{
    producer.seek(position);
    std::unique_ptr<Mlt::Frame> frame(producer.get_frame());
    mlt_image_format format = mlt_image_rgba;
    int w = width, h = height;
    const uint8_t *data = frame->get_image(format, w, h);
    return data ? QImage(data, w, h, w * 4, QImage::Format_RGBA8888).copy() : QImage();
}

double audioPeak(Mlt::Producer &producer, int position)
{
    producer.seek(position);
    std::unique_ptr<Mlt::Frame> frame(producer.get_frame());
    mlt_audio_format format = mlt_audio_s16;
    int frequency = 48000, channels = 2;
    int samples = mlt_audio_calculate_frame_samples(30.0f, frequency, position);
    auto *pcm = static_cast<int16_t *>(frame->get_audio(format, frequency, channels, samples));
    double peak = 0;
    for (int i = 0; pcm && i < samples * channels; ++i) {
        peak = std::max(peak, std::abs(pcm[i]) / 32768.0);
    }
    return peak;
}

int alphaAt(const QImage &image, int x, int y)
{
    return qAlpha(image.pixel(x, y));
}

} // namespace

class TestServices : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;
    TestMediaFiles m_files;

private slots:
    void initTestCase()
    {
        m_files = generateTestMedia(m_dir.filePath(u"media"_s));
        if (!m_files.ok) {
            QSKIP("ffmpeg is needed");
        }
        QVERIFY(MltRuntime::waitUntilReady());
    }

    void transformPlacesTheClip()
    {
        auto profile = profile320();
        Mlt::Producer video(*profile, m_files.landscape.toUtf8().constData());
        // Moved right by a quarter of the canvas: the left 80 columns are empty.
        TransformSettings moved;
        moved.sourceWidth = 320;
        moved.sourceHeight = 180;
        moved.x = 0.25;
        {
            std::unique_ptr<Mlt::Producer> cut(video.cut(0, 29));
            auto filter = makeTransformFilter(*profile, moved);
            cut->attach(*filter);
            const QImage image = frameImage(*cut, 5);
            QCOMPARE(image.size(), QSize(320, 180));
            QCOMPARE(alphaAt(image, 40, 90), 0);
            QCOMPARE(alphaAt(image, 200, 90), 255);
            // Same content, shifted: canvas (80 + x) = source x.
            const QImage plain = frameImage(video, 5);
            QCOMPARE(image.pixel(80 + 100, 90), plain.pixel(100, 90));
        }
        // Half size and cropped: a smaller rectangle, the cropped side empty.
        TransformSettings small = moved;
        small.x = 0;
        small.scaleX = small.scaleY = 0.5;
        small.cropLeft = 0.5;
        {
            std::unique_ptr<Mlt::Producer> cut(video.cut(0, 29));
            auto filter = makeTransformFilter(*profile, small);
            cut->attach(*filter);
            const QImage image = frameImage(*cut, 5);
            QCOMPARE(alphaAt(image, 160, 10), 0);   // above the half-size picture
            QCOMPARE(alphaAt(image, 100, 90), 0);   // its left half is cropped
            QCOMPARE(alphaAt(image, 200, 90), 255); // its right half shows
        }
        // Rotated 90°: taller than wide, the corners empty.
        TransformSettings rotated = moved;
        rotated.x = 0;
        rotated.rotation = 90;
        {
            std::unique_ptr<Mlt::Producer> cut(video.cut(0, 29));
            auto filter = makeTransformFilter(*profile, rotated);
            cut->attach(*filter);
            const QImage image = frameImage(*cut, 5);
            QCOMPARE(alphaAt(image, 5, 5), 0);
            QCOMPARE(alphaAt(image, 160, 90), 255);
        }
    }

    void blurredBackgroundFillsTheCanvas()
    {
        auto profile = profile320();
        Mlt::Producer vertical(*profile, m_files.vertical.toUtf8().constData());
        TransformSettings settings;
        settings.sourceWidth = 180;
        settings.sourceHeight = 320;
        {
            std::unique_ptr<Mlt::Producer> cut(vertical.cut(0, 29));
            auto filter = makeTransformFilter(*profile, settings);
            cut->attach(*filter);
            QCOMPARE(alphaAt(frameImage(*cut, 3), 10, 90), 0); // pillarbox: transparent sides
        }
        settings.background = CanvasBackground{BackgroundType::Blur, Color{}, 0.6, {}, std::nullopt};
        {
            std::unique_ptr<Mlt::Producer> cut(vertical.cut(0, 29));
            auto filter = makeTransformFilter(*profile, settings);
            cut->attach(*filter);
            const QImage image = frameImage(*cut, 3);
            QCOMPARE(alphaAt(image, 10, 90), 255); // the blurred clip fills the sides
            QVERIFY(qRed(image.pixel(10, 90)) + qGreen(image.pixel(10, 90)) + qBlue(image.pixel(10, 90)) > 30);
        }
        settings.background = CanvasBackground{BackgroundType::Color, Color{0, 200, 0, 255}, 0.6, {}, std::nullopt};
        {
            std::unique_ptr<Mlt::Producer> cut(vertical.cut(0, 29));
            auto filter = makeTransformFilter(*profile, settings);
            cut->attach(*filter);
            QCOMPARE(frameImage(*cut, 3).pixel(10, 90), qRgba(0, 200, 0, 255));
        }
    }

    void adjustChangesColours()
    {
        auto profile = profile320();
        Mlt::Producer video(*profile, m_files.landscape.toUtf8().constData());
        AdjustSettings mono;
        mono.look.saturation = -1;
        std::unique_ptr<Mlt::Producer> cut(video.cut(0, 29));
        auto filter = makeAdjustFilter(*profile, mono);
        cut->attach(*filter);
        const QImage image = frameImage(*cut, 5);
        for (const QPoint p : {QPoint(20, 20), QPoint(160, 90), QPoint(300, 170)}) {
            const QRgb c = image.pixel(p);
            QVERIFY(std::abs(qRed(c) - qGreen(c)) <= 2 && std::abs(qGreen(c) - qBlue(c)) <= 2);
        }
    }

    void gainFadesFromTheClipStart()
    {
        auto profile = profile320();
        Mlt::Producer video(*profile, m_files.landscape.toUtf8().constData());
        GainSettings settings;
        settings.gainDb = -6.0206; // half
        settings.meterKey = "test";
        settings.fadeInFrames = 10;
        settings.length = 60;
        settings.firstFrame = 60; // the cut starts at source frame 60: the fade covers its own first 10 frames
        std::unique_ptr<Mlt::Producer> cut(video.cut(60, 119));
        auto filter = makeGainFilter(*profile, settings);
        cut->attach(*filter);
        std::unique_ptr<Mlt::Producer> plain(video.cut(60, 119));
        // Compared frame by frame with the same frames without the filter.
        const double first = audioPeak(*cut, 0) / audioPeak(*plain, 0);
        const double middle = audioPeak(*cut, 5) / audioPeak(*plain, 5);
        const double after = audioPeak(*cut, 20) / audioPeak(*plain, 20);
        QVERIFY2(first < 0.1, qPrintable(QString::number(first)));
        QVERIFY2(middle > 0.15 && middle < 0.4, qPrintable(QString::number(middle)));
        QVERIFY2(std::abs(after - 0.5) < 0.02, qPrintable(QString::number(after)));
        QVERIFY(AudioMeters::level("test") > 0.0f);
    }

    void transitionMixesTwoClips()
    {
        auto profile = profile320();
        Mlt::Producer red(*profile, "color:#ffff0000");
        Mlt::Producer blue(*profile, "color:#ff0000ff");
        Mlt::Tractor mini(*profile);
        Mlt::Playlist a(*profile);
        Mlt::Playlist b(*profile);
        a.append(red, 0, 8);
        b.append(blue, 0, 8);
        mini.set_track(a, 0);
        mini.set_track(b, 1);
        TransitionSettings settings;
        settings.kind = fx::TransitionKind::Dissolve;
        settings.easing = fx::Easing::Linear;
        auto transition = makeTransition(*profile, settings);
        transition->set_in_and_out(0, 8);
        mini.plant_transition(*transition, 0, 1);
        // 9 frames: frame 4 is the middle, (4 + 1) / 10 = 0.5.
        const QRgb middle = frameImage(mini, 4).pixel(160, 90);
        QVERIFY(std::abs(qRed(middle) - 128) <= 2 && std::abs(qBlue(middle) - 128) <= 2);
        QVERIFY(qRed(frameImage(mini, 0).pixel(10, 10)) > 200);
        QVERIFY(qBlue(frameImage(mini, 8).pixel(10, 10)) > 200);
    }

    void textIsDrawnInTheMiddle()
    {
        auto profile = profile320();
        TextClipData text;
        text.text = u"HI"_s;
        text.style.size = Param(0.4);
        text.style.color = Param(Color{255, 255, 255, 255});
        text.style.stroke = TextStroke{Param(Color{0, 0, 0, 255}), 0.1};
        auto producer = makeTextProducer(*profile, text);
        const QImage image = frameImage(*producer, 0);
        QCOMPARE(image.size(), QSize(320, 180));
        QCOMPARE(alphaAt(image, 5, 5), 0);
        // Somewhere near the centre there is white text.
        bool white = false;
        for (int x = 100; x < 220 && !white; ++x) {
            const QRgb c = image.pixel(x, 90);
            white = qAlpha(c) == 255 && qRed(c) > 240 && qGreen(c) > 240;
        }
        QVERIFY(white);
        const QRectF bounds = TextRenderer::bounds(text, QSize(320, 180));
        QVERIFY(std::abs(bounds.center().x() - 160) < 1 && std::abs(bounds.center().y() - 90) < 1);
        QVERIFY(bounds.height() > 60 && bounds.height() < 120);
    }

    void cleanupTestCase() { MltRuntime::shutdown(); }
};

QTEST_MAIN(TestServices)
#include "tst_services.moc"
