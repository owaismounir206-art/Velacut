// SPDX-License-Identifier: GPL-3.0-or-later
// Engine integration test: the MLT projection of a sequence renders what the model says, and incremental
// updates give exactly the same frames as a full rebuild (docs/ARCHITECTURE.md §5.1).
#include "../unit/ProjectFixture.h"
#include "TestMedia.h"

#include "engine/mlt/MltRuntime.h"
#include "engine/timeline/MediaProducerCache.h"
#include "engine/timeline/TimelineProjection.h"

#include <QCryptographicHash>
#include <QTemporaryDir>

#include <mlt++/Mlt.h>

using namespace vedit;
using namespace vedit::engine;
using namespace vedit::test;
using namespace Qt::StringLiterals;

namespace {

QByteArray rgbHash(const QImage &image)
{
    QCryptographicHash hash(QCryptographicHash::Md5);
    const QImage rgb = image.convertToFormat(QImage::Format_RGB888);
    for (int y = 0; y < rgb.height(); ++y) {
        hash.addData(QByteArrayView(reinterpret_cast<const char *>(rgb.constScanLine(y)), rgb.width() * 3));
    }
    return hash.result();
}

QRgb pixel(const QImage &image, int x, int y)
{
    return image.pixel(x, y) & 0x00ffffff;
}

} // namespace

