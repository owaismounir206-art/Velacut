// SPDX-License-Identifier: GPL-3.0-or-later
#include "ProjectFixture.h"

#include <QSignalSpy>

using namespace vedit;
using namespace vedit::test;

class TestTimelineEditor : public QObject
{
    Q_OBJECT

private:
    // Main track with three clips: A (0-300), B (300-420, mute video), C (420-510, photo 3 s).
    static std::unique_ptr<Session> threeClips(ClipId *a = nullptr, ClipId *b = nullptr, ClipId *c = nullptr)
    {
        Fixture fixture;
        auto owner = std::make_unique<Session>(fixture.data);
        Session &session = *owner;
        const auto add = [&session](const MediaId &media, std::int64_t at) {
            EditResult result = session.editor().insertMedia(media, frames(at));
            const ClipId id = result.primaryClip;
            if (!session.apply(std::move(result))) {
                qFatal("setup failed");
            }
            return id;
        };
        const ClipId idA = add(fixture.video10s, 0);
        const ClipId idB = add(fixture.video4sMute, 10000);
        const ClipId idC = add(fixture.photo, 10000);
        if (a) {
            *a = idA;
        }
        if (b) {
            *b = idB;
        }
        if (c) {
            *c = idC;
        }
        return owner;
    }

    static std::vector<ClipId> order(const Track &track)
    {
        std::vector<ClipId> ids;
        for (const Clip &clip : track.clips) {
            ids.push_back(clip.id);
        }
        return ids;
    }

private slots:
    void insertOnMagneticMainTrack()
    {
        ClipId a, b, c;
        auto owner = threeClips(&a, &b, &c);
        Session &session = *owner;
        const Track &main = session.mainTrack();
        QCOMPARE(main.clips.size(), size_t(3));
        QCOMPARE(order(main), (std::vector<ClipId>{a, b, c}));
        QCOMPARE(main.clips[0].start, frames(0));
        QCOMPARE(main.clips[1].start, frames(300));
        QCOMPARE(main.clips[2].start, frames(420));
        // Photos last 3 s by default.
        QCOMPARE(main.clips[2].duration, frames(90));
        // Video with audio keeps its audio in the same clip; mute video is video only.
        QCOMPARE(main.clips[0].media()->streams, Streams::AudioVideo);
        QCOMPARE(main.clips[1].media()->streams, Streams::VideoOnly);
    }

    void insertInFirstHalfGoesBefore()
    {
        ClipId a, b, c;
        auto owner = threeClips(&a, &b, &c);
        Session &session = *owner;
        // Frame 100 is in the first half of A: the new clip goes before A, and everything ripples.
        EditResult result = session.editor().insertMedia(session.data().media[1].id, frames(100));
        const ClipId inserted = result.primaryClip;
        QVERIFY(session.apply(std::move(result)));
        QCOMPARE(order(session.mainTrack()), (std::vector<ClipId>{inserted, a, b, c}));
        QCOMPARE(session.mainTrack().clips[1].start, frames(120));
    }

    void audioGoesToAnAudioTrack()
    {
        auto owner = threeClips();
        Session &session = *owner;
        const MediaId music = session.data().media[2].id;
        EditResult first = session.editor().insertMedia(music, frames(0));
        QVERIFY(session.apply(std::move(first)));
        QCOMPARE(session.sequence().audioTracks.size(), size_t(1));
        // 20 s of 48 kHz audio = 600 frames at 30 fps.
        QCOMPARE(session.sequence().audioTracks[0].clips[0].duration, frames(600));
        // A second music clip overlapping the first creates a second audio track automatically.
        EditResult second = session.editor().insertMedia(music, frames(100));
        QVERIFY(session.apply(std::move(second)));
        QCOMPARE(session.sequence().audioTracks.size(), size_t(2));
    }

    void overlayPlacementCreatesTrackAbove()
    {
        auto owner = threeClips();
        Session &session = *owner;
        const MediaId mute = session.data().media[1].id;
        QVERIFY(session.apply(session.editor().insertMedia(mute, frames(50), std::nullopt, Placement::Overlay)));
        QCOMPARE(session.sequence().visualTracks.size(), size_t(2));
        QCOMPARE(session.sequence().visualTracks[1].clips[0].start, frames(50));
        // Overlapping overlay: never overwrites, a new track appears.
        QVERIFY(session.apply(session.editor().insertMedia(mute, frames(60), std::nullopt, Placement::Overlay)));
        QCOMPARE(session.sequence().visualTracks.size(), size_t(3));
        // Non-overlapping overlay reuses the first free overlay track.
        QVERIFY(session.apply(session.editor().insertMedia(mute, frames(1000), std::nullopt, Placement::Overlay)));
        QCOMPARE(session.sequence().visualTracks.size(), size_t(3));
        QCOMPARE(session.sequence().visualTracks[1].clips.size(), size_t(2));
    }

