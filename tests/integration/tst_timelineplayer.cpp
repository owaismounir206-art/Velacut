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

using namespace vedit;
using namespace vedit::engine;
using namespace vedit::test;
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
        player.setHelperExecutable(QStringLiteral(VEDIT_RENDER_EXECUTABLE));
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
