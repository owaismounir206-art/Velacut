// SPDX-License-Identifier: GPL-3.0-or-later
// Engine integration test: opens a generated test video with MLT and plays it headless.
#include "engine/mlt/MltRuntime.h"
#include "engine/playback/Player.h"

#include <QProcess>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

using namespace vedit::engine;
using namespace Qt::StringLiterals;

class TestPlayer : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;
    QString m_video;

private slots:
    void initTestCase()
    {
        qputenv("SDL_AUDIODRIVER", "dummy");
        const QString ffmpeg = QStandardPaths::findExecutable(u"ffmpeg"_s);
        if (ffmpeg.isEmpty()) {
            QSKIP("ffmpeg is needed to generate the test video");
        }
        m_video = m_dir.filePath(u"testsrc.mp4"_s);
        const int rc = QProcess::execute(ffmpeg, {u"-hide_banner"_s, u"-loglevel"_s, u"error"_s, u"-f"_s, u"lavfi"_s,
                                                  u"-i"_s, u"testsrc2=size=320x240:rate=30"_s, u"-f"_s, u"lavfi"_s, u"-i"_s,
                                                  u"sine=frequency=440:sample_rate=48000"_s, u"-t"_s, u"3"_s,
                                                  u"-c:v"_s, u"libx264"_s, u"-pix_fmt"_s, u"yuv420p"_s, u"-c:a"_s,
                                                  u"aac"_s, u"-shortest"_s, m_video});
        QCOMPARE(rc, 0);
        MltRuntime::initializeAsync();
    }

    void opensAndReportsMetadata()
    {
        Player player;
        QSignalSpy opened(&player, &Player::sourceChanged);
        player.open(m_video);
        QVERIFY(opened.wait(10000));
        QVERIFY(player.ready());
        QCOMPARE(player.videoSize(), QSize(320, 240));
        QCOMPARE(player.frameRate(), 30.0);
        QCOMPARE(player.duration(), 90);
        QVERIFY(player.error().isEmpty());
    }

    void playsFrames()
    {
        Player player;
        player.setVolume(0.0);
        QSignalSpy opened(&player, &Player::sourceChanged);
        player.open(m_video);
        QVERIFY(opened.wait(10000));
        player.play();
        QTRY_VERIFY_WITH_TIMEOUT(player.sink()->framesReceived() >= 30, 5000);
        QVERIFY(player.position() > 0);
        QCOMPARE(player.sink()->frameSize(), QSize(320, 240));
    }

    void seekShowsTheRequestedFrame()
    {
        Player player;
        QSignalSpy opened(&player, &Player::sourceChanged);
        player.open(m_video);
        QVERIFY(opened.wait(10000));
        player.seek(60);
        int position = -1;
        QTRY_VERIFY_WITH_TIMEOUT((player.sink()->latest(nullptr, &position), position == 60), 5000);
    }

    void missingFileIsAnError()
    {
        Player player;
        QSignalSpy state(&player, &Player::stateChanged);
        player.open(m_dir.filePath(u"missing.mp4"_s));
        QTRY_VERIFY_WITH_TIMEOUT(!player.error().isEmpty(), 10000);
        QVERIFY(!player.ready());
    }

    void cleanupTestCase() { MltRuntime::shutdown(); }
};

QTEST_GUILESS_MAIN(TestPlayer)
#include "tst_player.moc"
