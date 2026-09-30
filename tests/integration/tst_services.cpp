// SPDX-License-Identifier: GPL-3.0-or-later
// vedit's own MLT services on real frames: transform, adjust, gain, transition, text (docs/ARCHITECTURE.md §5.2).
#include "TestMedia.h"

#include "engine/analysis/BeatDetection.h"
#include "engine/analysis/Spectrum.h"
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

    void transformAnimatesWithKeyframes()
    {
        auto profile = profile320();
        Mlt::Producer video(*profile, m_files.landscape.toUtf8().constData());
        std::unique_ptr<Mlt::Producer> cut(video.cut(0, 29));

        TransformSettings animated;
        animated.sourceWidth = 320;
        animated.sourceHeight = 180;
        animated.frameRate = Rational(30, 1);
        animated.sourceIn = RationalTime(0, 30);

        Keyframe k0{RationalTime(0, 30), Vec2{-0.25, 0.0}, Interpolation::Linear};
        Keyframe k1{RationalTime(30, 30), Vec2{0.25, 0.0}, Interpolation::Linear};
        animated.positionParam.setKeyframes({k0, k1});

        auto filter = makeTransformFilter(*profile, animated);
        cut->attach(*filter);

        // Frame 0: shifted left by 0.25 (left occupied, right 80 columns empty)
        const QImage img0 = frameImage(*cut, 0);
        QCOMPARE(img0.size(), QSize(320, 180));
        QCOMPARE(alphaAt(img0, 300, 90), 0);
        QCOMPARE(alphaAt(img0, 50, 90), 255);

        // Frame 29: shifted right by ~0.25 (right occupied, left 80 columns empty)
        const QImage img29 = frameImage(*cut, 29);
        QCOMPARE(alphaAt(img29, 20, 90), 0);
        QCOMPARE(alphaAt(img29, 250, 90), 255);
    }

    void chromaKeyRemovesGreen()
    {
        auto profile = profile320();
        Mlt::Producer green(*profile, "color:#ff00ff00");
        std::unique_ptr<Mlt::Producer> cut(green.cut(0, 9));

        ChromaKeySettings ck;
        ck.keyColor = Color{0, 255, 0, 255};
        ck.similarity = 0.4;
        ck.smoothness = 0.1;
        ck.spill = 0.5;

        auto filter = makeChromaKeyFilter(*profile, ck);
        cut->attach(*filter);

        const QImage img = frameImage(*cut, 0);
        QCOMPARE(img.size(), QSize(320, 180));
        // Green color keyed out
        QCOMPARE(alphaAt(img, 160, 90), 0);

        // Red color remains opaque
        Mlt::Producer red(*profile, "color:#ffff0000");
        std::unique_ptr<Mlt::Producer> redCut(red.cut(0, 9));
        auto redFilter = makeChromaKeyFilter(*profile, ck);
        redCut->attach(*redFilter);
        const QImage redImg = frameImage(*redCut, 0);
        QCOMPARE(alphaAt(redImg, 160, 90), 255);
        QVERIFY(qRed(redImg.pixel(160, 90)) > 200);
    }

    void maskCutsOutRegion()
    {
        auto profile = profile320();
        Mlt::Producer white(*profile, "color:#ffffffff");
        std::unique_ptr<Mlt::Producer> cut(white.cut(0, 9));

        MaskSettings settings;
        Mask mask;
        mask.id = MaskId::create();
        mask.shape = MaskShape::Rectangle;
        mask.center = Param(Vec2{0.0, 0.0});
        mask.size = Param(Vec2{0.5, 0.5});
        mask.feather = Param(0.0);
        settings.masks.push_back(mask);

        auto filter = makeMaskFilter(*profile, settings);
        cut->attach(*filter);

        const QImage img = frameImage(*cut, 0);
        QCOMPARE(img.size(), QSize(320, 180));
        QCOMPARE(alphaAt(img, 160, 90), 255);
        QCOMPARE(alphaAt(img, 10, 10), 0);
        QCOMPARE(alphaAt(img, 310, 170), 0);
    }

    void textAnimationRendersProgressively()
    {
        auto profile = profile320();
        TextClipData text;
        text.text = u"HELLO WORLD"_s;
        text.style.size = Param(0.3);
        text.style.color = Param(Color{255, 255, 255, 255});
        text.animation = TextAnimation{
            .type = TextAnimationType::Typewriter,
            .scope = TextAnimationScope::Character,
            .duration = RationalTime(30, 30),
            .easing = Easing::preset(Easing::Preset::Linear),
            .cursor = false,
            .stagger = 0.0,
            .params = {}
        };

        const QImage imgStart = TextRenderer::render(text, QSize(320, 180), 0.0, 1.0);
        QCOMPARE(imgStart.size(), QSize(320, 180));

        const QImage imgEnd = TextRenderer::render(text, QSize(320, 180), 1.0, 1.0);
        QCOMPARE(imgEnd.size(), QSize(320, 180));

        int pixelsStart = 0;
        int pixelsEnd = 0;
        for (int y = 0; y < 180; ++y) {
            for (int x = 0; x < 320; ++x) {
                if (qAlpha(imgStart.pixel(x, y)) > 0) {
                    ++pixelsStart;
                }
                if (qAlpha(imgEnd.pixel(x, y)) > 0) {
                    ++pixelsEnd;
                }
            }
        }
        QVERIFY(pixelsStart < pixelsEnd);
        QVERIFY(pixelsEnd > 50);

        auto producer = makeTextProducer(*profile, text);
        const QImage frame0 = frameImage(*producer, 0);
        const QImage frame29 = frameImage(*producer, 29);
        QCOMPARE(frame0.size(), QSize(320, 180));
        QCOMPARE(frame29.size(), QSize(320, 180));
    }

    void speechBubbleAndLowerThird()
    {
        TextClipData bubble;
        bubble.text = u"Speech Bubble"_s;
        bubble.style.size = Param(0.2);
        bubble.style.background = TextBackground{
            Color{0, 120, 255, 255},
            0.2,
            0.4,
            BubbleShape::SpeechRound,
            BubbleTail::BottomLeft,
            0.5,
            Color{255, 255, 255, 255},
            0.05,
            Color{255, 200, 0, 255}
        };

        const QRectF boundsNormal = TextRenderer::bounds(bubble, QSize(320, 180));
        QVERIFY(boundsNormal.width() > 0 && boundsNormal.height() > 0);

        const QImage bubbleImg = TextRenderer::render(bubble, QSize(320, 180));
        QCOMPARE(bubbleImg.size(), QSize(320, 180));
        bool hasBubbleBg = false;
        for (int y = 0; y < 180 && !hasBubbleBg; ++y) {
            for (int x = 0; x < 320 && !hasBubbleBg; ++x) {
                const QRgb c = bubbleImg.pixel(x, y);
                if (qAlpha(c) > 200 && qBlue(c) > 200) {
                    hasBubbleBg = true;
                }
            }
        }
        QVERIFY(hasBubbleBg);

        TextClipData lowerThird;
        lowerThird.text = u"Breaking News"_s;
        lowerThird.style.background = TextBackground{
            Color{20, 20, 20, 230},
            0.2,
            0.2,
            BubbleShape::LowerThirdBar,
            BubbleTail::None,
            0.4,
            Color{},
            0.0,
            Color{255, 0, 0, 255}
        };
        const QImage ltImg = TextRenderer::render(lowerThird, QSize(320, 180));
        QCOMPARE(ltImg.size(), QSize(320, 180));
        bool hasAccent = false;
        for (int y = 0; y < 180 && !hasAccent; ++y) {
            for (int x = 0; x < 320 && !hasAccent; ++x) {
                const QRgb c = ltImg.pixel(x, y);
                if (qAlpha(c) > 200 && qRed(c) > 200 && qGreen(c) < 50 && qBlue(c) < 50) {
                    hasAccent = true;
                }
            }
        }
        QVERIFY(hasAccent);
    }

    void speedRampProducer()
    {
        auto profile = profile320();
        auto base = std::make_shared<Mlt::Producer>(*profile, "color:#0000FF");
        QVERIFY(base->is_valid());
        base->set("length", 300);
        base->set("out", 299);

        const SpeedCurve hero = SpeedCurveUtil::preset(u"hero"_s);
        auto ramp = makeSpeedRampProducer(*profile, base, hero, 0, 150, false);
        QVERIFY(ramp != nullptr);
        QVERIFY(ramp->is_valid());
        QCOMPARE(ramp->get_length(), 150);

        ramp->seek(0);
        std::unique_ptr<Mlt::Frame> f0(ramp->get_frame());
        QVERIFY(f0 != nullptr);
        QVERIFY(f0->is_valid());
        QCOMPARE(f0->get_position(), 0);

        ramp->seek(75);
        std::unique_ptr<Mlt::Frame> f75(ramp->get_frame());
        QVERIFY(f75 != nullptr);
        QVERIFY(f75->is_valid());
        QCOMPARE(f75->get_position(), 75);

        // Also test reversed
        auto rampRev = makeSpeedRampProducer(*profile, base, hero, 0, 150, true);
        QVERIFY(rampRev != nullptr);
        rampRev->seek(149);
        std::unique_ptr<Mlt::Frame> fRev(rampRev->get_frame());
        QVERIFY(fRev != nullptr);
        QVERIFY(fRev->is_valid());
        QCOMPARE(fRev->get_position(), 149);
    }

    void motionBlurFilter()
    {
        auto profile = profile320();
        auto producer = std::make_unique<Mlt::Producer>(*profile, "color:#00FF00");
        QVERIFY(producer->is_valid());
        producer->set("length", 10);
        producer->set("out", 9);

        fx::MotionBlurSettings settings;
        settings.intensity = 0.5;
        settings.angle = 45.0;
        settings.samples = 9;

        auto filter = makeMotionBlurFilter(*profile, settings);
        QVERIFY(filter != nullptr);
        QVERIFY(filter->is_valid());
        producer->attach(*filter);

        producer->seek(0);
        std::unique_ptr<Mlt::Frame> frame(producer->get_frame());
        QVERIFY(frame != nullptr);
        QVERIFY(frame->is_valid());

        mlt_image_format format = mlt_image_rgba;
        int width = 320, height = 180;
        const uint8_t *imgData = frame->get_image(format, width, height);
        QVERIFY(imgData != nullptr);
        QCOMPARE(width, 320);
        QCOMPARE(height, 180);
    }

    // The spectrum puts a sine in the band of its frequency, near 0 dBFS, and the other bands far below.
    void spectrumOfASine()
    {
        const QString sine = m_dir.filePath(u"sine1k.wav"_s);
        QVERIFY(runFfmpeg({u"-f"_s, u"lavfi"_s, u"-i"_s, u"sine=frequency=1000:sample_rate=44100:duration=2"_s, sine}));
        const std::optional<Spectrum> spectrum = extractSpectrum(sine);
        QVERIFY(spectrum);
        QCOMPARE(spectrum->framesPerSecond, 30);
        QVERIFY(std::abs(spectrum->seconds() - 2.0) < 0.1);
        std::vector<float> levels;
        spectrum->levelsAt(1.0, levels);
        QCOMPARE(levels.size(), size_t(32));
        const auto loudest = std::max_element(levels.begin(), levels.end()) - levels.begin();
        const double low = Spectrum::kLowHz * std::pow(Spectrum::kHighHz / Spectrum::kLowHz, loudest / 32.0);
        const double high = Spectrum::kLowHz * std::pow(Spectrum::kHighHz / Spectrum::kLowHz, (loudest + 1) / 32.0);
        QVERIFY2(low <= 1000 * 1.05 && high >= 1000 / 1.05, qPrintable(u"band %1–%2 Hz"_s.arg(low).arg(high)));
        // The sine is at -1/8 amplitude by default in FFmpeg: about -18 dBFS, well above the floor.
        QVERIFY2(levels[static_cast<size_t>(loudest)] > 0.6, qPrintable(QString::number(levels[static_cast<size_t>(loudest)])));
        QVERIFY(levels[0] < 0.2 && levels[31] < 0.2);
        QCOMPARE(spectrum->levelAt(5.0), 0.0); // after the end
        // Disk format round trip.
        const std::optional<Spectrum> decoded = decodeSpectrum(encodeSpectrum(*spectrum));
        QVERIFY(decoded);
        QCOMPARE(decoded->levels, spectrum->levels);
        QVERIFY(!decodeSpectrum("VSPC"));
    }

    // Clicks every half second: the beats are found there, and none in a steady tone.
    void beatsOfAClickTrack()
    {
        const QString clicks = m_dir.filePath(u"clicks.wav"_s);
        QVERIFY(runFfmpeg({u"-f"_s, u"lavfi"_s, u"-i"_s,
                           u"aevalsrc='if(lt(mod(t\\,0.5)\\,0.03)\\,0.8*sin(2*PI*150*t)\\,0)':s=44100:d=4"_s, clicks}));
        const std::optional<Spectrum> spectrum = extractSpectrum(clicks);
        QVERIFY(spectrum);
        const std::vector<double> beats = detectBeats(*spectrum);
        QVERIFY2(beats.size() >= 6 && beats.size() <= 9, qPrintable(QString::number(beats.size())));
        for (const double beat : beats) {
            const double offset = std::fmod(beat + 0.25, 0.5) - 0.25; // distance from the nearest click
            QVERIFY2(std::abs(offset) < 0.06, qPrintable(QString::number(beat)));
        }
        const QString tone = m_dir.filePath(u"tone.wav"_s);
        QVERIFY(runFfmpeg({u"-f"_s, u"lavfi"_s, u"-i"_s, u"sine=frequency=440:duration=3"_s, tone}));
        const std::optional<Spectrum> steady = extractSpectrum(tone);
        QVERIFY(steady);
        QVERIFY(detectBeats(*steady).size() <= 1); // at most the onset of the tone
    }

    void cleanupTestCase() { MltRuntime::shutdown(); }
};

QTEST_MAIN(TestServices)
#include "tst_services.moc"
