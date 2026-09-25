// SPDX-License-Identifier: GPL-3.0-or-later
// "New project" asks nothing: the first clip decides canvas and frame rate (SPEC 0bis rule 1).
#include "ProjectFixture.h"

#include "core/edit/ProjectFormat.h"

using namespace vedit;
using namespace vedit::test;
using namespace Qt::StringLiterals;

namespace {

Media video(int width, int height, Rational rate, int rotation = 0, Rational sar = Rational(1))
{
    Media media = makeMedia(MediaKind::Video, u"v.mp4"_s, RationalTime(10 * rate.num() / rate.den(), rate), true);
    media.info.video->width = width;
    media.info.video->height = height;
    media.info.video->frameRate = rate;
    media.info.video->rotation = rotation;
    media.info.video->sampleAspectRatio = sar;
    return media;
}

} // namespace

class TestProjectFormat : public QObject
{
    Q_OBJECT

private slots:
    void canvasFollowsTheDisplayedPicture()
    {
        QCOMPARE(canvasForMedia(video(1920, 1080, Rational(30))), (Canvas{1920, 1080, CanvasPreset::Landscape16x9}));
        // A phone's portrait video is stored as rotated landscape.
        QCOMPARE(canvasForMedia(video(1920, 1080, Rational(30), 90)), (Canvas{1080, 1920, CanvasPreset::Portrait9x16}));
        QCOMPARE(canvasForMedia(video(1440, 1080, Rational(25), 0, Rational(4, 3))),
                 (Canvas{1920, 1080, CanvasPreset::Landscape16x9}));
        QCOMPARE(canvasForMedia(video(1080, 1350, Rational(30))), (Canvas{1080, 1350, CanvasPreset::Portrait4x5}));
        QCOMPARE(canvasForMedia(video(3840, 2160, Rational(30))), (Canvas{3840, 2160, CanvasPreset::Landscape16x9}));
        // A 24 MP photo is capped at 4K.
        Media photo = makeMedia(MediaKind::Image, u"p.jpg"_s, std::nullopt, false);
        photo.info.video->width = 6000;
        photo.info.video->height = 4000;
        QCOMPARE(canvasForMedia(photo), (Canvas{3240, 2160, CanvasPreset::Custom}));
        QCOMPARE(canvasForMedia(makeMedia(MediaKind::Audio, u"a.mp3"_s, RationalTime(48000, Rational(48000)), true)),
                 std::nullopt);
    }

    void frameRateSnapsToAStandardRate()
    {
        QCOMPARE(frameRateForMedia(video(1920, 1080, Rational(2999, 100))), Rational(30));
        QCOMPARE(frameRateForMedia(video(1920, 1080, Rational(2987, 100))), Rational(30000, 1001));
        QCOMPARE(frameRateForMedia(video(1920, 1080, Rational(30000, 1001))), Rational(30000, 1001));
        QCOMPARE(frameRateForMedia(video(1920, 1080, Rational(25))), Rational(25));
        QCOMPARE(frameRateForMedia(video(1920, 1080, Rational(120))), Rational(60));
        QCOMPARE(frameRateForMedia(video(1920, 1080, Rational(240))), Rational(60));
        QCOMPARE(frameRateForMedia(video(1920, 1080, Rational(120000, 1001))), Rational(60000, 1001));
        QCOMPARE(frameRateForMedia(video(1920, 1080, Rational(15))), Rational(30));
        QCOMPARE(frameRateForMedia(makeMedia(MediaKind::Image, u"p.jpg"_s, std::nullopt, false)), std::nullopt);
    }

    void firstClipSetsTheFormatInOneCommand()
    {
        ProjectData data = ProjectData::createEmpty(u"New"_s);
        QCOMPARE(data.settings.frameRate, Rational(30));
        const Media vertical = video(1920, 1080, Rational(60), 90);
        const Media other = video(1920, 1080, Rational(25));
        data.media = {vertical, other};
        Session session(data);
        QVERIFY(session.apply(insertMediaAdoptingFormat(session.data(), session.data().mainSequenceId, vertical.id,
                                                        RationalTime(0, Rational(30)))));
        QCOMPARE(session.data().settings.frameRate, Rational(60));
        QCOMPARE(session.sequence().canvas, (Canvas{1080, 1920, CanvasPreset::Portrait9x16}));
        QCOMPARE(session.mainTrack().clips.size(), size_t(1));
        QCOMPARE(session.mainTrack().clips.front().duration, RationalTime(600, Rational(60)));
        QCOMPARE(session.stack.count(), 1); // a single undo step brings everything back

        // Later clips never change the format.
        QVERIFY(session.apply(insertMediaAdoptingFormat(session.data(), session.data().mainSequenceId, other.id,
                                                        RationalTime(600, Rational(60)))));
        QCOMPARE(session.data().settings.frameRate, Rational(60));
        QCOMPARE(session.sequence().canvas.height, 1920);
        session.stack.undo();
        session.stack.undo();
        QCOMPARE(session.data().settings.frameRate, Rational(30));
        QCOMPARE(session.sequence().canvas, data.sequences.front().canvas);
    }

    void audioFirstKeepsTheFormat()
    {
        ProjectData data = ProjectData::createEmpty(u"New"_s);
        const Media music = makeMedia(MediaKind::Audio, u"m.flac"_s, RationalTime(48000 * 5, Rational(48000)), true);
        data.media = {music};
        Session session(data);
        QVERIFY(session.apply(insertMediaAdoptingFormat(session.data(), session.data().mainSequenceId, music.id,
                                                        RationalTime(0, Rational(30)))));
        QCOMPARE(session.data().settings, data.settings);
        QCOMPARE(session.sequence().canvas, data.sequences.front().canvas);
    }
};

QTEST_GUILESS_MAIN(TestProjectFormat)
#include "tst_projectformat.moc"
