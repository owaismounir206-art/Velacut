// SPDX-License-Identifier: GPL-3.0-or-later
// Engine integration test: the MLT projection of a sequence renders what the model says, and incremental
// updates give exactly the same frames as a full rebuild (docs/ARCHITECTURE.md §5.1).
#include "../unit/ProjectFixture.h"
#include "TestMedia.h"

#include "core/serialization/ProjectJson.h"
#include "fx/Library.h"
#include "fx/Stabilization.h"
#include "engine/mlt/MltRuntime.h"
#include "engine/timeline/ClipPlacement.h"
#include "engine/timeline/MediaProducerCache.h"
#include "engine/analysis/AudioSync.h"
#include "engine/analysis/Decoding.h"
#include "engine/analysis/SmoothMotion.h"
#include "engine/text/CaptionRenderer.h"
#include "engine/timeline/TimelineProjection.h"

#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFile>
#include <QPainter>
#include <QTemporaryDir>

#include <mlt++/Mlt.h>

#include <cstring>

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

    static QImage renderFresh1(const ProjectData &data, int position)
    {
        auto profile = makeProfile(data, data.mainSequenceId);
        MediaProducerCache cache(*profile);
        TimelineProjection projection(*profile, cache, TimelineProjection::MediaLoading::Wait);
        projection.build(data, data.mainSequenceId);
        return projection.renderFrame(position);
    }

    static Effect filterEffect(const QString &id)
    {
        Effect effect;
        effect.id = EffectId::create();
        effect.type = u"vedit.filter"_s;
        effect.preset = AssetRef{u"vedit.core"_s, id, 1};
        return effect;
    }

    static AssetRef dissolveRef() { return AssetRef{u"vedit.core"_s, u"transitions/dissolve"_s, 1}; }

    ProjectData baseProjectWithClip()
    {
        Session session(baseProject());
        if (!session.apply(session.editor().insertMedia(m_landscape.id, frames(0)))) {
            qWarning("setup failed");
        }
        return session.data();
    }

    QImage lastOfAHelper(const ProjectData &withTransition)
    {
        // The same timeline without the transition, at frame 100 (outside the transition window).
        ProjectData data = withTransition;
        data.sequences.front().visualTracks.front().transitions.clear();
        return renderFresh1(data, 100);
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

    void backgroundIsOpaqueBlackAndColorClipsKeepTheirColor()
    {
        // MLT's colour strings are #AARRGGBB: a wrong order shows up here as transparent or tinted pixels.
        Session session(baseProject());
        const auto centre = [](const ProjectData &data, int position) {
            auto profile = makeProfile(data, data.mainSequenceId);
            MediaProducerCache cache(*profile);
            TimelineProjection projection(*profile, cache, TimelineProjection::MediaLoading::Wait);
            projection.build(data, data.mainSequenceId);
            return projection.renderFrame(position).pixel(160, 90);
        };
        QCOMPARE(centre(session.data(), 0), qRgba(0, 0, 0, 255));
        EditResult color;
        Clip clip;
        clip.id = ClipId::create();
        clip.start = frames(0);
        clip.duration = frames(20);
        clip.payload = ColorClipData{Param(Color{255, 128, 0, 255})};
        color.script.push_back(edits::insertClip(session.mainTrack().id, clip));
        color.text = u"color"_s;
        QVERIFY(session.apply(std::move(color)));
        QCOMPARE(centre(session.data(), 10), qRgba(255, 128, 0, 255));
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
        // Phase 2 edits patch the running graph too.
        const std::vector<ClipId> all{session.mainTrack().clips[0].id, session.mainTrack().clips[1].id};
        QVERIFY(session.apply(session.editor().updateClips(all, [](Clip &clip) { clip.effects.push_back(filterEffect(u"filters/bw"_s)); }, u"filter"_s)));
        check("filter on all");
        QVERIFY(session.apply(session.editor().updateClips({all[1]}, [](Clip &clip) {
            clip.transform.position = Param(Vec2{0.2, -0.1});
            clip.transform.scale = Param(Vec2{0.7, 0.7});
        }, u"move"_s)));
        check("transform");
        QVERIFY(session.apply(session.editor().addTransition(all[0], dissolveRef(), frames(12))));
        check("transition");
        QVERIFY(session.apply(session.editor().setSpeed(all[1], 2.0)));
        check("speed");
        QVERIFY(fullRebuilds <= 1);
    }

    // ---- Phase 2 ------------------------------------------------------------------------------------------------
    void transformMovesTheClip()
    {
        Session session(baseProject());
        QVERIFY(session.apply(session.editor().insertMedia(m_landscape.id, frames(0))));
        const QImage plain = renderFresh1(session.data(), 10);
        const ClipId clip = session.mainTrack().clips.front().id;
        QVERIFY(session.apply(session.editor().updateClips({clip}, [](Clip &c) { c.transform.position = Param(Vec2{0.25, 0.0}); }, u"move"_s)));
        const QImage moved = renderFresh1(session.data(), 10);
        QCOMPARE(pixel(moved, 40, 90), 0u); // the black canvas where the picture was
        QCOMPARE(pixel(moved, 80 + 120, 90), pixel(plain, 120, 90));
    }

    void filtersAndAdjustments()
    {
        Session session(baseProject());
        QVERIFY(session.apply(session.editor().insertMedia(m_landscape.id, frames(0))));
        const ClipId clip = session.mainTrack().clips.front().id;
        QVERIFY(session.apply(session.editor().updateClips({clip}, [](Clip &c) { c.effects.push_back(filterEffect(u"filters/bw"_s)); }, u"bw"_s)));
        const QImage grey = renderFresh1(session.data(), 10);
        for (const QPoint p : {QPoint(30, 30), QPoint(160, 90), QPoint(290, 150)}) {
            const QRgb c = grey.pixel(p);
            QVERIFY(std::abs(qRed(c) - qGreen(c)) <= 2 && std::abs(qGreen(c) - qBlue(c)) <= 2);
        }
        // Intensity 0: the original.
        QVERIFY(session.apply(session.editor().updateClips({clip}, [](Clip &c) { c.effects.front().intensity = Param(0.0); }, u"0"_s)));
        Session plain(baseProject());
        QVERIFY(plain.apply(plain.editor().insertMedia(m_landscape.id, frames(0))));
        QCOMPARE(rgbHash(renderFresh1(session.data(), 10)), rgbHash(renderFresh1(plain.data(), 10)));
        // Adjust: exposure up brightens.
        QVERIFY(session.apply(session.editor().updateClips({clip}, [](Clip &c) {
            Effect adjust;
            adjust.id = EffectId::create();
            adjust.type = u"vedit.adjust.basic"_s;
            adjust.params[u"exposure"_s] = Param(1.0);
            c.effects = {adjust};
        }, u"exposure"_s)));
        const QImage brighter = renderFresh1(session.data(), 10);
        const QImage original = renderFresh1(plain.data(), 10);
        long sumA = 0, sumB = 0;
        for (int x = 0; x < 320; x += 7) {
            sumA += qGray(brighter.pixel(x, 90));
            sumB += qGray(original.pixel(x, 90));
        }
        QVERIFY(sumA > sumB);
    }

    void speedAndReverse()
    {
        Session session(baseProject());
        QVERIFY(session.apply(session.editor().insertMedia(m_landscape.id, frames(0))));
        const QByteArray source20 = rgbHash(renderFresh1(session.data(), 20));
        const QByteArray source119 = rgbHash(renderFresh1(session.data(), 119));
        const ClipId clip = session.mainTrack().clips.front().id;
        QVERIFY(session.apply(session.editor().setSpeed(clip, 2.0)));
        QCOMPARE(session.mainTrack().clips.front().duration, frames(60));
        QCOMPARE(rgbHash(renderFresh1(session.data(), 10)), source20); // frame 10 at 2x = source frame 20
        QVERIFY(session.apply(session.editor().setSpeed(clip, 1.0)));
        QVERIFY(session.apply(session.editor().updateClips({clip}, [](Clip &c) { c.media()->reversed = true; }, u"reverse"_s)));
        QCOMPARE(rgbHash(renderFresh1(session.data(), 0)), source119); // backwards: starts with the last frame
    }

    void textOverTheVideo()
    {
        Session session(baseProject());
        QVERIFY(session.apply(session.editor().insertMedia(m_landscape.id, frames(0))));
        const QImage before = renderFresh1(session.data(), 10);
        TextClipData text;
        text.text = u"TITLE"_s;
        text.style.size = Param(0.2);
        text.style.color = Param(Color{255, 255, 0, 255});
        QVERIFY(session.apply(session.editor().insertText(frames(0), text, frames(60))));
        const QImage after = renderFresh1(session.data(), 10);
        int changed = 0;
        for (int x = 60; x < 260; ++x) {
            changed += pixel(after, x, 90) != pixel(before, x, 90) ? 1 : 0;
        }
        QVERIFY2(changed > 20, qPrintable(QString::number(changed)));
        QCOMPARE(pixel(after, 5, 5), pixel(before, 5, 5)); // outside the text: the video
        // Outside the text clip's time: just the video.
        QCOMPARE(rgbHash(renderFresh1(session.data(), 90)), rgbHash(renderFresh1(Session(baseProjectWithClip()).data(), 90)));
    }

    void captionsHighlightTheWordBeingSaid()
    {
        // Over the black background: the test video has coloured parts of its own.
        Session session(baseProject());
        CaptionStyle style;
        style.text.size = Param(0.22);
        style.text.color = Param(Color{255, 0, 0, 255});
        style.text.fontWeight = 900;
        style.position = 0.0;
        style.highlight = CaptionHighlight::Color;
        style.highlightColor = Color{0, 255, 0, 255};
        const std::vector<captions::CaptionLine> lines{
            {frames(0), frames(60), u"MMM MMM"_s,
             {TimedWord{u"MMM"_s, frames(0), frames(30)}, TimedWord{u"MMM"_s, frames(30), frames(60)}}},
            {frames(80), frames(100), u"MMM"_s, {}}};
        QVERIFY(session.apply(session.editor().insertCaptions(lines, style)));
        // Green (the word being said) on the left at first, then on the right; red for the other word.
        const auto count = [](const QImage &image, QRgb color, bool left) {
            int n = 0;
            for (int y = 40; y < 140; ++y) {
                for (int x = left ? 0 : 160; x < (left ? 160 : 320); ++x) {
                    const QColor c = image.pixelColor(x, y);
                    const bool match = color == qRgb(0, 255, 0) ? (c.green() > 200 && c.red() < 60 && c.blue() < 60)
                                                                : (c.red() > 200 && c.green() < 60 && c.blue() < 60);
                    n += match ? 1 : 0;
                }
            }
            return n;
        };
        const QImage first = renderFresh1(session.data(), 10);
        QVERIFY2(count(first, qRgb(0, 255, 0), true) > 100, "the first word is highlighted");
        QCOMPARE(count(first, qRgb(0, 255, 0), false), 0);
        QVERIFY(count(first, qRgb(255, 0, 0), false) > 100);
        const QImage second = renderFresh1(session.data(), 40);
        QCOMPARE(count(second, qRgb(0, 255, 0), true), 0);
        QVERIFY2(count(second, qRgb(0, 255, 0), false) > 100, "then the second");

        // One word at a time: the second word replaces the first in the middle.
        style.maxWordsPerLine = 1;
        QVERIFY(session.apply(session.editor().insertCaptions(lines, style, true)));
        const QImage alone = renderFresh1(session.data(), 10);
        QCOMPARE(count(alone, qRgb(255, 0, 0), false) + count(alone, qRgb(255, 0, 0), true), 0);
        QVERIFY(count(alone, qRgb(0, 255, 0), true) > 50 && count(alone, qRgb(0, 255, 0), false) > 50);
        QCOMPARE(rgbHash(renderFresh1(session.data(), 59)), rgbHash(renderFresh1(session.data(), 10)));
        // Between the lines: nothing.
        const QImage between = renderFresh1(session.data(), 70);
        QImage black(between.size(), between.format());
        black.fill(Qt::black);
        QCOMPARE(between, black);
    }

    void captionFramesAreReusedBetweenChanges()
    {
        SubtitleClipData line;
        line.text = u"uno due tre"_s;
        line.words = {TimedWord{u"uno"_s, frames(0), frames(20)}, TimedWord{u"due"_s, frames(20), frames(40)},
                      TimedWord{u"tre"_s, frames(40), frames(60)}};
        CaptionStyle style;
        style.highlight = CaptionHighlight::Box;
        auto layout = CaptionRenderer::layout(line, style, 60, QSize(320, 180), Rational(30));
        QCOMPARE(layout->groups.size(), 1u);
        // Same word, no animation: same picture (the producer reuses it).
        QCOMPARE(CaptionRenderer::stateKey(*layout, 5), CaptionRenderer::stateKey(*layout, 15));
        QVERIFY(CaptionRenderer::stateKey(*layout, 15) != CaptionRenderer::stateKey(*layout, 25));
        QCOMPARE(CaptionRenderer::stateKey(*layout, 60), std::int64_t(-1));
        QCOMPARE(rgbHash(CaptionRenderer::render(*layout, 5, QSize(320, 180))),
                 rgbHash(CaptionRenderer::render(*layout, 15, QSize(320, 180))));
        // A pop animation changes the picture during its first frames only.
        style.animation = CaptionAnimation::Pop;
        style.maxWordsPerLine = 2;
        layout = CaptionRenderer::layout(line, style, 60, QSize(320, 180), Rational(30));
        QCOMPARE(layout->groups.size(), 2u);
        QCOMPARE(layout->groups[1].start, std::int64_t(40));
        QVERIFY(CaptionRenderer::stateKey(*layout, 1) != CaptionRenderer::stateKey(*layout, 2));
        QCOMPARE(CaptionRenderer::stateKey(*layout, 12), CaptionRenderer::stateKey(*layout, 18));
        // Drawn at half size: the same picture, smaller.
        const QImage half = CaptionRenderer::render(*layout, 12, QSize(160, 90));
        QCOMPARE(half.size(), QSize(160, 90));
        QVERIFY(!CaptionRenderer::bounds(*layout, 12).isEmpty());
    }

    // Slow motion at half speed: frames repeat in pairs; with "smooth", every frame is a different picture (new ones
    // computed between the real ones), and the clip still shows the same moment at the same time.
    void smoothSlowMotionMakesNewFrames()
    {
        Session session(baseProject());
        QVERIFY(session.apply(session.editor().insertMedia(m_landscape.id, frames(0))));
        const ClipId clip = session.mainTrack().clips.front().id;
        QVERIFY(session.apply(session.editor().setSpeed(clip, 0.5)));
        const auto repeats = [this](const ProjectData &data) {
            auto profile = makeProfile(data, data.mainSequenceId);
            MediaProducerCache cache(*profile);
            TimelineProjection projection(*profile, cache, TimelineProjection::MediaLoading::Wait);
            projection.build(data, data.mainSequenceId);
            int same = 0;
            QByteArray previous = rgbHash(projection.renderFrame(20));
            for (int position = 21; position < 41; ++position) {
                const QByteArray current = rgbHash(projection.renderFrame(position));
                same += current == previous ? 1 : 0;
                previous = current;
            }
            return same;
        };
        const int plain = repeats(session.data());
        QVERIFY2(plain >= 8, qPrintable(QString::number(plain)));
        const QImage plainFrame = renderFresh1(session.data(), 40);
        QVERIFY(session.apply(session.editor().updateClips({clip}, [](Clip &c) { c.media()->smooth = true; }, u"Smooth"_s)));
        QElapsedTimer timer;
        timer.start();
        const int smooth = repeats(session.data()); // the export path: the copy is made now
        qInfo("smooth slow motion: %d repeated frames of 20 (%d without), copy made in %lld ms", smooth, plain, timer.elapsed());
        QCOMPARE(smooth, 0);
        // At a real frame (frame 40 = source frame 20) the picture is the same as without.
        QVERIFY2(meanDifference(renderFresh1(session.data(), 40), plainFrame) < 6.0,
                 qPrintable(QString::number(meanDifference(renderFresh1(session.data(), 40), plainFrame))));
        const std::optional<SmoothCopy> copy = smoothCopyFor(session.mainTrack().clips.front(), m_landscape);
        QVERIFY(copy && copy->ready());
        QCOMPARE(copy->frameRate, Rational(60));
    }

    // A video with transparency (what "Remove background" makes) shows what is under it through its transparent parts.
    void videoWithTransparencyShowsWhatIsUnder()
    {
        const QString file = m_dir.filePath(u"alpha.mov"_s);
        QVERIFY(runFfmpeg({u"-f"_s, u"lavfi"_s, u"-i"_s,
                           u"color=c=red:s=320x180:r=30:d=1,format=rgba,geq=r='255':g='0':b='0':a='if(lt(X,160),255,0)'"_s,
                           u"-c:v"_s, u"qtrle"_s, u"-pix_fmt"_s, u"argb"_s, file}));
        Media alpha = testMedia(MediaKind::Video, file, RationalTime(30, Rational(30)), 320, 180, false);
        alpha.info.video->hasAlpha = true;
        ProjectData data = baseProject();
        data.media.push_back(alpha);
        Session session(data);
        // The test picture below, the half-transparent red video above.
        QVERIFY(session.apply(session.editor().insertMedia(m_landscape.id, frames(0))));
        const QImage under = renderFresh1(session.data(), 10);
        QVERIFY(session.apply(session.editor().insertMedia(alpha.id, frames(0), std::nullopt, Placement::Overlay)));
        const QImage frame = renderFresh1(session.data(), 10);
        if (const QString folder = qEnvironmentVariable("VEDIT_UI_SHOTS"); !folder.isEmpty()) {
            frame.save(folder + u"/alpha.png"_s);
        }
        QCOMPARE(QColor(frame.pixel(60, 90)), QColor(255, 0, 0));
        QCOMPARE(QColor(frame.pixel(260, 90)), QColor(under.pixel(260, 90)));
    }

    // A shaky shot (a textured picture seen through a jittering window) is steadied: the picture moves much less from one
    // rendered frame to the next.
    void stabilizationSteadiesTheShot()
    {
        const QString file = m_dir.filePath(u"shaky.mp4"_s);
        QVERIFY(runFfmpeg({u"-f"_s, u"lavfi"_s, u"-i"_s,
                           u"nullsrc=s=400x225:r=30:d=3,format=gray,geq=lum='128+60*sin(X/7)*cos(Y/5)+40*sin((X+2*Y)/11)',"
                           "crop=w=320:h=180:x='40+14*sin(n*1.9)':y='22+9*cos(n*2.7)',format=yuv420p"_s,
                           u"-c:v"_s, u"libx264"_s, u"-crf"_s, u"16"_s, file}));
        const Media shaky = testMedia(MediaKind::Video, file, RationalTime(90, Rational(30)), 320, 180, false);
        ProjectData data = baseProject();
        data.media.push_back(shaky);
        Session session(data);
        QVERIFY(session.apply(session.editor().insertMedia(shaky.id, frames(0))));
        const auto shake = [](const ProjectData &project) {
            auto profile = makeProfile(project, project.mainSequenceId);
            MediaProducerCache cache(*profile);
            TimelineProjection projection(*profile, cache, TimelineProjection::MediaLoading::Wait);
            projection.build(project, project.mainSequenceId);
            const auto grey = [](const QImage &image) {
                const QImage small = image.scaled(160, 90, Qt::IgnoreAspectRatio, Qt::SmoothTransformation).convertToFormat(QImage::Format_Grayscale8);
                std::vector<std::uint8_t> pixels(160 * 90);
                for (int y = 0; y < 90; ++y) {
                    std::memcpy(pixels.data() + y * 160, small.constScanLine(y), 160);
                }
                return pixels;
            };
            double total = 0.0;
            std::vector<std::uint8_t> previous = grey(projection.renderFrame(20));
            for (int position = 21; position < 70; ++position) {
                std::vector<std::uint8_t> current = grey(projection.renderFrame(position));
                const fx::CameraStep step = fx::estimateCameraStep(previous, current, 160, 90);
                total += std::abs(step.dx * 160.0) + std::abs(step.dy * 90.0);
                previous = std::move(current);
            }
            return total / 49.0;
        };
        const double before = shake(session.data());
        Rational rate;
        const auto steps = extractCameraSteps(file, 0.0, 3.0, &rate);
        QVERIFY(steps && steps->size() >= 85);
        QCOMPARE(rate, Rational(30));
        const ClipId clip = session.mainTrack().clips.front().id;
        QVERIFY(session.apply(session.editor().updateClips({clip}, [&](Clip &c) {
            Effect effect;
            effect.id = EffectId::create();
            effect.type = u"vedit.stabilize"_s;
            effect.params[u"strength"_s] = Param(0.8);
            effect.params[u"motion"_s] = Param(QString::fromLatin1(fx::encodeCameraSteps(*steps)));
            effect.params[u"motionRate"_s] = Param(rate.toString());
            effect.params[u"motionStart"_s] = Param(u"0"_s);
            c.effects.push_back(effect);
        }, u"Stabilize"_s)));
        const double after = shake(session.data());
        qInfo("shake: %.2f -> %.2f pixels per frame", before, after);
        if (const QString folder = qEnvironmentVariable("VEDIT_UI_SHOTS"); !folder.isEmpty()) {
            renderFresh1(session.data(), 40).save(folder + u"/stabilized-40.png"_s);
        }
        QVERIFY2(before > 2.0, qPrintable(QString::number(before)));
        QVERIFY2(after < before * 0.3, qPrintable(QStringLiteral("%1 → %2 pixels per frame").arg(before).arg(after)));
    }

    // Every caption style of the library draws its words, in a vertical video (VEDIT_UI_SHOTS=<folder> saves a contact
    // sheet to look at them).
    void everyCaptionStyleDraws()
    {
        SubtitleClipData line;
        line.text = u"Questo è il momento migliore della giornata!"_s;
        const fx::Library &library = fx::Library::core();
        const QSize canvas(540, 960);
        const int columns = 8;
        const int rows = static_cast<int>((library.captionStyles().size() + columns - 1) / columns);
        QImage sheet(canvas.width() / 2 * columns, canvas.height() / 2 * rows, QImage::Format_RGB888);
        sheet.fill(QColor(70, 90, 110));
        QPainter painter(&sheet);
        int index = 0;
        for (const fx::CaptionStylePreset &preset : library.captionStyles()) {
            const CaptionStyle style = projectjson::captionStyleFromJson(preset.style);
            const auto layout = CaptionRenderer::layout(line, style, 150, canvas, Rational(30));
            QVERIFY2(!layout->groups.empty(), qPrintable(preset.id));
            // Late in the line, after any entry animation: the words are on screen.
            const std::int64_t frame = layout->groups.front().start + 20;
            const QImage image = CaptionRenderer::render(*layout, frame, canvas);
            int opaque = 0;
            for (int y = 0; y < image.height(); y += 2) {
                for (int x = 0; x < image.width(); x += 2) {
                    opaque += qAlpha(image.pixel(x, y)) > 200 ? 1 : 0;
                }
            }
            QVERIFY2(opaque > 200, qPrintable(preset.id + u' ' + QString::number(opaque)));
            const QRect cell(index % columns * canvas.width() / 2, index / columns * canvas.height() / 2, canvas.width() / 2,
                             canvas.height() / 2);
            painter.drawImage(cell, image);
            painter.setPen(Qt::white);
            painter.drawText(cell.adjusted(8, 8, -8, -8), Qt::AlignTop | Qt::AlignLeft, preset.id.section(u'/', 1));
            ++index;
        }
        painter.end();
        const QString folder = qEnvironmentVariable("VEDIT_UI_SHOTS");
        if (!folder.isEmpty()) {
            sheet.save(folder + u"/caption-styles.png"_s);
        }
    }

    // The box the preview's handles draw (ClipPlacement) is where vedit.transform renders the clip.
    void canvasBoxMatchesTheRender()
    {
        const auto litBounds = [](const QImage &image) {
            QRect bounds;
            for (int y = 0; y < image.height(); ++y) {
                for (int x = 0; x < image.width(); ++x) {
                    if (qGray(image.pixel(x, y)) > 128) {
                        bounds |= QRect(x, y, 1, 1);
                    }
                }
            }
            return bounds;
        };
        for (const double rotation : {0.0, 90.0}) {
            Session session(baseProject());
            TextClipData text;
            text.text = u"TITLE"_s;
            text.style.size = Param(0.12);
            text.style.background = TextBackground{Color{255, 255, 255, 255}, 0.25, 0.0};
            QVERIFY(session.apply(session.editor().insertText(frames(0), text, frames(60))));
            const ClipId id = session.data().mainSequence()->visualTracks.back().clips.front().id;
            QVERIFY(session.apply(session.editor().updateClips({id}, [rotation](Clip &clip) {
                clip.transform.position = Param(Vec2{0.1, -0.1});
                clip.transform.scale = Param(Vec2{0.8, 0.8});
                clip.transform.rotation = Param(rotation);
            }, u"place"_s)));
            const QImage frame = renderFresh1(session.data(), 10);
            const CanvasBox box = canvasBox(*session.data().findClip(id), nullptr, frame.size());
            const QRect lit = litBounds(frame);
            const QSizeF expected = rotation == 0 ? box.size : box.size.transposed();
            QVERIFY2(std::abs(lit.center().x() - box.centre.x()) <= 2 && std::abs(lit.center().y() - box.centre.y()) <= 2,
                     qPrintable(u"%1,%2 vs %3,%4"_s.arg(lit.center().x()).arg(lit.center().y()).arg(box.centre.x()).arg(box.centre.y())));
            QVERIFY2(std::abs(lit.width() - expected.width()) <= 3 && std::abs(lit.height() - expected.height()) <= 3,
                     qPrintable(u"%1x%2 vs %3x%4"_s.arg(lit.width()).arg(lit.height()).arg(expected.width()).arg(expected.height())));
        }
        // A vertical video on a 16:9 canvas: as high as the canvas, centred.
        Clip clip;
        MediaClipData media;
        media.mediaId = m_vertical.id;
        clip.payload = media;
        const CanvasBox box = canvasBox(clip, &m_vertical, QSize(320, 180));
        QCOMPARE(box.centre, QPointF(160, 90));
        QCOMPARE(box.size.height(), 180.0);
        QVERIFY(std::abs(box.size.width() - 180.0 * 9 / 16) < 1);
        clip.transform.fit = FitMode::Cover;
        QCOMPARE(canvasBox(clip, &m_vertical, QSize(320, 180)).size.width(), 320.0);
    }

    // Keyframes stay on the content (D-05): at 2× a keyframe at source frame 60 is reached at timeline frame 30.
    void keyframesFollowTheSpeed()
    {
        const auto opacityAt = [this](double speed, bool animated, int position) {
            Session session(baseProject());
            if (!session.apply(session.editor().insertMedia(m_landscape.id, frames(0)))) {
                return QImage();
            }
            const ClipId id = session.mainTrack().clips.front().id;
            if (speed != 1.0 && !session.apply(session.editor().setSpeed(id, speed))) {
                return QImage();
            }
            if (animated) {
                // Opacity 0 at source frame 0, 1 at source frame 60.
                const bool ok = session.apply(session.editor().updateClips({id}, [](Clip &clip) {
                    clip.opacity.setKeyframes({Keyframe{frames(0), 0.0, Interpolation::Linear}, Keyframe{frames(60), 1.0, Interpolation::Linear}});
                }, u"animate"_s));
                if (!ok) {
                    return QImage();
                }
            }
            return renderFresh1(session.data(), position);
        };
        // Timeline frame 15 at 2× shows source frame 30: half way.
        const QImage full = opacityAt(2.0, false, 15);
        const QImage half = opacityAt(2.0, true, 15);
        QVERIFY(!full.isNull() && !half.isNull());
        const QRgb f = full.pixel(160, 90);
        const QRgb h = half.pixel(160, 90);
        QVERIFY2(std::abs(qRed(h) - qRed(f) / 2) <= 6 && std::abs(qGreen(h) - qGreen(f) / 2) <= 6,
                 qPrintable(u"%1,%2 vs half of %3,%4"_s.arg(qRed(h)).arg(qGreen(h)).arg(qRed(f)).arg(qGreen(f))));
        // Timeline frame 30 at 2× = source frame 60: fully visible.
        QCOMPARE(rgbHash(opacityAt(2.0, true, 30)), rgbHash(opacityAt(2.0, false, 30)));
    }

    // Changing a keyframe (not only adding one) reaches a running projection.
    void keyframeChangesUpdateTheProjection()
    {
        Session session(baseProject());
        QVERIFY(session.apply(session.editor().insertMedia(m_landscape.id, frames(0))));
        const ClipId id = session.mainTrack().clips.front().id;
        const auto animate = [&](double end) {
            return session.apply(session.editor().updateClips({id}, [end](Clip &clip) {
                clip.opacity.setKeyframes({Keyframe{frames(0), 0.0, Interpolation::Linear}, Keyframe{frames(60), end, Interpolation::Linear}});
            }, u"animate"_s));
        };
        QVERIFY(animate(1.0));
        auto profile = makeProfile(session.data(), session.data().mainSequenceId);
        MediaProducerCache cache(*profile);
        TimelineProjection projection(*profile, cache, TimelineProjection::MediaLoading::Wait);
        projection.build(session.data(), session.data().mainSequenceId);
        connect(&session.project, &Project::changed, this, [&](const ChangeSet &changes) { projection.update(session.data(), changes); });
        const QByteArray before = rgbHash(projection.renderFrame(30));
        QVERIFY(animate(0.2)); // same number of keyframes, another value
        const QByteArray after = rgbHash(projection.renderFrame(30));
        QVERIFY(after != before);
        QCOMPARE(after, rgbHash(renderFresh1(session.data(), 30)));
    }

    // A preset entry animation (SPEC §5.6): a text fading in over its first half second.
    void presetFadeInAnimation()
    {
        Session session(baseProject());
        TextClipData text;
        text.text = u"TITLE"_s;
        text.style.size = Param(0.2);
        QVERIFY(session.apply(session.editor().insertText(frames(0), text, frames(60))));
        const ClipId id = session.data().mainSequence()->visualTracks.back().clips.front().id;
        ClipAnimations animations;
        ClipAnimation in;
        in.type = AssetRef{u"vedit.core"_s, u"animations/in/fade"_s, 1};
        in.duration = frames(15);
        in.easing = Easing::preset(Easing::Preset::Linear);
        animations.in = in;
        const QImage still = renderFresh1(session.data(), 20);
        QVERIFY(session.apply(session.editor().setClipAnimations(id, animations)));
        const auto brightest = [](const QImage &image) {
            int best = 0;
            for (int y = 0; y < image.height(); ++y) {
                for (int x = 0; x < image.width(); ++x) {
                    best = std::max(best, qGray(image.pixel(x, y)));
                }
            }
            return best;
        };
        QCOMPARE(brightest(renderFresh1(session.data(), 0)), 0);   // not visible yet
        const int middle = brightest(renderFresh1(session.data(), 7));
        QVERIFY2(middle > 60 && middle < 200, qPrintable(QString::number(middle)));
        QCOMPARE(rgbHash(renderFresh1(session.data(), 20)), rgbHash(still)); // after it: as without animation
    }

    void dissolveBetweenClipsFreezesMissingFrames()
    {
        // Two whole clips: no material beyond the cut, so the edge frames are frozen during the transition.
        Session session(baseProject());
        QVERIFY(session.apply(session.editor().insertMedia(m_landscape.id, frames(0))));
        QVERIFY(session.apply(session.editor().insertMedia(m_vertical.id, frames(1000))));
        const QImage lastOfA = renderFresh1(session.data(), 119);
        const QImage firstOfB = renderFresh1(session.data(), 120);
        const ClipId a = session.mainTrack().clips.front().id;
        std::map<QString, Param> linear{{u"easing"_s, Param(u"linear"_s)}};
        QVERIFY(session.apply(session.editor().addTransition(a, dissolveRef(), frames(10), linear)));
        // Window [115, 125): at frame 119 (5 of 10) the mix is (5 + 1) / 11 of B.
        const QImage middle = renderFresh1(session.data(), 119);
        const QRgb m = middle.pixel(160, 90);
        const QRgb pa = lastOfA.pixel(160, 90);
        const double t = 6.0 / 11.0;
        const QRgb pb = renderFresh1(session.data(), 125).pixel(160, 90); // B at frame 125 = first frame + 5
        Q_UNUSED(pb);
        // B's frame shown at 119 is its first frame frozen (no material before its start).
        const QRgb pbFrozen = firstOfB.pixel(160, 90);
        QVERIFY2(std::abs(qRed(m) - (qRed(pa) * (1 - t) + qRed(pbFrozen) * t)) <= 3,
                 qPrintable(u"%1 vs %2/%3"_s.arg(qRed(m)).arg(qRed(pa)).arg(qRed(pbFrozen))));
        // Outside the window: untouched.
        QCOMPARE(rgbHash(renderFresh1(session.data(), 100)), rgbHash(lastOfAHelper(session.data())));
        QCOMPARE(session.mainTrack().transitions.size(), size_t(1));
    }

    void backgroundAndTrackVolume()
    {
        Session session(baseProject());
        QVERIFY(session.apply(session.editor().insertMedia(m_vertical.id, frames(0))));
        QCOMPARE(pixel(renderFresh1(session.data(), 5), 10, 90), 0u); // pillarbox: black canvas
        QVERIFY(session.apply(session.editor().setDefaultBackground(CanvasBackground{BackgroundType::Blur, Color{}, 0.6, {}, std::nullopt})));
        QVERIFY(pixel(renderFresh1(session.data(), 5), 10, 90) != 0u);
        const auto peak = [](const ProjectData &data) {
            auto profile = makeProfile(data, data.mainSequenceId);
            MediaProducerCache cache(*profile);
            TimelineProjection projection(*profile, cache, TimelineProjection::MediaLoading::Wait);
            projection.build(data, data.mainSequenceId);
            projection.tractor()->seek(30);
            std::unique_ptr<Mlt::Frame> frame(projection.tractor()->get_frame());
            mlt_audio_format format = mlt_audio_s16;
            int frequency = 48000, channels = 2;
            int samples = mlt_audio_calculate_frame_samples(30.0f, frequency, 30);
            const auto *pcm = static_cast<const std::int16_t *>(frame->get_audio(format, frequency, channels, samples));
            int result = 0;
            for (int i = 0; pcm && i < samples * channels; ++i) {
                result = std::max(result, std::abs(int(pcm[i])));
            }
            return result;
        };
        const int full = peak(session.data());
        QVERIFY(session.apply(session.editor().updateTrack(session.mainTrack().id, [](Track &t) { t.gainDb = Param(-6.0206); }, u"vol"_s)));
        const int half = peak(session.data());
        QVERIFY2(std::abs(half - full / 2) <= full / 20, qPrintable(u"%1 %2"_s.arg(full).arg(half)));
    }

    void livePreviewDoesNotTouchTheModel()
    {
        Session session(baseProject());
        QVERIFY(session.apply(session.editor().insertMedia(m_landscape.id, frames(0))));
        auto profile = makeProfile(session.data(), session.data().mainSequenceId);
        MediaProducerCache cache(*profile);
        TimelineProjection projection(*profile, cache, TimelineProjection::MediaLoading::Wait);
        projection.build(session.data(), session.data().mainSequenceId);
        const QByteArray original = rgbHash(projection.renderFrame(10));
        Clip previewed = session.mainTrack().clips.front();
        previewed.effects.push_back(filterEffect(u"filters/bw"_s));
        const ProjectData before = session.data();
        projection.setPreview(session.data(), TimelineProjection::Preview{previewed, std::nullopt, std::nullopt, std::nullopt, std::nullopt});
        const QImage grey = projection.renderFrame(10);
        QVERIFY(rgbHash(grey) != original);
        QVERIFY(std::abs(qRed(grey.pixel(160, 90)) - qGreen(grey.pixel(160, 90))) <= 2);
        projection.setPreview(session.data(), {});
        QCOMPARE(rgbHash(projection.renderFrame(10)), original);
        QVERIFY(session.data() == before);
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

    void chromaKeyRemovesGreenInProjection()
    {
        ProjectData data = baseProject();
        Clip redClip;
        redClip.id = ClipId::create();
        redClip.start = frames(0);
        redClip.duration = frames(30);
        redClip.payload = ColorClipData{Param(Color{255, 0, 0, 255})};
        data.sequences.front().visualTracks.front().clips.push_back(redClip);

        Track overlayTrack;
        overlayTrack.id = TrackId::create();
        overlayTrack.kind = TrackKind::Video;
        Clip greenClip;
        greenClip.id = ClipId::create();
        greenClip.start = frames(0);
        greenClip.duration = frames(30);
        greenClip.payload = ColorClipData{Param(Color{0, 255, 0, 255})};

        Effect ck;
        ck.type = u"vedit.chroma_key"_s;
        ck.params[u"keyColor"_s] = Param(Color{0, 255, 0, 255});
        ck.params[u"similarity"_s] = Param(0.4);
        ck.params[u"smoothness"_s] = Param(0.1);
        greenClip.effects.push_back(ck);
        overlayTrack.clips.push_back(greenClip);
        data.sequences.front().visualTracks.push_back(overlayTrack);

        const QImage keyed = renderFresh1(data, 10);
        const QRgb p = pixel(keyed, 160, 90);
        QVERIFY(qRed(p) > 200);
        QVERIFY(qGreen(p) < 50);

        data.sequences.front().visualTracks[1].clips.front().effects.clear();
        const QImage opaqueGreen = renderFresh1(data, 10);
        const QRgb pGreen = pixel(opaqueGreen, 160, 90);
        QVERIFY(qGreen(pGreen) > 200);
        QVERIFY(qRed(pGreen) < 50);
    }

    void animatedTransformInProjection()
    {
        ProjectData data = baseProject();
        Clip clip;
        clip.id = ClipId::create();
        clip.start = frames(0);
        clip.duration = frames(30);
        clip.payload = ColorClipData{Param(Color{255, 255, 255, 255})};

        Keyframe k0{frames(0), Vec2{-0.25, 0.0}, Interpolation::Linear};
        Keyframe k1{frames(30), Vec2{0.25, 0.0}, Interpolation::Linear};
        clip.transform.position.setKeyframes({k0, k1});

        data.sequences.front().visualTracks.front().clips.push_back(clip);

        const QImage frame0 = renderFresh1(data, 0);
        QCOMPARE(pixel(frame0, 50, 90), 0x00ffffffu);
        QCOMPARE(pixel(frame0, 300, 90), 0x00000000u);

        const QImage frame29 = renderFresh1(data, 29);
        QCOMPARE(pixel(frame29, 270, 90), 0x00ffffffu);
        QCOMPARE(pixel(frame29, 20, 90), 0x00000000u);
    }

    void greenScreenCompositionWithMaskInProjection()
    {
        ProjectData data = baseProject();

        // Track 0 (bottom): Blue background
        Clip blueClip;
        blueClip.id = ClipId::create();
        blueClip.start = frames(0);
        blueClip.duration = frames(30);
        blueClip.payload = ColorClipData{Param(Color{0, 0, 255, 255})};
        data.sequences.front().visualTracks.front().clips.push_back(blueClip);

        // Track 1 (top overlay): Green clip with chroma key AND a mask
        Track overlayTrack;
        overlayTrack.id = TrackId::create();
        overlayTrack.kind = TrackKind::Video;

        Clip overlayClip;
        overlayClip.id = ClipId::create();
        overlayClip.start = frames(0);
        overlayClip.duration = frames(30);
        overlayClip.payload = ColorClipData{Param(Color{0, 255, 0, 255})};

        Effect ck;
        ck.type = u"vedit.chroma_key"_s;
        ck.params[u"keyColor"_s] = Param(Color{0, 255, 0, 255});
        ck.params[u"similarity"_s] = Param(0.4);
        overlayClip.effects.push_back(ck);

        Mask mask;
        mask.id = MaskId::create();
        mask.shape = MaskShape::Rectangle;
        mask.center = Param(Vec2{0.0, 0.0});
        mask.size = Param(Vec2{0.5, 0.5});
        mask.feather = Param(0.0);
        overlayClip.masks.push_back(mask);

        overlayTrack.clips.push_back(overlayClip);
        data.sequences.front().visualTracks.push_back(overlayTrack);

        const QImage comp = renderFresh1(data, 10);
        const QRgb centerP = pixel(comp, 160, 90);
        QVERIFY(qBlue(centerP) > 200 && qGreen(centerP) < 50);

        const QRgb cornerP = pixel(comp, 10, 10);
        QVERIFY(qBlue(cornerP) > 200 && qGreen(cornerP) < 50);
    }

    void compoundClipRendersInProjection()
    {
        ProjectData data = baseProject();

        // 1. Create a nested sequence with a red color clip
        Sequence nested;
        nested.id = SequenceId::create();
        nested.name = u"Nested"_s;
        nested.canvas = data.settings.defaultCanvas;
        Track nestedTrack;
        nestedTrack.id = TrackId::create();
        nestedTrack.kind = TrackKind::Video;
        Clip redClip;
        redClip.id = ClipId::create();
        redClip.start = frames(0);
        redClip.duration = frames(30);
        redClip.payload = ColorClipData{Param(Color{255, 0, 0, 255})};
        nestedTrack.clips.push_back(redClip);
        nested.visualTracks.push_back(nestedTrack);
        data.sequences.push_back(nested);

        // 2. Main sequence contains a compound clip referencing the nested sequence
        Clip compClip;
        compClip.id = ClipId::create();
        compClip.start = frames(0);
        compClip.duration = frames(30);
        compClip.payload = CompoundClipData{nested.id, frames(0)};
        data.sequences.front().visualTracks.front().clips.push_back(compClip);

        // Render frame 5: should be red!
        const QImage img = renderFresh1(data, 5);
        const QRgb p = pixel(img, 160, 90);
        QVERIFY(qRed(p) > 200 && qGreen(p) < 50 && qBlue(p) < 50);
    }

    void multicamAngleSwitchingAndAudioSync()
    {
        // 1. Test waveform alignment with synthetic waveforms
        Waveform wf1;
        wf1.bucketsPerSecond = 100;
        // 5 seconds of signal with a prominent pulse at 2.0s (bucket 200)
        wf1.peaks.resize(500 * 2);
        for (int i = 0; i < 500; ++i) {
            int8_t val = (i >= 200 && i <= 210) ? 100 : 10;
            wf1.peaks[2 * i] = -val;
            wf1.peaks[2 * i + 1] = val;
        }

        Waveform wf2;
        wf2.bucketsPerSecond = 100;
        // Same signal delayed by 1.0s (pulse at 3.0s, bucket 300)
        wf2.peaks.resize(500 * 2);
        for (int i = 0; i < 500; ++i) {
            int8_t val = (i >= 300 && i <= 310) ? 100 : 10;
            wf2.peaks[2 * i] = -val;
            wf2.peaks[2 * i + 1] = val;
        }

        AudioSyncResult syncRes = alignWaveforms(wf1, wf2);
        QVERIFY(syncRes.matched);
        QVERIFY(syncRes.confidence > 0.8);
        QCOMPARE(std::round(syncRes.offsetSeconds * 100.0) / 100.0, 1.0);

        // 2. Multicam projection angle switching
        ProjectData data = baseProject();
        Sequence nested;
        nested.id = SequenceId::create();
        nested.name = u"NestedMulticam"_s;
        nested.canvas = data.settings.defaultCanvas;

        // Angle 0: Red clip
        Track track0;
        track0.id = TrackId::create();
        track0.kind = TrackKind::Video;
        Clip redClip;
        redClip.id = ClipId::create();
        redClip.start = frames(0);
        redClip.duration = frames(30);
        redClip.payload = ColorClipData{Param(Color{255, 0, 0, 255})};
        track0.clips.push_back(redClip);
        nested.visualTracks.push_back(track0);

        // Angle 1: Blue clip
        Track track1;
        track1.id = TrackId::create();
        track1.kind = TrackKind::Video;
        Clip blueClip;
        blueClip.id = ClipId::create();
        blueClip.start = frames(0);
        blueClip.duration = frames(30);
        blueClip.payload = ColorClipData{Param(Color{0, 0, 255, 255})};
        track1.clips.push_back(blueClip);
        nested.visualTracks.push_back(track1);

        data.sequences.push_back(nested);

        // Compound clip on main sequence with activeAngle = 0
        Clip compClip;
        compClip.id = ClipId::create();
        compClip.start = frames(0);
        compClip.duration = frames(30);
        compClip.payload = CompoundClipData{nested.id, frames(0), 0};
        data.sequences.front().visualTracks.front().clips.push_back(compClip);

        // Render frame 5: angle 0 is active -> pixel is Red!
        QImage img0 = renderFresh1(data, 5);
        QRgb p0 = pixel(img0, 160, 90);
        QVERIFY(qRed(p0) > 200 && qGreen(p0) < 50 && qBlue(p0) < 50);

        // Switch to angle 1 (Blue)
        compClip.payload = CompoundClipData{nested.id, frames(0), 1};
        data.sequences.front().visualTracks.front().clips.front() = compClip;

        // Render frame 5: angle 1 is active -> pixel is Blue!
        QImage img1 = renderFresh1(data, 5);
        QRgb p1 = pixel(img1, 160, 90);
        QVERIFY(qRed(p1) < 50 && qGreen(p1) < 50 && qBlue(p1) > 200);
    }

    void adjustmentLayerRendersInProjection()
    {
        ProjectData data = baseProject();

        // Track 0 (bottom): White clip (255, 255, 255)
        Clip whiteClip;
        whiteClip.id = ClipId::create();
        whiteClip.start = frames(0);
        whiteClip.duration = frames(30);
        whiteClip.payload = ColorClipData{Param(Color{255, 255, 255, 255})};
        data.sequences.front().visualTracks.front().clips.push_back(whiteClip);

        // Track 1 (adjustment track): Adjustment clip with exposure = -1.0
        Track adjTrack;
        adjTrack.id = TrackId::create();
        adjTrack.kind = TrackKind::Adjustment;

        Clip adjClip;
        adjClip.id = ClipId::create();
        adjClip.start = frames(0);
        adjClip.duration = frames(30);
        adjClip.payload = AdjustmentClipData{};

        Effect adjEffect;
        adjEffect.id = EffectId::create();
        adjEffect.type = u"vedit.adjust.basic"_s;
        adjEffect.params[u"exposure"_s] = Param(-1.0);
        adjClip.effects.push_back(adjEffect);

        adjTrack.clips.push_back(adjClip);
        data.sequences.front().visualTracks.push_back(adjTrack);

        // Render frame 10: exposure -1.0 halves RGB values from 255 to ~128
        const QImage img = renderFresh1(data, 10);
        const QRgb p = pixel(img, 160, 90);
        QVERIFY(qRed(p) < 200 && qRed(p) > 50);
        QVERIFY(qGreen(p) < 200 && qGreen(p) > 50);
        QVERIFY(qBlue(p) < 200 && qBlue(p) > 50);
    }

    void gradeEffectInProjection()
    {
        ProjectData data = baseProject();

        // White clip (255, 255, 255)
        Clip clip;
        clip.id = ClipId::create();
        clip.start = frames(0);
        clip.duration = frames(30);
        clip.payload = ColorClipData{Param(Color{255, 255, 255, 255})};

        // Grade effect: balance.r = 0.2
        Effect gradeEffect;
        gradeEffect.id = EffectId::create();
        gradeEffect.type = u"vedit.grade"_s;
        gradeEffect.params[u"balance.r"_s] = Param(0.2);
        clip.effects.push_back(gradeEffect);

        data.sequences.front().visualTracks.front().clips.push_back(clip);

        const QImage img = renderFresh1(data, 5);
        const QRgb p = pixel(img, 160, 90);
        // Red channel attenuated (~51), green and blue remain 255
        QVERIFY(qRed(p) < 100);
        QVERIFY(qGreen(p) > 200);
        QVERIFY(qBlue(p) > 200);
    }

    void cubeLutInProjection()
    {
        const QString lutPath = m_dir.filePath(u"test.cube"_s);
        QFile file(lutPath);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
        const QByteArray cubeContent =
            "TITLE \"Test LUT\"\n"
            "LUT_3D_SIZE 2\n"
            "0.0 0.0 0.0\n"
            "0.0 0.0 0.0\n"
            "0.0 0.0 0.0\n"
            "0.0 0.0 0.0\n"
            "0.0 0.0 0.0\n"
            "0.0 0.0 0.0\n"
            "0.0 0.0 0.0\n"
            "0.0 1.0 0.0\n";
        file.write(cubeContent);
        file.close();

        ProjectData data = baseProject();

        Clip clip;
        clip.id = ClipId::create();
        clip.start = frames(0);
        clip.duration = frames(30);
        clip.payload = ColorClipData{Param(Color{255, 255, 255, 255})};

        Effect lutEffect;
        lutEffect.id = EffectId::create();
        lutEffect.type = u"vedit.lut"_s;
        lutEffect.params[u"path"_s] = Param(lutPath);
        clip.effects.push_back(lutEffect);

        data.sequences.front().visualTracks.front().clips.push_back(clip);

        const QImage img = renderFresh1(data, 5);
        const QRgb p = pixel(img, 160, 90);
        // White transformed to green by LUT
        QVERIFY(qRed(p) < 50);
        QVERIFY(qGreen(p) > 200);
        QVERIFY(qBlue(p) < 50);
    }

    void deflickerInProjection()
    {
        ProjectData data = baseProject();

        Clip clip;
        clip.id = ClipId::create();
        clip.start = frames(0);
        clip.duration = frames(30);
        clip.payload = ColorClipData{Param(Color{200, 200, 200, 255})};

        Effect deflickerEffect;
        deflickerEffect.id = EffectId::create();
        deflickerEffect.type = u"vedit.deflicker"_s;
        deflickerEffect.params[u"size"_s] = Param(5.0);
        deflickerEffect.params[u"mode"_s] = Param(u"pm"_s);
        clip.effects.push_back(deflickerEffect);

        data.sequences.front().visualTracks.front().clips.push_back(clip);

        const QImage img = renderFresh1(data, 5);
        const QRgb p = pixel(img, 160, 90);
        QVERIFY(qRed(p) > 180 && qGreen(p) > 180 && qBlue(p) > 180);
    }

    // Stickers (P5.4): a picture of the library at a third of the canvas, an emoji of the system font, a tint.
    void stickersInProjection()
    {
        Session session(baseProject());
        QVERIFY(session.apply(session.editor().insertMedia(m_landscape.id, frames(0))));
        const QImage plain = renderFresh1(session.data(), 10);
        StickerClipData star;
        star.source = AssetRef{u"vedit.core"_s, u"stickers/shapes/star"_s, 1};
        QVERIFY(session.apply(session.editor().insertSticker(frames(0), star, frames(30))));
        const Track &stickers = session.sequence().visualTracks.back();
        QCOMPARE(stickers.kind, TrackKind::Sticker);
        const ClipId starId = stickers.clips.front().id;
        const QImage withStar = renderFresh1(session.data(), 10);
        QVERIFY(pixel(withStar, 160, 90) != pixel(plain, 160, 90));
        QCOMPARE(pixel(withStar, 4, 4), pixel(plain, 4, 4));
        QCOMPARE(pixel(withStar, 315, 175), pixel(plain, 315, 175));

        // Recoloured blue: the centre turns blue.
        QVERIFY(session.apply(session.editor().updateClips({starId}, [](Clip &c) { c.sticker()->tint = Color{0, 0, 255, 255}; },
                                                           u"tint"_s)));
        const QRgb blue = renderFresh1(session.data(), 10).pixel(160, 90);
        QVERIFY2(qBlue(blue) > qRed(blue) + 40 && qBlue(blue) > qGreen(blue) + 40, qPrintable(QString::number(blue, 16)));

        // An emoji (colour emoji font of the system).
        StickerClipData fire;
        fire.emoji = u"🔥"_s;
        QVERIFY(session.apply(session.editor().insertSticker(frames(60), fire, frames(30))));
        const QImage plainLater = renderFresh1(ProjectData(baseProjectWithClip()), 70);
        const QImage withFire = renderFresh1(session.data(), 70);
        QVERIFY(pixel(withFire, 160, 90) != pixel(plainLater, 160, 90));
        QCOMPARE(pixel(withFire, 4, 4), pixel(plainLater, 4, 4));
    }

    // An audio visualizer draws the spectrum of the music under it: nearly nothing before the music starts.
    void visualizerFollowsTheMusic()
    {
        Session session(baseProject());
        QVERIFY(session.apply(session.editor().insertMedia(m_music.id, frames(60))));
        StickerClipData visualizer;
        visualizer.visualizer = AudioVisualizerSettings{};
        QVERIFY(session.apply(session.editor().insertSticker(frames(0), visualizer, frames(200))));
        const auto lit = [](const QImage &image) {
            int count = 0;
            for (int y = 0; y < image.height(); ++y) {
                for (int x = 0; x < image.width(); ++x) {
                    count += qGray(image.pixel(x, y)) > 40 ? 1 : 0;
                }
            }
            return count;
        };
        const int silent = lit(renderFresh1(session.data(), 30));
        const int playing = lit(renderFresh1(session.data(), 120));
        QVERIFY2(playing > silent + 200, qPrintable(u"%1 lit pixels in silence, %2 with music"_s.arg(silent).arg(playing)));
    }

    // Beat flash: the picture flashes on the beats of the music (its beat markers), not between them.
    void beatFlashOnTheBeats()
    {
        Session session(baseProject());
        QVERIFY(session.apply(session.editor().insertMedia(m_landscape.id, frames(0))));
        QVERIFY(session.apply(session.editor().insertMedia(m_music.id, frames(0))));
        const ClipId video = session.mainTrack().clips.front().id;
        const ClipId music = session.sequence().audioTracks.front().clips.front().id;
        QVERIFY(session.apply(session.editor().setBeatMarkers(music, {1.0, 2.0, 99.0})));
        QCOMPARE(session.data().findClip(music)->markers.size(), size_t(2)); // 99 s is beyond the song
        const QImage onBeatPlain = renderFresh1(session.data(), 30);
        const QImage betweenPlain = renderFresh1(session.data(), 45);
        QVERIFY(session.apply(session.editor().updateClips({video}, [](Clip &c) {
            Effect flash;
            flash.id = EffectId::create();
            flash.type = u"vedit.beat.flash"_s;
            flash.params[u"amount"_s] = Param(1.0);
            c.effects.push_back(flash);
        }, u"flash"_s)));
        const QImage onBeat = renderFresh1(session.data(), 30);
        QVERIFY2(qGray(onBeat.pixel(60, 120)) > qGray(onBeatPlain.pixel(60, 120)) + 60,
                 qPrintable(u"%1 → %2"_s.arg(qGray(onBeatPlain.pixel(60, 120))).arg(qGray(onBeat.pixel(60, 120)))));
        QCOMPARE(rgbHash(renderFresh1(session.data(), 45)), rgbHash(betweenPlain));
    }

    // Video effects (P5.5): on a clip, with its strength; on a layer, only under it and only for its stretch.
    void videoEffectsInProjection()
    {
        Session session(baseProject());
        QVERIFY(session.apply(session.editor().insertMedia(m_landscape.id, frames(0))));
        const ClipId clip = session.mainTrack().clips.front().id;
        const QImage plain10 = renderFresh1(session.data(), 10);
        const QImage plain100 = renderFresh1(session.data(), 100);
        const auto effect = [](const QString &id, double mix) {
            Effect e;
            e.id = EffectId::create();
            e.type = u"vedit.effect"_s;
            e.preset = AssetRef{u"vedit.core"_s, id, 1};
            e.intensity = Param(mix);
            return e;
        };
        QVERIFY(session.apply(session.editor().updateClips({clip}, [&](Clip &c) { c.effects.push_back(effect(u"effects/invert"_s, 0.0)); },
                                                           u"invert"_s)));
        QCOMPARE(rgbHash(renderFresh1(session.data(), 10)), rgbHash(plain10)); // strength 0: nothing
        QVERIFY(session.apply(session.editor().updateClips({clip}, [](Clip &c) { c.effects.back().intensity = Param(1.0); }, u"x"_s)));
        const QRgb inverted = renderFresh1(session.data(), 10).pixel(40, 40);
        const QRgb original = plain10.pixel(40, 40);
        QVERIFY2(std::abs(qRed(inverted) - (255 - qRed(original))) <= 2, qPrintable(u"%1 vs %2"_s.arg(qRed(inverted)).arg(qRed(original))));

        // A layer with the effect from frame 60 to 90: frame 10 as before, frame 100 untouched, frame 70 changed.
        QVERIFY(session.apply(session.editor().updateClips({clip}, [](Clip &c) { c.effects.clear(); }, u"x"_s)));
        QVERIFY(session.apply(session.editor().insertAdjustment(frames(60), frames(30))));
        ClipId layer;
        for (const Track &track : session.sequence().visualTracks) {
            for (const Clip &c : track.clips) {
                if (c.adjustment() || track.kind == TrackKind::Adjustment) {
                    layer = c.id;
                }
            }
        }
        QVERIFY(!layer.isNull());
        QVERIFY(session.apply(session.editor().updateClips({layer}, [&](Clip &c) { c.effects.push_back(effect(u"effects/pixelate-big"_s, 1.0)); },
                                                           u"pixelate"_s)));
        QCOMPARE(rgbHash(renderFresh1(session.data(), 10)), rgbHash(plain10));
        QCOMPARE(rgbHash(renderFresh1(session.data(), 100)), rgbHash(plain100));
        const QImage pixelated = renderFresh1(session.data(), 70);
        // Pixelated: neighbours inside a block are equal.
        int equal = 0;
        for (int x = 0; x < 300; x += 3) {
            equal += pixel(pixelated, x, 90) == pixel(pixelated, x + 1, 90) ? 1 : 0;
        }
        QVERIFY2(equal > 80, qPrintable(QString::number(equal)));
    }

    // Graphic elements (P5.6): a counter shows other digits at the end than at the start; a hand-drawn arrow is not there
    // at its first frame and is there once drawn.
    void graphicElementsInProjection()
    {
        Session session(baseProject());
        QVERIFY(session.apply(session.editor().insertMedia(m_landscape.id, frames(0))));
        StickerClipData counter;
        counter.graphic = GraphicSettings{};
        counter.graphic->to = 999;
        QVERIFY(session.apply(session.editor().insertSticker(frames(0), counter, frames(60))));
        StickerClipData arrow;
        arrow.graphic = GraphicSettings{};
        arrow.graphic->kind = GraphicKind::Arrow;
        arrow.graphic->color = Color{255, 0, 0, 255};
        QVERIFY(session.apply(session.editor().insertSticker(frames(60), arrow, frames(60))));
        QVERIFY(rgbHash(renderFresh1(session.data(), 1)) != rgbHash(renderFresh1(session.data(), 59)));
        const QImage plain = renderFresh1(baseProjectWithClip(), 60);
        QCOMPARE(rgbHash(renderFresh1(session.data(), 60)), rgbHash(plain)); // nothing drawn yet
        const QImage drawn = renderFresh1(session.data(), 100);
        int red = 0;
        for (int y = 0; y < drawn.height(); ++y) {
            for (int x = 0; x < drawn.width(); ++x) {
                const QRgb p = drawn.pixel(x, y);
                red += qRed(p) > 220 && qGreen(p) < 40 && qBlue(p) < 40 ? 1 : 0;
            }
        }
        QVERIFY2(red > 100, qPrintable(QString::number(red)));
    }

    void cleanupTestCase() { MltRuntime::shutdown(); }
};

QTEST_MAIN(TestProjection) // text is drawn with QPainter: needs a QGuiApplication (offscreen)
#include "tst_projection.moc"
