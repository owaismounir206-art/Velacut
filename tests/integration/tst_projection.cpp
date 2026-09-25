// SPDX-License-Identifier: GPL-3.0-or-later
// Engine integration test: the MLT projection of a sequence renders what the model says, and incremental
// updates give exactly the same frames as a full rebuild (docs/ARCHITECTURE.md §5.1).
#include "../unit/ProjectFixture.h"
#include "TestMedia.h"

#include "engine/mlt/MltRuntime.h"
#include "engine/timeline/ClipPlacement.h"
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
        projection.setPreview(session.data(), TimelineProjection::Preview{previewed, std::nullopt, std::nullopt});
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

    void cleanupTestCase() { MltRuntime::shutdown(); }
};

QTEST_MAIN(TestProjection) // text is drawn with QPainter: needs a QGuiApplication (offscreen)
#include "tst_projection.moc"