    void moveReordersMainTrack()
    {
        ClipId a, b, c;
        auto owner = threeClips(&a, &b, &c);
        Session &session = *owner;
        // Drag C to the very beginning.
        QVERIFY(session.apply(session.editor().moveClip(c, frames(0))));
        QCOMPARE(order(session.mainTrack()), (std::vector<ClipId>{c, a, b}));
        QCOMPARE(session.mainTrack().clips[1].start, frames(90));
        QCOMPARE(session.sequence().duration(kRate), frames(510));
    }

    void moveToOverlayAndBack()
    {
        ClipId a, b, c;
        auto owner = threeClips(&a, &b, &c);
        Session &session = *owner;
        // Move B off the main track onto an overlay: the main track closes the gap.
        QVERIFY(session.apply(session.editor().insertMedia(session.data().media[1].id, frames(2000),
                                                           std::nullopt, Placement::Overlay)));
        const TrackId overlay = session.sequence().visualTracks[1].id;
        QVERIFY(session.apply(session.editor().moveClip(b, frames(40), overlay)));
        QCOMPARE(order(session.mainTrack()), (std::vector<ClipId>{a, c}));
        QCOMPARE(session.mainTrack().clips[1].start, frames(300));
        QCOMPARE(session.data().findClip(b)->start, frames(40));
        // Move it back onto the main track, after A.
        QVERIFY(session.apply(session.editor().moveClip(b, frames(299), session.mainTrack().id)));
        QCOMPARE(order(session.mainTrack()), (std::vector<ClipId>{a, b, c}));
    }

    void emptyTracksDisappear()
    {
        ClipId a, b, c;
        auto owner = threeClips(&a, &b, &c);
        Session &session = *owner;
        QVERIFY(session.apply(session.editor().insertMedia(session.data().media[1].id, frames(0), std::nullopt,
                                                           Placement::Overlay)));
        const ClipId overlayClip = session.sequence().visualTracks[1].clips[0].id;
        QCOMPARE(session.sequence().visualTracks.size(), size_t(2));
        // Moving the only overlay clip to the main track removes the overlay track.
        QVERIFY(session.apply(session.editor().moveClip(overlayClip, frames(0), session.mainTrack().id)));
        QCOMPARE(session.sequence().visualTracks.size(), size_t(1));
        // Deleting everything keeps the (empty) main track.
        QVERIFY(session.apply(session.editor().deleteClips(order(session.mainTrack()))));
        QCOMPARE(session.sequence().visualTracks.size(), size_t(1));
        QVERIFY(session.mainTrack().clips.empty());
    }

    void overlayMoveCollisionCreatesTrackAbove()
    {
        auto owner = threeClips();
        Session &session = *owner;
        const MediaId mute = session.data().media[1].id;
        QVERIFY(session.apply(session.editor().insertMedia(mute, frames(0), std::nullopt, Placement::Overlay)));
        QVERIFY(session.apply(session.editor().insertMedia(mute, frames(500), std::nullopt, Placement::Overlay)));
        const Track &overlay = session.sequence().visualTracks[1];
        QCOMPARE(overlay.clips.size(), size_t(2));
        const ClipId second = overlay.clips[1].id;
        // Dropping the second clip on top of the first (same track) must not overwrite it.
        QVERIFY(session.apply(session.editor().moveClip(second, frames(30))));
        QCOMPARE(session.sequence().visualTracks.size(), size_t(3));
        QCOMPARE(session.sequence().visualTracks[2].clips[0].id, second);
    }

    void trimEndRipplesOnMainTrack()
    {
        ClipId a, b, c;
        auto owner = threeClips(&a, &b, &c);
        Session &session = *owner;
        QVERIFY(session.apply(session.editor().trimClip(a, ClipEdge::End, frames(200))));
        QCOMPARE(session.data().findClip(a)->duration, frames(200));
        QCOMPARE(session.data().findClip(b)->start, frames(200));
        // Cannot extend beyond the media (300 frames).
        QVERIFY(session.apply(session.editor().trimClip(a, ClipEdge::End, frames(1000))));
        QCOMPARE(session.data().findClip(a)->duration, frames(300));
    }

