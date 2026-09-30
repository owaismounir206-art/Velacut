// SPDX-License-Identifier: GPL-3.0-or-later
// Engine integration test: the MLT projection of a sequence renders what the model says, and incremental
// updates give exactly the same frames as a full rebuild (docs/ARCHITECTURE.md §5.1).
#include "../unit/ProjectFixture.h"
#include "TestMedia.h"

#include "engine/mlt/MltRuntime.h"
#include "engine/timeline/ClipPlacement.h"
#include "engine/timeline/MediaProducerCache.h"
#include "engine/analysis/AudioSync.h"
#include "engine/timeline/TimelineProjection.h"

#include <QCryptographicHash>
#include <QFile>
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

    void cleanupTestCase() { MltRuntime::shutdown(); }
};

QTEST_MAIN(TestProjection) // text is drawn with QPainter: needs a QGuiApplication (offscreen)
#include "tst_projection.moc"