class TestProjection : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;
    TestMediaFiles m_files;
    Media m_landscape;
    Media m_vertical;
    Media m_music;

    ProjectData baseProject() const
    {
        ProjectData data = ProjectData::createEmpty(u"Projection"_s);
        data.settings.frameRate = Rational(30);
        data.settings.defaultCanvas = Canvas{320, 180, CanvasPreset::Landscape16x9};
        data.sequences.front().canvas = data.settings.defaultCanvas;
        data.media = {m_landscape, m_vertical, m_music};
        return data;
    }

    // Renders `positions` of a fresh (full) projection of `data`.
    static QList<QByteArray> renderFresh(const ProjectData &data, const QList<int> &positions)
    {
        auto profile = makeProfile(data, data.mainSequenceId);
        MediaProducerCache cache(*profile);
        TimelineProjection projection(*profile, cache, TimelineProjection::MediaLoading::Wait);
        projection.build(data, data.mainSequenceId);
        QList<QByteArray> hashes;
        for (int position : positions) {
            hashes << rgbHash(projection.renderFrame(position));
        }
        return hashes;
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

    void profileFollowsTheSequence()
    {
        const ProjectData data = baseProject();
        auto profile = makeProfile(data, data.mainSequenceId);
        QCOMPARE(profile->width(), 320);
        QCOMPARE(profile->height(), 180);
        QCOMPARE(profile->fps(), 30.0);
        QVERIFY(profileMatches(*profile, data, data.mainSequenceId));
        ProjectData other = data;
        other.settings.frameRate = Rational(25);
        QVERIFY(!profileMatches(*profile, other, other.mainSequenceId));
    }

    void clipShowsTheRightSourceFrames()
    {
        Session session(baseProject());
        // Whole landscape clip vs. a clip trimmed to start at source frame 30.
        QVERIFY(session.apply(session.editor().insertMedia(m_landscape.id, frames(0))));
        const QList<QByteArray> whole = renderFresh(session.data(), {35, 50});
        const ClipId clip = session.mainTrack().clips.front().id;
        QVERIFY(session.apply(session.editor().trimClip(clip, ClipEdge::Start, frames(30))));
        QCOMPARE(session.mainTrack().clips.front().media()->sourceIn, frames(30));
        // Timeline frame 5 = source frame 35.
        QCOMPARE(renderFresh(session.data(), {5, 20}), whole);
    }

    void durationIsTheEndOfTheLastClip()
    {
        Session session(baseProject());
        QVERIFY(session.apply(session.editor().insertMedia(m_landscape.id, frames(0))));
        QVERIFY(session.apply(session.editor().insertMedia(m_music.id, frames(0))));
        auto profile = makeProfile(session.data(), session.data().mainSequenceId);
        MediaProducerCache cache(*profile);
        TimelineProjection projection(*profile, cache, TimelineProjection::MediaLoading::Wait);
        projection.build(session.data(), session.data().mainSequenceId);
        QCOMPARE(projection.duration(), 180); // music: 6 s at 30 fps
        QVERIFY(projection.warnings().isEmpty());
    }

    void overlayCompositesWithTransparentBorders()
    {
        Session session(baseProject());
        QVERIFY(session.apply(session.editor().insertMedia(m_landscape.id, frames(0))));
        const QList<QByteArray> mainOnly = renderFresh(session.data(), {10, 60});
        QVERIFY(session.apply(session.editor().insertMedia(m_vertical.id, frames(30), std::nullopt, Placement::Overlay)));
        auto profile = makeProfile(session.data(), session.data().mainSequenceId);
        MediaProducerCache cache(*profile);
        TimelineProjection projection(*profile, cache, TimelineProjection::MediaLoading::Wait);
        projection.build(session.data(), session.data().mainSequenceId);
        // Before the overlay starts, the gap on its track must not paint anything.
        QCOMPARE(rgbHash(projection.renderFrame(10)), mainOnly[0]);
        // During the overlay: the pillarboxed vertical video covers the centre, the main track shows at the sides.
        const QImage composite = projection.renderFrame(60);
        QVERIFY(rgbHash(composite) != mainOnly[1]);
        auto mainProjectionProfile = makeProfile(session.data(), session.data().mainSequenceId);
        ProjectData mainOnlyData = session.data();
        mainOnlyData.sequences.front().visualTracks.pop_back();
        MediaProducerCache mainCache(*mainProjectionProfile);
        TimelineProjection mainProjection(*mainProjectionProfile, mainCache, TimelineProjection::MediaLoading::Wait);
        mainProjection.build(mainOnlyData, mainOnlyData.mainSequenceId);
        const QImage below = mainProjection.renderFrame(60);
        QCOMPARE(pixel(composite, 10, 90), pixel(below, 10, 90));     // transparent border of the overlay
        QVERIFY(pixel(composite, 160, 90) != pixel(below, 160, 90)); // overlay content
    }

    void hiddenTrackIsNotRendered()
    {
        Session session(baseProject());
        QVERIFY(session.apply(session.editor().insertMedia(m_landscape.id, frames(0))));
        const QList<QByteArray> mainOnly = renderFresh(session.data(), {60});
        QVERIFY(session.apply(session.editor().insertMedia(m_vertical.id, frames(0), std::nullopt, Placement::Overlay)));
        Track hidden = session.sequence().visualTracks[1];
        hidden.hidden = true;
        EditResult hide;
        hide.script.push_back(edits::setTrackProperties(session.sequence().visualTracks[1], hidden));
        hide.text = u"hide"_s;
        QVERIFY(session.apply(std::move(hide)));
        QCOMPARE(renderFresh(session.data(), {60}), mainOnly);
    }

    void incrementalUpdatesMatchFullRebuild()
    {
        Session session(baseProject());
        auto profile = makeProfile(session.data(), session.data().mainSequenceId);
        MediaProducerCache cache(*profile);
        TimelineProjection projection(*profile, cache, TimelineProjection::MediaLoading::Wait);
        projection.build(session.data(), session.data().mainSequenceId);
        int fullRebuilds = 0;
        connect(&session.project, &Project::changed, this, [&](const ChangeSet &changes) {
            fullRebuilds += projection.update(session.data(), changes) ? 1 : 0;
        });
        const QList<int> positions{0, 45, 100, 130, 170, 230};
        const auto check = [&](const char *step) {
            QList<QByteArray> incremental;
            for (int position : positions) {
                incremental << rgbHash(projection.renderFrame(position));
            }
            if (incremental != renderFresh(session.data(), positions)) {
                QFAIL(qPrintable(u"incremental != rebuild after: "_s + QLatin1StringView(step)));
            }
        };
        QVERIFY(session.apply(session.editor().insertMedia(m_landscape.id, frames(0))));
        check("insert");
        QVERIFY(session.apply(session.editor().insertMedia(m_vertical.id, frames(1000))));
        check("append");
        const ClipId first = session.mainTrack().clips.front().id;
        EditResult split = session.editor().splitClip(first, frames(40));
        const ClipId second = split.primaryClip;
        QVERIFY(session.apply(std::move(split)));
        check("split");
        QVERIFY(session.apply(session.editor().deleteClips({first})));
        check("delete (ripple)");
        QVERIFY(session.apply(session.editor().moveClip(second, frames(1000))));
        check("reorder");
        QVERIFY(session.apply(session.editor().trimClip(second, ClipEdge::End, frames(200))));
        check("trim");
        session.stack.undo();
        session.stack.undo();
        check("undo x2");
        session.stack.redo();
        check("redo");
        QVERIFY(fullRebuilds <= 1);
    }

    void mutedAudioIsSilent()
    {
        Session session(baseProject());
        QVERIFY(session.apply(session.editor().insertMedia(m_music.id, frames(0))));
        const auto level = [](const ProjectData &data) {
            auto profile = makeProfile(data, data.mainSequenceId);
            MediaProducerCache cache(*profile);
            TimelineProjection projection(*profile, cache, TimelineProjection::MediaLoading::Wait);
            projection.build(data, data.mainSequenceId);
            projection.tractor()->seek(30);
            std::unique_ptr<Mlt::Frame> frame(projection.tractor()->get_frame());
            mlt_audio_format format = mlt_audio_s16;
            int frequency = 48000;
            int channels = 2;
            int samples = mlt_audio_calculate_frame_samples(30.0f, frequency, 30);
            const auto *pcm = static_cast<const std::int16_t *>(frame->get_audio(format, frequency, channels, samples));
            int peak = 0;
            for (int i = 0; pcm && i < samples * channels; ++i) {
                peak = std::max(peak, std::abs(int(pcm[i])));
            }
            return peak;
        };
        QVERIFY(level(session.data()) > 1000);
        Track muted = session.sequence().audioTracks[0];
        muted.muted = true;
        EditResult mute;
        mute.script.push_back(edits::setTrackProperties(session.sequence().audioTracks[0], muted));
        mute.text = u"mute"_s;
        QVERIFY(session.apply(std::move(mute)));
        QCOMPARE(level(session.data()), 0);
    }

    void cleanupTestCase() { MltRuntime::shutdown(); }
};

QTEST_GUILESS_MAIN(TestProjection)
#include "tst_projection.moc"