    void trimStartMovesSourceIn()
    {
        ClipId a, b, c;
        auto owner = threeClips(&a, &b, &c);
        Session &session = *owner;
        QVERIFY(session.apply(session.editor().trimClip(a, ClipEdge::Start, frames(60))));
        const Clip *clip = session.data().findClip(a);
        QCOMPARE(clip->start, frames(0)); // magnetic: stays at 0
        QCOMPARE(clip->duration, frames(240));
        QCOMPARE(clip->media()->sourceIn, frames(60));
        QCOMPARE(session.data().findClip(b)->start, frames(240));
        // Extending back to the left is limited by the available material (60 frames).
        QVERIFY(session.apply(session.editor().trimClip(a, ClipEdge::Start, frames(-500))));
        QCOMPARE(session.data().findClip(a)->media()->sourceIn, frames(0));
        QCOMPARE(session.data().findClip(a)->duration, frames(300));
    }

    void trimOffMainIsBoundedByNeighbours()
    {
        auto owner = threeClips();
        Session &session = *owner;
        const MediaId mute = session.data().media[1].id;
        QVERIFY(session.apply(session.editor().insertMedia(mute, frames(0), std::nullopt, Placement::Overlay)));
        QVERIFY(session.apply(session.editor().insertMedia(mute, frames(200), std::nullopt, Placement::Overlay)));
        const Track &overlay = session.sequence().visualTracks[1];
        QCOMPARE(overlay.clips.size(), size_t(2));
        const ClipId first = overlay.clips[0].id;
        const ClipId second = overlay.clips[1].id;
        // Trim the first clip to 60 frames, then try to extend it over the second one.
        QVERIFY(session.apply(session.editor().trimClip(first, ClipEdge::End, frames(60))));
        QVERIFY(session.apply(session.editor().trimClip(second, ClipEdge::Start, frames(250))));
        QVERIFY(session.apply(session.editor().trimClip(second, ClipEdge::Start, frames(0))));
        QCOMPARE(session.data().findClip(second)->start, frames(200)); // limited by the material (sourceIn 0)
        QVERIFY(session.apply(session.editor().trimClip(first, ClipEdge::End, frames(500))));
        QCOMPARE(session.data().findClip(first)->end(), frames(120)); // limited by its media (120 frames)
    }

    void splitKeepsContentContinuous()
    {
        ClipId a, b, c;
        auto owner = threeClips(&a, &b, &c);
        Session &session = *owner;
        EditResult result = session.editor().splitClip(a, frames(100));
        const ClipId second = result.primaryClip;
        QVERIFY(session.apply(std::move(result)));
        QCOMPARE(order(session.mainTrack()), (std::vector<ClipId>{a, second, b, c}));
        const Clip *left = session.data().findClip(a);
        const Clip *right = session.data().findClip(second);
        QCOMPARE(left->duration, frames(100));
        QCOMPARE(right->start, frames(100));
        QCOMPARE(right->duration, frames(200));
        QCOMPARE(right->media()->sourceIn, frames(100));
        // Timeline length unchanged.
        QCOMPARE(session.sequence().duration(kRate), frames(510));
        // Splitting outside the clip is refused with a message.
        QVERIFY(!session.editor().splitClip(a, frames(100)).ok());
    }

    void splitRespectsSpeed()
    {
        Fixture fixture;
        Session session(fixture.data);
        EditResult insert = session.editor().insertMedia(fixture.video10s, frames(0));
        const ClipId a = insert.primaryClip;
        QVERIFY(session.apply(std::move(insert)));
        Clip faster = *session.data().findClip(a);
        faster.media()->speed = 2.0;
        faster.duration = frames(150);
        EditScript script;
        script.push_back(edits::replaceClip(*session.data().findClip(a), faster));
        // Use the command directly (speed editing is a later-phase feature).
        EditResult change;
        change.script = std::move(script);
        change.text = QStringLiteral("speed");
        QVERIFY(session.apply(std::move(change)));
        EditResult split = session.editor().splitClip(a, frames(50));
        const ClipId second = split.primaryClip;
        QVERIFY(session.apply(std::move(split)));
        // 50 timeline frames at 2x consume 100 source frames.
        QCOMPARE(session.data().findClip(second)->media()->sourceIn, frames(100));
    }

