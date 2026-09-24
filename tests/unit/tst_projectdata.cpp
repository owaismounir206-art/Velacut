// SPDX-License-Identifier: GPL-3.0-or-later
#include "ProjectFixture.h"

using namespace vedit;
using namespace vedit::test;
using namespace Qt::StringLiterals;

namespace {

Clip mediaClip(const MediaId &media, std::int64_t start, std::int64_t duration)
{
    Clip clip;
    clip.id = ClipId::create();
    clip.start = frames(start);
    clip.duration = frames(duration);
    MediaClipData data;
    data.mediaId = media;
    data.sourceIn = frames(0);
    clip.payload = data;
    return clip;
}

bool hasError(const ProjectData &data, const QString &fragment)
{
    const QStringList errors = data.checkInvariants();
    for (const QString &error : errors) {
        if (error.contains(fragment)) {
            return true;
        }
    }
    qWarning("expected an error containing '%s', got: %s", qPrintable(fragment), qPrintable(errors.join(u" | "_s)));
    return false;
}

Track &mainTrack(ProjectData &data)
{
    return data.sequences.front().visualTracks.front();
}

} // namespace

class TestProjectData : public QObject
{
    Q_OBJECT

private slots:
    void emptyProjectIsValid()
    {
        const ProjectData data = ProjectData::createEmpty(u"Empty"_s);
        QVERIFY(data.checkInvariants().isEmpty());
        QVERIFY(data.mainSequence() != nullptr);
        QCOMPARE(data.mainSequence()->visualTracks.size(), size_t(1));
        QCOMPARE(data.mainSequence()->visualTracks.front().kind, TrackKind::Video);
    }

    void validTimeline()
    {
        Fixture f;
        mainTrack(f.data).clips = {mediaClip(f.video10s, 0, 100), mediaClip(f.video10s, 100, 50)};
        QVERIFY(Session::checkInvariants(f.data));
    }

    void gapOnMagneticMainTrack()
    {
        Fixture f;
        mainTrack(f.data).clips = {mediaClip(f.video10s, 0, 100), mediaClip(f.video10s, 120, 50)};
        QVERIFY(hasError(f.data, u"gap on the magnetic main track"_s));
        f.data.sequences.front().magneticMain = false;
        QVERIFY(Session::checkInvariants(f.data));
    }

    void magneticMainStartsAtZero()
    {
        Fixture f;
        mainTrack(f.data).clips = {mediaClip(f.video10s, 10, 100)};
        QVERIFY(hasError(f.data, u"must start at 0"_s));
    }

    void overlapWithoutTransition()
    {
        Fixture f;
        f.data.sequences.front().magneticMain = false;
        mainTrack(f.data).clips = {mediaClip(f.video10s, 0, 100), mediaClip(f.video10s, 90, 50)};
        QVERIFY(hasError(f.data, u"overlapping clips"_s));
    }

    void overlapTransitionMustMatch()
    {
        Fixture f;
        Track &track = mainTrack(f.data);
        track.clips = {mediaClip(f.video10s, 0, 100), mediaClip(f.video10s, 90, 50)};
        Transition transition;
        transition.id = TransitionId::create();
        transition.type = {u"vedit.core"_s, u"transitions/dissolve"_s, 1};
        transition.from = track.clips[0].id;
        transition.to = track.clips[1].id;
        transition.duration = frames(10);
        transition.alignment = TransitionAlignment::Overlap;
        track.transitions = {transition};
        QVERIFY(Session::checkInvariants(f.data));
        track.transitions[0].duration = frames(5);
        QVERIFY(hasError(f.data, u"overlap transition does not match"_s));
    }

    void transitionMustConnectAdjacentClips()
    {
        Fixture f;
        Track &track = mainTrack(f.data);
        track.clips = {mediaClip(f.video10s, 0, 100), mediaClip(f.video10s, 100, 50), mediaClip(f.video10s, 150, 50)};
        Transition transition;
        transition.id = TransitionId::create();
        transition.from = track.clips[0].id;
        transition.to = track.clips[2].id;
        transition.duration = frames(10);
        track.transitions = {transition};
        QVERIFY(hasError(f.data, u"two adjacent clips"_s));
    }

    void timesMustBeOnTheProjectGrid()
    {
        Fixture f;
        Clip clip = mediaClip(f.video10s, 0, 100);
        clip.duration = RationalTime(4, Rational(1)); // 4 s, but at rate 1 instead of 30
        mainTrack(f.data).clips = {clip};
        QVERIFY(hasError(f.data, u"not on the project frame grid"_s));
    }

    void referencesMustExist()
    {
        Fixture f;
        mainTrack(f.data).clips = {mediaClip(MediaId::create(), 0, 100)};
        QVERIFY(hasError(f.data, u"references missing media"_s));
    }

    void duplicateIds()
    {
        Fixture f;
        Clip clip = mediaClip(f.video10s, 0, 100);
        Clip copy = mediaClip(f.video10s, 100, 100);
        copy.id = clip.id;
        mainTrack(f.data).clips = {clip, copy};
        QVERIFY(hasError(f.data, u"clip id is not unique"_s));
    }

    void clipKindMustMatchTrack()
    {
        Fixture f;
        Clip audioClip = mediaClip(f.music20s, 0, 100);
        audioClip.media()->streams = Streams::AudioOnly;
        mainTrack(f.data).clips = {audioClip};
        QVERIFY(hasError(f.data, u"not allowed on this track kind"_s));
    }

    void mainTrackMustExist()
    {
        Fixture f;
        f.data.sequences.front().visualTracks.clear();
        QVERIFY(hasError(f.data, u"main track"_s));
    }

    void compoundCycleIsDetected()
    {
        Fixture f;
        Sequence nested;
        nested.id = SequenceId::create();
        Track nestedMain;
        nestedMain.id = TrackId::create();
        nested.visualTracks.push_back(nestedMain);
        Clip toMain;
        toMain.id = ClipId::create();
        toMain.start = frames(0);
        toMain.duration = frames(10);
        toMain.payload = CompoundClipData{f.data.mainSequenceId, frames(0)};
        nested.visualTracks[0].clips.push_back(toMain);
        Clip toNested = toMain;
        toNested.id = ClipId::create();
        toNested.payload = CompoundClipData{nested.id, frames(0)};
        mainTrack(f.data).clips = {toNested};
        f.data.sequences.push_back(nested);
        QVERIFY(hasError(f.data, u"cycle"_s));
    }

    void lookups()
    {
        Fixture f;
        mainTrack(f.data).clips = {mediaClip(f.video10s, 0, 100)};
        const ClipId id = mainTrack(f.data).clips[0].id;
        const ClipLocation location = f.data.locateClip(id);
        QVERIFY(location.isValid());
        QCOMPARE(location.trackIndex, 0);
        QVERIFY(!location.audioTrack);
        QCOMPARE(f.data.findClip(id)->id, id);
        QVERIFY(f.data.findClip(ClipId::create()) == nullptr);
        QCOMPARE(f.data.findMedia(f.video10s)->name, u"video.mp4"_s);
        QCOMPARE(f.data.findTrack(mainTrack(f.data).id)->kind, TrackKind::Video);
    }
};

QTEST_GUILESS_MAIN(TestProjectData)
#include "tst_projectdata.moc"
