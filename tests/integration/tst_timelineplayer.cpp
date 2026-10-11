// SPDX-License-Identifier: GPL-3.0-or-later
// Engine integration test: the timeline player plays the live project headless and follows its edits,
// including edits made while it plays (run under ASan/UBSan in the Debug build).
#include "../unit/ProjectFixture.h"
#include "TestMedia.h"

#include "engine/mlt/MltRuntime.h"
#include "engine/analysis/ReverseProxy.h"
#include "engine/playback/TimelinePlayer.h"
#include "engine/timeline/MediaProducerCache.h"
#include "engine/timeline/TimelineProjection.h"

#include <QSignalSpy>
#include <QTemporaryDir>

#include <mlt++/Mlt.h>

using namespace velacut;
using namespace velacut::engine;
using namespace velacut::test;
using namespace Qt::StringLiterals;

namespace {

bool isBlack(const QImage &image)
{
    if (image.isNull()) {
        return true;
    }
    const QRgb centre = image.pixel(image.width() / 2, image.height() / 2);
    return qRed(centre) < 8 && qGreen(centre) < 8 && qBlue(centre) < 8;
}

} // namespace

class TestTimelinePlayer : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;
    TestMediaFiles m_files;
    Media m_landscape;
    Media m_vertical;
    Media m_music;

    ProjectData baseProject() const
    {
        ProjectData data = ProjectData::createEmpty(u"Playback"_s);
        data.settings.frameRate = Rational(30);
        data.settings.defaultCanvas = Canvas{320, 180, CanvasPreset::Landscape16x9};
        data.sequences.front().canvas = data.settings.defaultCanvas;
        data.media = {m_landscape, m_vertical, m_music};
        return data;
    }

    // Waits until the frame on screen is `position` (a new frame after the call).
    static bool waitForShown(TimelinePlayer &player, int position, int timeout = 5000)
    {
        QDeadlineTimer deadline(timeout);
        while (!deadline.hasExpired()) {
            int shown = -1;
            player.sink()->latest(nullptr, &shown);
            if (shown == position && player.shownPosition() == position) {
                return true;
            }
            QTest::qWait(10);
        }
        return false;
    }