    void deleteRipplesMainTrackOnly()
    {
        ClipId a, b, c;
        auto owner = threeClips(&a, &b, &c);
        Session &session = *owner;
        QVERIFY(session.apply(session.editor().deleteClips({b})));
        QCOMPARE(order(session.mainTrack()), (std::vector<ClipId>{a, c}));
        QCOMPARE(session.data().findClip(c)->start, frames(300));
    }

    void duplicatePlacesACopyRightAfter()
    {
        ClipId a, b, c;
        auto owner = threeClips(&a, &b, &c);
        Session &session = *owner;
        EditResult duplicate = session.editor().duplicateClips({b});
        const ClipId copy = duplicate.primaryClip;
        QVERIFY(session.apply(std::move(duplicate)));
        // Main track: the copy follows the original and the rest moves along.
        QCOMPARE(order(session.mainTrack()), (std::vector<ClipId>{a, b, copy, c}));
        QCOMPARE(session.data().findClip(copy)->start, frames(420));
        QCOMPARE(session.data().findClip(copy)->duration, frames(120));
        QCOMPARE(session.data().findClip(c)->start, frames(540));
        QVERIFY(copy != b);

        // Off the main track, with no room after the clip: a new track above.
        QVERIFY(session.apply(session.editor().insertMedia(session.data().media[0].id, frames(0), std::nullopt, Placement::Overlay)));
        const ClipId overlay = session.sequence().visualTracks[1].clips.front().id;
        QVERIFY(session.apply(session.editor().insertMedia(session.data().media[1].id, frames(300), std::nullopt, Placement::Overlay)));
        QCOMPARE(session.sequence().visualTracks.size(), size_t(2)); // room after the first overlay clip
        QVERIFY(session.apply(session.editor().duplicateClips({overlay})));
        QCOMPARE(session.sequence().visualTracks.size(), size_t(3));
        QCOMPARE(session.sequence().visualTracks[2].clips.front().start, frames(300));
    }

    void moveToANewTrack()
    {
        ClipId a, b, c;
        auto owner = threeClips(&a, &b, &c);
        Session &session = *owner;
        // Dragging B from the main track into the free space above it.
        QVERIFY(session.apply(session.editor().moveClipToNewTrack(b, frames(50), 1)));
        QCOMPARE(session.sequence().visualTracks.size(), size_t(2));
        QCOMPARE(session.sequence().visualTracks[1].clips.front().id, b);
        QCOMPARE(session.data().findClip(b)->start, frames(50));
        QCOMPARE(order(session.mainTrack()), (std::vector<ClipId>{a, c})); // rippled
        // Index 0 would be below the main track: clamped above it.
        QVERIFY(session.apply(session.editor().moveClipToNewTrack(c, frames(0), 0)));
        QCOMPARE(session.sequence().visualTracks[1].clips.front().id, c);
        // The track left empty disappears.
        QVERIFY(session.apply(session.editor().moveClipToNewTrack(b, frames(0), 3)));
        QCOMPARE(session.sequence().visualTracks.size(), size_t(3));
    }

    // ---- Phase 2 ------------------------------------------------------------------------------------------------
    void textGoesOnTextTracks()
    {
        auto owner = threeClips();
        Session &session = *owner;
        TextClipData text;
        text.text = QStringLiteral("Hello");
        EditResult first = session.editor().insertText(frames(10), text, frames(90));
        const ClipId firstId = first.primaryClip;
        QVERIFY(session.apply(std::move(first)));
        QCOMPARE(session.sequence().visualTracks.size(), size_t(2));
        QCOMPARE(session.sequence().visualTracks[1].kind, TrackKind::Text);
        QCOMPARE(session.data().findClip(firstId)->text()->text, QStringLiteral("Hello"));
        // Overlapping in time: a second text track.
        QVERIFY(session.apply(session.editor().insertText(frames(50), text, frames(0)))); // default duration
        QCOMPARE(session.sequence().visualTracks.size(), size_t(3));
        QCOMPARE(session.sequence().visualTracks[2].clips.front().duration, frames(90)); // 3 s
    }

    void attributesChangeWithoutMovingClips()
    {
        ClipId a, b, c;
        auto owner = threeClips(&a, &b, &c);
        Session &session = *owner;
        EditResult change = session.editor().updateClips(
            {a, b},
            [](Clip &clip) {
                clip.opacity = Param(0.5);
                clip.start = frames(999); // not an attribute: ignored
                Effect effect;
                effect.id = EffectId::create();
                effect.type = QStringLiteral("vedit.adjust.basic");
                effect.params[QStringLiteral("contrast")] = Param(0.3);
                clip.effects.push_back(effect);
            },
            QStringLiteral("Apply"));
        QVERIFY(session.apply(std::move(change)));
        QCOMPARE(session.data().findClip(a)->opacity, Param(0.5));
        QCOMPARE(session.data().findClip(b)->effects.size(), size_t(1));
        QCOMPARE(session.data().findClip(b)->start, frames(300));
        QCOMPARE(session.stack.count(), 4); // one undo step for both clips
    }

    void speedChangesDurationAndRipples()
    {
        ClipId a, b, c;
        auto owner = threeClips(&a, &b, &c);
        Session &session = *owner;
        QVERIFY(session.apply(session.editor().setSpeed(a, 2.0)));
        QCOMPARE(session.data().findClip(a)->duration, frames(150));
        QCOMPARE(session.data().findClip(a)->media()->speed, 2.0);
        QCOMPARE(session.data().findClip(b)->start, frames(150));
        QVERIFY(session.apply(session.editor().setSpeed(a, 0.5))); // back to the same material at half speed
        QCOMPARE(session.data().findClip(a)->duration, frames(600));
        QVERIFY(!session.editor().setSpeed(c, 2.0).ok()); // a photo
    }

    void freezeFrameInsertsAStill()
    {
        ClipId a, b, c;
        auto owner = threeClips(&a, &b, &c);
        Session &session = *owner;
        const MediaId still = session.data().media[3].id; // the photo of the fixture stands for the frame
        EditResult freeze = session.editor().insertFreezeFrame(a, frames(100), still, frames(90));
        const ClipId stillClip = freeze.primaryClip;
        QVERIFY(session.apply(std::move(freeze)));
        const Track &main = session.mainTrack();
        QCOMPARE(main.clips.size(), size_t(5));
        QCOMPARE(main.clips[0].duration, frames(100));
        QCOMPARE(main.clips[1].id, stillClip);
        QCOMPARE(main.clips[1].duration, frames(90));
        QCOMPARE(main.clips[2].start, frames(190));
        QCOMPARE(main.clips[2].media()->sourceIn, frames(100)); // the rest of the clip continues after the still
        QCOMPARE(session.data().findClip(b)->start, frames(390));
        // At the start of a clip: the still goes before it.
        QVERIFY(session.apply(session.editor().insertFreezeFrame(b, frames(390), still, frames(30))));
        QCOMPARE(session.data().findClip(b)->start, frames(420));
    }

    void transitionsBetweenClips()
    {
        ClipId a, b, c;
        auto owner = threeClips(&a, &b, &c);
        Session &session = *owner;
        const AssetRef dissolve{QStringLiteral("vedit.core"), QStringLiteral("transitions/dissolve"), 1};
        QVERIFY(session.apply(session.editor().addTransition(a, dissolve, frames(15))));
        QCOMPARE(session.mainTrack().transitions.size(), size_t(1));
        QCOMPARE(session.mainTrack().transitions.front().to, b);
        // The duration is limited by the shorter clip (B: 120 frames).
        QVERIFY(session.apply(session.editor().addTransition(a, dissolve, frames(500))));
        QCOMPARE(session.mainTrack().transitions.size(), size_t(1)); // replaced, not added
        QCOMPARE(session.mainTrack().transitions.front().duration, frames(120));
        QVERIFY(!session.editor().addTransition(c, dissolve, frames(15)).ok()); // last clip: nothing after it
        const TransitionId id = session.mainTrack().transitions.front().id;
        QVERIFY(session.apply(session.editor().updateTransition(id, [](Transition &t) { t.duration = RationalTime(10, Rational(30)); })));
        QCOMPARE(session.mainTrack().transitions.front().duration, frames(10));
        // On every cut, then none.
        QVERIFY(session.apply(session.editor().applyTransitionToAll(session.mainTrack().id, dissolve, frames(20))));
        QCOMPARE(session.mainTrack().transitions.size(), size_t(2));
        QVERIFY(session.apply(session.editor().removeAllTransitions(session.mainTrack().id)));
        QVERIFY(session.mainTrack().transitions.empty());
        QVERIFY(session.apply(session.editor().addTransition(b, dissolve, frames(15))));
        QVERIFY(session.apply(session.editor().removeTransitions({session.mainTrack().transitions.front().id})));
        QVERIFY(session.mainTrack().transitions.empty());
    }