private slots:
    void initTestCase()
    {
        m_files = generateTestMedia(m_dir.filePath(u"media"_s));
        if (!m_files.ok) {
            QSKIP("ffmpeg is needed to generate the test media");
        }
        qputenv("SDL_AUDIODRIVER", "dummy");
        QVERIFY(MltRuntime::waitUntilReady());
        m_landscape = testMedia(MediaKind::Video, m_files.landscape, RationalTime(120, Rational(30)), 320, 180, true);
        m_vertical = testMedia(MediaKind::Video, m_files.vertical, RationalTime(120, Rational(30)), 180, 320, true);
        m_music = testMedia(MediaKind::Audio, m_files.music, RationalTime(6 * 48000, Rational(48000)), 0, 0, true);
    }

    void showsTheTimelineWhenMediaIsReady()
    {
        Session session(baseProject());
        QVERIFY(session.apply(session.editor().insertMedia(m_landscape.id, frames(0))));
        TimelinePlayer player;
        player.setVolume(0.0);
        player.setSequence(&session.project, session.data().mainSequenceId);
        QVERIFY(player.ready());
        QCOMPARE(player.canvasSize(), QSize(320, 180));
        QCOMPARE(player.frameRate(), 30.0);
        QCOMPARE(player.duration(), 120);
        // The producer opens in background; then the clip replaces the gap on screen.
        QTRY_VERIFY_WITH_TIMEOUT(!isBlack(player.sink()->latest()), 5000);
        QVERIFY(player.warnings().isEmpty());
    }

    // The render time of the frames is measured on the graph (vedit.timing) and the HUD statistics follow playback.
    void measuresTheFramesItRenders()
    {
        Session session(baseProject());
        QVERIFY(session.apply(session.editor().insertMedia(m_landscape.id, frames(0))));
        TimelinePlayer player;
        player.setVolume(0.0);
        player.setStatsEnabled(true);
        player.setSequence(&session.project, session.data().mainSequenceId);
        QTRY_VERIFY_WITH_TIMEOUT(!isBlack(player.sink()->latest()), 5000);
        QVERIFY(player.frameTiming().frames.load() > 0);
        QVERIFY(player.statsRenderMs() > 0.0);
        QCOMPARE(player.statsRenderSize(), QSize(320, 180));
        QSignalSpy stats(&player, &TimelinePlayer::statsChanged);
        player.play();
        QTRY_VERIFY_WITH_TIMEOUT(stats.count() >= 2 && player.statsFps() > 5.0, 3000);
        player.pause();
    }

    void seekSkimAndShuttle()
    {
        Session session(baseProject());
        QVERIFY(session.apply(session.editor().insertMedia(m_landscape.id, frames(0))));
        TimelinePlayer player;
        player.setVolume(0.0);
        player.setSequence(&session.project, session.data().mainSequenceId);
        QTRY_VERIFY_WITH_TIMEOUT(!isBlack(player.sink()->latest()), 5000);

        player.seek(40);
        QCOMPARE(player.position(), 40);
        QVERIFY(waitForShown(player, 40));

        player.skim(90);
        QVERIFY(player.skimming());
        QVERIFY(waitForShown(player, 90));
        QCOMPARE(player.position(), 40); // skimming never moves the playhead
        player.endSkim();
        QVERIFY(!player.skimming());
        QVERIFY(waitForShown(player, 40));

        player.seek(500); // clamped
        QCOMPARE(player.position(), 119);
        player.step(-9);
        QCOMPARE(player.position(), 110);

        player.shuttleForward();
        QCOMPARE(player.rate(), 1.0);
        player.shuttleForward();
        QCOMPARE(player.rate(), 2.0);
        player.shuttleBackward();
        QCOMPARE(player.rate(), -1.0);
        player.skim(10); // ignored while playing
        QVERIFY(!player.skimming());
        player.pause();
        QCOMPARE(player.rate(), 0.0);
        QVERIFY(!player.playing());
    }

    void scrubMutesTheAudioAndTheCommitRestoresIt()
    {
        Session session(baseProject());
        QVERIFY(session.apply(session.editor().insertMedia(m_landscape.id, frames(0))));
        TimelinePlayer player;
        player.setVolume(0.0);
        player.setSequence(&session.project, session.data().mainSequenceId);
        QTRY_VERIFY_WITH_TIMEOUT(!isBlack(player.sink()->latest()), 5000);

        // A scrub drag that starts while playing: muted (MLT level) and paused, exactly the
        // sequence of calls the timeline view makes when the pointer press starts the drag.
        player.play();
        QVERIFY(player.playing());
        player.setScrubMuted(true);
        QVERIFY(player.scrubMuted());
        player.pause();
        player.scrubSeek(80);
        QCOMPARE(player.position(), 80);
        QVERIFY(!player.playing());
        QVERIFY(waitForShown(player, 80));

        // Release on the frame the preview already landed on: the commit takes the early-return
        // path, and still restores the audio and resumes the playback where it was.
        player.commitSeek(80);
        QVERIFY(!player.scrubMuted());
        QVERIFY(player.playing());
        player.pause();

        // A drag that started paused stays paused, and a commit to another frame is a real seek.
        player.setScrubMuted(true);
        QVERIFY(player.scrubMuted());
        player.scrubSeek(10);
        QVERIFY(waitForShown(player, 10));
        player.commitSeek(60);
        QCOMPARE(player.position(), 60);
        QVERIFY(!player.scrubMuted());
        QVERIFY(!player.playing());
        QVERIFY(waitForShown(player, 60));

        // The hover preview (skim) is silent too, and it never moves the playhead.
        player.skim(20);
        QVERIFY(player.skimming());
        QVERIFY(waitForShown(player, 20));
        player.endSkim();
        QVERIFY(!player.skimming());
        QVERIFY(waitForShown(player, 60));
        QCOMPARE(player.position(), 60);
    }

    // Slow frames (a heavy effect), requests faster than renders: one render at a time, the latest request wins and is
    // never lost, and the render in progress is never thrown away (docs/PERFORMANCE.md, phase 1).
    void skimBurstEndsOnTheLastPosition()
    {
        Session session(baseProject());
        QVERIFY(session.apply(session.editor().insertMedia(m_landscape.id, frames(0))));
        TimelinePlayer player;
        player.setVolume(0.0);
        player.setSequence(&session.project, session.data().mainSequenceId);
        QTRY_VERIFY_WITH_TIMEOUT(!isBlack(player.sink()->latest()), 5000);
        player.setMinimumRenderTime(150);
        // The pointer sweeps over the timeline at 60 events per second and stops on 77.
        for (int frame = 10; frame <= 77; ++frame) {
            player.skim(frame);
            QTest::qWait(16);
        }
        QVERIFY(waitForShown(player, 77, 3000));
        player.endSkim();
        QVERIFY(waitForShown(player, 0, 3000));
    }

    void scrubShowsFramesWhileDraggingWithSlowFrames()
    {
        Session session(baseProject());
        QVERIFY(session.apply(session.editor().insertMedia(m_landscape.id, frames(0))));
        TimelinePlayer player;
        player.setVolume(0.0);
        player.setSequence(&session.project, session.data().mainSequenceId);
        QTRY_VERIFY_WITH_TIMEOUT(!isBlack(player.sink()->latest()), 5000);
        player.setMinimumRenderTime(150);
        const quint64 before = player.sink()->framesReceived();
        player.setScrubMuted(true);
        for (int i = 0; i < 90; ++i) { // 1.5 s at 60 events per second
            player.scrubSeek(10 + i);
            QTest::qWait(16);
        }
        // Before: every new seek purged the render in progress, and nothing was shown until the pointer stopped.
        const quint64 during = player.sink()->framesReceived() - before;
        QVERIFY2(during >= 4, qPrintable(QString::number(during)));
        player.commitSeek(99);
        QVERIFY(waitForShown(player, 99, 3000));
        QCOMPARE(player.position(), 99);
    }

    // Edits faster than renders (an effect slider dragged on a still frame): frames keep coming with the latest values,
    // and the last frame shows the final value.
    void effectSliderUpdatesThePreviewAndEndsExact()
    {
        Session session(baseProject());
        QVERIFY(session.apply(session.editor().insertMedia(m_landscape.id, frames(0))));
        const ClipId clip = session.mainTrack().clips.front().id;
        Effect invert;
        invert.id = EffectId::create();
        invert.type = u"vedit.effect"_s;
        invert.preset = AssetRef{u"vedit.core"_s, u"effects/invert"_s, 1};
        invert.intensity = Param(0.0);
        QVERIFY(session.apply(session.editor().updateClips({clip}, [&](Clip &c) { c.effects.push_back(invert); }, u"invert"_s)));
        TimelinePlayer player;
        player.setVolume(0.0);
        player.setSequence(&session.project, session.data().mainSequenceId);
        QTRY_VERIFY_WITH_TIMEOUT(!isBlack(player.sink()->latest()), 5000);
        player.seek(30);
        QVERIFY(waitForShown(player, 30));
        player.setMinimumRenderTime(120);
        const quint64 before = player.sink()->framesReceived();
        for (int i = 1; i <= 60; ++i) { // 1 s at 60 commands per second, merged like the inspector's
            const double mix = i / 60.0;
            EditResult edit = session.editor().updateClips({clip}, [&](Clip &c) { c.effects.back().intensity = Param(mix); }, u"x"_s);
            session.stack.push(new EditCommand(session.project, edit.text, std::move(edit.script), MergeKey{u"slider"_s, 1}));
            QTest::qWait(16);
        }
        const quint64 during = player.sink()->framesReceived() - before;
        QVERIFY2(during >= 4, qPrintable(QString::number(during)));
        // Final value: fully inverted, exactly as a fresh render of the model.
        auto profile = makeProfile(session.data(), session.data().mainSequenceId);
        MediaProducerCache cache(*profile);
        TimelineProjection projection(*profile, cache, TimelineProjection::MediaLoading::Wait);
        projection.build(session.data(), session.data().mainSequenceId);
        const QImage expected = projection.renderFrame(30).convertToFormat(QImage::Format_RGBA8888);
        QTRY_VERIFY_WITH_TIMEOUT(player.sink()->latest().convertToFormat(QImage::Format_RGBA8888) == expected, 3000);
    }

    // A frame slower than the watchdog's minimum (1 s) is still shown, not purged and asked again.
    void verySlowFrameIsNeverThrownAway()
    {
        Session session(baseProject());
        QVERIFY(session.apply(session.editor().insertMedia(m_landscape.id, frames(0))));
        TimelinePlayer player;
        player.setVolume(0.0);
        player.setSequence(&session.project, session.data().mainSequenceId);
        QTRY_VERIFY_WITH_TIMEOUT(!isBlack(player.sink()->latest()), 5000);
        player.setMinimumRenderTime(1400);
        const quint64 rendered = player.frameTiming().frames.load();
        player.seek(50);
        QVERIFY(waitForShown(player, 50, 6000));
        QTest::qWait(1500);
        // One render for the request; the consumer's own renders of the same still frame reuse it.
        QCOMPARE(player.frameTiming().frames.load(), rendered + 1);
    }

    // The preview renders as many pixels as it shows (never more than the canvas), the quality divides that, and the
    // export path (renderStill) is always at the canvas size.
    void frameSizeFollowsTheViewAndTheQuality()
    {
        Session session(baseProject());
        QVERIFY(session.apply(session.editor().insertMedia(m_landscape.id, frames(0))));
        TimelinePlayer player;
        player.setVolume(0.0);
        player.setPreviewQuality(TimelinePlayer::PreviewQuality::Full);
        player.setSequence(&session.project, session.data().mainSequenceId);
        QTRY_VERIFY_WITH_TIMEOUT(!isBlack(player.sink()->latest()), 5000);
        QCOMPARE(player.sink()->latest().size(), QSize(320, 180));
        player.setViewSize(QSize(170, 200)); // narrow: the canvas fitted in it
        QCOMPARE(player.renderSize(), QSize(170, 96));
        QTRY_COMPARE_WITH_TIMEOUT(player.sink()->latest().size(), QSize(170, 96), 3000);
        player.setPreviewQuality(TimelinePlayer::PreviewQuality::Half);
        QCOMPARE(player.renderScale(), 2);
        QTRY_COMPARE_WITH_TIMEOUT(player.sink()->latest().size(), player.renderSize(), 3000);
        QCOMPARE(player.renderSize(), QSize(86, 48));
        player.setViewSize(QSize(2000, 2000)); // bigger than the canvas: the canvas
        player.setPreviewQuality(TimelinePlayer::PreviewQuality::Full);
        QTRY_COMPARE_WITH_TIMEOUT(player.sink()->latest().size(), QSize(320, 180), 3000);
        player.setViewSize(QSize(160, 90));
        QTRY_COMPARE_WITH_TIMEOUT(player.sink()->latest().size(), QSize(160, 90), 3000);
        // While playing too.
        player.play();
        QTRY_VERIFY_WITH_TIMEOUT(player.sink()->framesReceived() > 10, 3000);
        QCOMPARE(player.sink()->latest().size(), QSize(160, 90));
        player.pause();
        // A saved frame or a cover: the canvas size.
        QCOMPARE(player.renderStill(30).size(), QSize(320, 180));
    }

    // Auto: frames slower than 80 % of a frame's time make the preview one step smaller while playing; paused and quiet,
    // the still frame comes back at the full size.
    void autoQualityStepsDownWhilePlayingAndBackWhenStill()
    {
        Session session(baseProject());
        QVERIFY(session.apply(session.editor().insertMedia(m_landscape.id, frames(0))));
        TimelinePlayer player;
        player.setVolume(0.0);
        player.setSequence(&session.project, session.data().mainSequenceId);
        QTRY_VERIFY_WITH_TIMEOUT(!isBlack(player.sink()->latest()), 5000);
        QCOMPARE(player.previewQuality(), TimelinePlayer::PreviewQuality::Auto);
        QCOMPARE(player.renderScale(), 1);
        player.setMinimumRenderTime(40); // > 80 % of 33 ms
        player.play();
        QTRY_VERIFY_WITH_TIMEOUT(player.renderScale() >= 2, 4000);
        QTRY_VERIFY_WITH_TIMEOUT(player.sink()->latest().width() < 320, 3000);
        player.pause();
        // 250 ms of quiet: the frame at the full size again.
        QTRY_COMPARE_WITH_TIMEOUT(player.renderScale(), 1, 3000);
        QTRY_COMPARE_WITH_TIMEOUT(player.sink()->latest().size(), QSize(320, 180), 3000);
    }

    void playsToTheEndAndStops()
    {
        Session session(baseProject());
        QVERIFY(session.apply(session.editor().insertMedia(m_landscape.id, frames(0))));
        const ClipId clip = session.mainTrack().clips.front().id;
        QVERIFY(session.apply(session.editor().trimClip(clip, ClipEdge::End, frames(30))));
        TimelinePlayer player;
        player.setVolume(0.0);
        player.setSequence(&session.project, session.data().mainSequenceId);
        QCOMPARE(player.duration(), 30);
        QTRY_VERIFY_WITH_TIMEOUT(!isBlack(player.sink()->latest()), 5000);
        const quint64 before = player.sink()->framesReceived();
        player.play();
        QVERIFY(player.playing());
        QTRY_VERIFY_WITH_TIMEOUT(!player.playing(), 5000);
        QCOMPARE(player.position(), 29);
        QVERIFY(player.sink()->framesReceived() - before >= 20);
        // Play again from the end restarts from the beginning.
        player.play();
        QVERIFY(player.position() < 5);
        player.pause();
    }

    void backwardPlaybackStopsAtTheStart()
    {
        Session session(baseProject());
        QVERIFY(session.apply(session.editor().insertMedia(m_landscape.id, frames(0))));
        TimelinePlayer player;
        player.setVolume(0.0);
        player.setSequence(&session.project, session.data().mainSequenceId);
        QTRY_VERIFY_WITH_TIMEOUT(!isBlack(player.sink()->latest()), 5000);
        player.seek(15);
        player.shuttleBackward();
        QTRY_VERIFY_WITH_TIMEOUT(!player.playing(), 10000);
        QCOMPARE(player.position(), 0);
    }

    void followsEditsWhilePlaying()
    {
        Session session(baseProject());
        QVERIFY(session.apply(session.editor().insertMedia(m_landscape.id, frames(0))));
        QVERIFY(session.apply(session.editor().insertMedia(m_vertical.id, frames(1000))));
        TimelinePlayer player;
        player.setVolume(0.0);
        player.setSequence(&session.project, session.data().mainSequenceId);
        QTRY_VERIFY_WITH_TIMEOUT(!isBlack(player.sink()->latest()), 5000);
        QSignalSpy durations(&player, &TimelinePlayer::durationChanged);
        player.play();
        // Edits at a "drag" pace while playing: splits, ripple deletes, moves, trims, undo/redo, a new overlay
        // track (structural rebuild) and its removal.
        for (int round = 0; round < 3; ++round) {
            const ClipId first = session.mainTrack().clips.front().id;
            QVERIFY(session.apply(session.editor().splitClip(first, frames(8 + round * 3))));
            QTest::qWait(30);
            QVERIFY(session.apply(session.editor().moveClip(session.mainTrack().clips.back().id, frames(0))));
            QTest::qWait(30);
            QVERIFY(session.apply(session.editor().trimClip(session.mainTrack().clips.front().id, ClipEdge::End, frames(25))));
            QTest::qWait(30);
            QVERIFY(session.apply(session.editor().insertMedia(m_vertical.id, frames(10), std::nullopt, Placement::Overlay)));
            QTest::qWait(30);
            session.stack.undo();
            QTest::qWait(30);
            session.stack.undo();
            QTest::qWait(30);
            session.stack.redo();
            QTest::qWait(30);
        }
        QCOMPARE(player.duration(), [&] {
            auto profile = makeProfile(session.data(), session.data().mainSequenceId);
            MediaProducerCache cache(*profile);
            TimelineProjection projection(*profile, cache, TimelineProjection::MediaLoading::Wait);
            projection.build(session.data(), session.data().mainSequenceId);
            return projection.duration();
        }());
        QVERIFY(durations.count() > 0);
        // Still playing and still delivering frames after all the edits.
        if (!player.playing()) {
            player.seek(0);
            player.play();
        }
        const quint64 received = player.sink()->framesReceived();
        QTRY_VERIFY_WITH_TIMEOUT(player.sink()->framesReceived() > received + 5, 3000);
        player.pause();
    }

    void newFormatRecreatesTheGraph()
    {
        ProjectData empty = baseProject();
        Session session(empty);
        TimelinePlayer player;
        player.setVolume(0.0);
        player.setSequence(&session.project, session.data().mainSequenceId);
        QCOMPARE(player.duration(), 1);
        QSignalSpy format(&player, &TimelinePlayer::formatChanged);
        // First clip of an empty project: canvas and frame rate follow it (one command).
        ProjectSettings settings = session.data().settings;
        settings.frameRate = Rational(25);
        const Canvas canvas{180, 320, CanvasPreset::Portrait9x16};
        EditResult adopt;
        adopt.script.push_back(edits::setSettings(session.data().settings, settings));
        adopt.script.push_back(edits::setCanvas(session.data().mainSequenceId, session.sequence().canvas, canvas));
        adopt.text = u"format"_s;
        QVERIFY(session.apply(std::move(adopt)));
        QVERIFY(format.count() > 0);
        QCOMPARE(player.frameRate(), 25.0);
        QCOMPARE(player.canvasSize(), QSize(180, 320));
        QVERIFY(session.apply(session.editor().insertMedia(m_vertical.id, RationalTime(0, Rational(25)))));
        QCOMPARE(player.duration(), 100);
        QTRY_VERIFY_WITH_TIMEOUT(!isBlack(player.sink()->latest()), 5000);
        QCOMPARE(player.sink()->latest().size(), QSize(180, 320));
    }

    void reversedClipGetsABackwardsCopy()
    {
        Session session(baseProject());
        QVERIFY(session.apply(session.editor().insertMedia(m_landscape.id, frames(0))));
        const ClipId clip = session.mainTrack().clips.front().id;
        QFile::remove(reverseProxyPath(*session.data().findMedia(m_landscape.id))); // left by an earlier run
        TimelinePlayer player;
        player.setVolume(0.0);
        player.setHelperExecutable(QStringLiteral(VELACUT_RENDER_EXECUTABLE));
        player.setSequence(&session.project, session.data().mainSequenceId);
        QTRY_VERIFY_WITH_TIMEOUT(!isBlack(player.sink()->latest()), 5000);
        const QImage lastForward = [&] {
            auto profile = makeProfile(session.data(), session.data().mainSequenceId);
            MediaProducerCache cache(*profile);
            TimelineProjection projection(*profile, cache, TimelineProjection::MediaLoading::Wait);
            projection.build(session.data(), session.data().mainSequenceId);
            return projection.renderFrame(119);
        }();
        QVERIFY(session.apply(session.editor().updateClips({clip}, [](Clip &c) { c.media()->reversed = true; }, u"reverse"_s)));
        QTRY_VERIFY_WITH_TIMEOUT(player.preparingReverse(), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(!player.preparingReverse(), 60000);
        QVERIFY(QFileInfo::exists(reverseProxyPath(*session.data().findMedia(m_landscape.id))));
        // The preview now reads the copy: the first frame is (a slightly compressed) last frame of the original.
        player.seek(1);
        QVERIFY(waitForShown(player, 1));
        player.seek(0);
        QVERIFY(waitForShown(player, 0));
        QTRY_VERIFY_WITH_TIMEOUT(!isBlack(player.sink()->latest()), 5000);
        QImage first;
        QTRY_VERIFY_WITH_TIMEOUT([&] {
            first = player.sink()->latest();
            return !isBlack(first) && meanDifference(first, lastForward) < 8.0;
        }(), 5000);
        QVERIFY2(meanDifference(first, lastForward) < 8.0, qPrintable(QString::number(meanDifference(first, lastForward))));
    }

    void closeReleasesEverything()
    {
        Session session(baseProject());
        QVERIFY(session.apply(session.editor().insertMedia(m_music.id, frames(0))));
        TimelinePlayer player;
        player.setVolume(0.0);
        player.setSequence(&session.project, session.data().mainSequenceId);
        player.play();
        QTest::qWait(100);
        player.close();
        QVERIFY(!player.ready());
        QVERIFY(!player.playing());
        // Edits after close are ignored.
        QVERIFY(session.apply(session.editor().insertMedia(m_landscape.id, frames(0))));
    }

    void cleanupTestCase() { MltRuntime::shutdown(); }
};

QTEST_GUILESS_MAIN(TestTimelinePlayer)
#include "tst_timelineplayer.moc"