    void trackVolumeAndBackground()
    {
        auto owner = threeClips();
        Session &session = *owner;
        QVERIFY(session.apply(session.editor().updateTrack(session.mainTrack().id, [](Track &t) {
            t.gainDb = Param(-12.0);
            t.clips.clear(); // not a property: ignored
        }, QStringLiteral("Volume"))));
        QCOMPARE(session.mainTrack().gainDb, Param(-12.0));
        QCOMPARE(session.mainTrack().clips.size(), size_t(3));
        const CanvasBackground blur{BackgroundType::Blur, Color{}, 0.8, {}, std::nullopt};
        QVERIFY(session.apply(session.editor().setDefaultBackground(blur)));
        QCOMPARE(session.sequence().defaultBackground, std::optional<CanvasBackground>(blur));
    }

    void lockedTrackRefusesEdits()
    {
        ClipId a;
        auto owner = threeClips(&a);
        Session &session = *owner;
        Track locked = session.mainTrack();
        locked.locked = true;
        EditResult lock;
        lock.script.push_back(edits::setTrackProperties(session.mainTrack(), locked));
        lock.text = QStringLiteral("lock");
        QVERIFY(session.apply(std::move(lock)));
        QVERIFY(!session.editor().deleteClips({a}).ok());
        QVERIFY(!session.editor().splitClip(a, frames(10)).ok());
        QVERIFY(!session.editor().trimClip(a, ClipEdge::End, frames(10)).ok());
        QVERIFY(!session.editor().moveClip(a, frames(10)).ok());
    }

    void oneSignalPerCommand()
    {
        ClipId a;
        auto owner = threeClips(&a);
        Session &session = *owner;
        QSignalSpy spy(&session.project, &Project::changed);
        QVERIFY(session.apply(session.editor().deleteClips({a})));
        // push, undo, redo: exactly one notification each.
        QCOMPARE(spy.count(), 3);
        const ChangeSet changes = spy.at(0).at(0).value<ChangeSet>();
        QVERIFY(changes.clips.contains(a));
        QVERIFY(changes.tracks.contains(session.mainTrack().id));
    }

    void noOpProducesNoEdits()
    {
        ClipId a;
        auto owner = threeClips(&a);
        Session &session = *owner;
        const EditResult result = session.editor().trimClip(a, ClipEdge::End, frames(300));
        QVERIFY(result.ok());
        QVERIFY(result.script.empty());
    }

    void rippleShiftIsCompact()
    {
        // Deleting the first of many clips shifts all the others with a single small edit.
        Fixture fixture;
        Session session(fixture.data);
        for (int i = 0; i < 50; ++i) {
            QVERIFY(session.apply(session.editor().insertMedia(fixture.photo, frames(1000000))));
        }
        const EditResult result = session.editor().deleteClips({session.mainTrack().clips.front().id});
        QVERIFY(result.ok());
        QCOMPARE(result.script.size(), size_t(2)); // remove + one shift of 49 clips
    }

    void sliderDragMergesIntoOneStep()
    {
        ClipId a;
        auto owner = threeClips(&a);
        Session &session = *owner;
        const int before = session.stack.count();
        const ProjectData original = session.data();
        for (int step = 1; step <= 10; ++step) {
            Clip changed = *session.data().findClip(a);
            changed.opacity = Param(1.0 - step * 0.05);
            EditScript script;
            script.push_back(edits::replaceClip(*session.data().findClip(a), changed));
            session.stack.push(new EditCommand(session.project, QStringLiteral("Opacity"), std::move(script),
                                               MergeKey{QStringLiteral("opacity"), 42}));
        }
        QCOMPARE(session.stack.count(), before + 1);
        QCOMPARE(session.data().findClip(a)->opacity.numberAt(frames(0)), 0.5);
        session.stack.undo();
        QVERIFY(session.data() == original);
        // A new gesture is a new step.
        Clip changed = *session.data().findClip(a);
        changed.opacity = Param(0.9);
        EditScript script;
        script.push_back(edits::replaceClip(*session.data().findClip(a), changed));
        session.stack.push(new EditCommand(session.project, QStringLiteral("Opacity"), std::move(script),
                                           MergeKey{QStringLiteral("opacity"), 43}));
        QCOMPARE(session.stack.index(), before + 1);
    }
};

QTEST_GUILESS_MAIN(TestTimelineEditor)
#include "tst_timelineeditor.moc"
