// SPDX-License-Identifier: GPL-3.0-or-later
#include "ProjectFixture.h"

#include "core/serialization/Migrations.h"
#include "core/serialization/ProjectFile.h"
#include "core/serialization/ProjectJson.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>

using namespace vedit;
using namespace vedit::test;
using namespace Qt::StringLiterals;

namespace {


// A project exercising every typed field of the format.
ProjectData richProject()
{
    Fixture fixture;
    Session session(fixture.data);
    const auto apply = [&session](EditResult result) {
        if (!session.apply(std::move(result))) {
            qFatal("setup failed");
        }
    };
    apply(session.editor().insertMedia(fixture.video10s, frames(0)));
    apply(session.editor().insertMedia(fixture.photo, frames(1000)));
    apply(session.editor().insertMedia(fixture.music20s, frames(0)));
    apply(session.editor().insertMedia(fixture.video4sMute, frames(30), std::nullopt, Placement::Overlay));
    ProjectData data = session.data();

    Sequence &sequence = data.sequences.front();
    Track &main = sequence.visualTracks.front();
    Clip &first = main.clips[0];
    first.name = u"Intro"_s;
    first.blendMode = BlendMode::Screen;
    first.transform.fit = FitMode::Cover;
    first.transform.flipH = true;
    Param position;
    position.setKeyframes({Keyframe{frames(0), Vec2{-0.25, 0.1}, Interpolation::Bezier, Easing::preset(Easing::Preset::EaseOutBack)},
                           Keyframe{frames(30), Vec2{0.0, 0.0}, Interpolation::Bezier, *Easing::cubicBezier(0.2, 0.0, 0.3, 1.4)},
                           Keyframe{frames(60), Vec2{0.1, 0.0}, Interpolation::Hold, {}}});
    first.transform.position = position;
    first.media()->audio.fadeIn = frames(10);
    first.media()->audio.gainDb = Param(-3.5);

    Effect effect;
    effect.id = EffectId::create();
    effect.type = u"vedit.adjust.basic"_s;
    effect.typeVersion = 2;
    effect.preset = AssetRef{u"vedit.core"_s, u"filters/warm"_s, 3};
    effect.params[u"exposure"_s] = Param(0.25);
    effect.params[u"tint"_s] = Param(Color{255, 128, 0, 255});
    effect.params[u"mode"_s] = Param(u"soft"_s);
    effect.params[u"enabled"_s] = Param(true);
    effect.params[u"curve"_s] = Param(QJsonValue(QJsonArray{0, 0.5, 1})); // unknown shape, preserved
    Param contrast;
    contrast.setKeyframes({Keyframe{frames(0), 0.0, Interpolation::Linear, {}}, Keyframe{frames(15), 1.0, Interpolation::Linear, {}}});
    effect.params[u"contrast"_s] = contrast;
    first.effects.push_back(effect);

    Marker marker;
    marker.id = MarkerId::create();
    marker.time = frames(12);
    marker.name = u"Drop"_s;
    marker.color = u"#FF0000FF"_s;
    marker.kind = MarkerKind::Beat;
    first.markers.push_back(marker);
    Marker chapter = marker;
    chapter.id = MarkerId::create();
    chapter.duration = frames(90);
    chapter.kind = MarkerKind::Chapter;
    sequence.markers.push_back(chapter);

    Transition transition;
    transition.id = TransitionId::create();
    transition.type = AssetRef{u"vedit.core"_s, u"transitions/dissolve"_s, 1};
    transition.from = main.clips[0].id;
    transition.to = main.clips[1].id;
    transition.duration = frames(15);
    transition.params[u"easing"_s] = Param(u"easeInOut"_s);
    main.transitions.push_back(transition);

    Group group;
    group.id = GroupId::create();
    group.clipIds = {main.clips[0].id, main.clips[1].id};
    sequence.groups.push_back(group);

    sequence.defaultBackground = CanvasBackground{BackgroundType::Blur, Color{0, 0, 0, 255}, 0.6, {}, std::nullopt};
    main.clips[1].background = CanvasBackground{BackgroundType::Color, Color{20, 40, 60, 255}, 0.6, {}, std::nullopt};
    main.gainDb = Param(-6.0);
    main.extras.insert(u"futureTrackField"_s, 42);
    first.extras.insert(u"masks"_s, QJsonArray{QJsonObject{{u"shape"_s, u"circle"_s}}});

    // A text clip on a text track, with the fields of later phases preserved verbatim.
    Track textTrack;
    textTrack.id = TrackId::create();
    textTrack.kind = TrackKind::Text;
    Clip text;
    text.id = ClipId::create();
    text.start = frames(0);
    text.duration = frames(45);
    TextClipData textData;
    textData.text = u"Ciao Roma!"_s;
    textData.style.fontFamily = u"Noto Serif"_s;
    textData.style.fontWeight = 400;
    textData.style.italic = true;
    textData.style.color = Param(Color{255, 213, 79, 255});
    textData.style.stroke = TextStroke{Param(Color{0, 0, 0, 255}), 0.1};
    textData.style.shadow = TextShadow{Color{0, 0, 0, 153}, Vec2{0.01, 0.02}, 0.05};
    textData.style.background = TextBackground{Color{0, 0, 0, 128}, 0.3, 0.5};
    textData.style.align = TextAlign::Left;
    textData.style.lineHeight = 1.4;
    textData.style.extras.insert(u"gradient"_s, QJsonObject{{u"type"_s, u"linear"_s}, {u"angle"_s, 90}});
    textData.stylePreset = AssetRef{u"vedit.core"_s, u"text/bold-outline"_s, 1};
    textData.boxWidth = 0.8;
    textData.fields.insert(u"spans"_s, QJsonArray{QJsonObject{{u"start"_s, 5}, {u"end"_s, 9}}});
    text.payload = textData;
    textTrack.clips.push_back(text);
    sequence.visualTracks.push_back(textTrack);

    // A compound clip referencing a nested sequence, and a color clip.
    Sequence nested;
    nested.id = SequenceId::create();
    nested.name = u"Nested"_s;
    Track nestedMain;
    nestedMain.id = TrackId::create();
    Clip color;
    color.id = ClipId::create();
    color.start = frames(0);
    color.duration = frames(30);
    color.payload = ColorClipData{Param(Color{10, 20, 30, 255})};
    nestedMain.clips.push_back(color);
    nested.visualTracks.push_back(nestedMain);
    Track overlay;
    overlay.id = TrackId::create();
    Clip compound;
    compound.id = ClipId::create();
    compound.start = frames(500);
    compound.duration = frames(30);
    compound.payload = CompoundClipData{nested.id, frames(0)};
    overlay.clips.push_back(compound);
    sequence.visualTracks.push_back(overlay);
    // Last: this push_back invalidates the `sequence` reference.
    data.sequences.push_back(nested);

    MediaFolder folder{FolderId::create(), u"Riprese"_s, FolderId()};
    data.mediaFolders.push_back(folder);
    data.media[0].folderId = folder.id;
    data.media[0].favorite = true;
    data.media[0].relativePath = u"../media/video.mp4"_s;
    data.media[0].info.video->rotation = 90;
    data.media[0].info.video->hdr = true;
    data.extras.insert(u"futureProjectField"_s, u"keep me"_s);
    return data;
}

} // namespace

class TestSerialization : public QObject
{
    Q_OBJECT

private slots:
    void richProjectIsValid() { QVERIFY(Session::checkInvariants(richProject())); }

    void roundTripIsLossless()
    {
        const ProjectData original = richProject();
        const QByteArray bytes = projectjson::toBytes(original);
        const ProjectLoadResult loaded = projectjson::fromBytes(bytes);
        QVERIFY2(loaded.ok(), qPrintable(loaded.error));
        QVERIFY2(loaded.warnings.isEmpty(), qPrintable(loaded.warnings.join(u'\n')));
        QVERIFY2(*loaded.project == original, qPrintable(firstDifference(*loaded.project, original)));
        // Canonical: writing the loaded project gives the same bytes.
        QCOMPARE(projectjson::toBytes(*loaded.project), bytes);
    }

    void canonicalLayout()
    {
        const QByteArray bytes = projectjson::toBytes(richProject());
        QVERIFY(bytes.endsWith("}\n"));
        QVERIFY(bytes.startsWith("{\n    \""));
        // Keys are sorted: "createdAt" comes before "format".
        QVERIFY(bytes.indexOf("\"createdAt\"") < bytes.indexOf("\"format\""));
        QVERIFY(!bytes.contains('\r'));
        // Times are exact strings, never floating point seconds.
        QVERIFY(bytes.contains("\"start\": \"0@30\""));
    }

    void unknownFieldsArePreserved()
    {
        QJsonObject json = projectjson::toJson(richProject());
        const ProjectLoadResult loaded = projectjson::fromJson(json);
        QVERIFY(loaded.ok());
        const QJsonObject rewritten = projectjson::toJson(*loaded.project);
        QCOMPARE(rewritten.value(u"futureProjectField"_s).toString(), u"keep me"_s);
        const QJsonObject sequence = rewritten.value(u"sequences"_s).toArray()[0].toObject();
        QCOMPARE(sequence.value(u"defaultBackground"_s).toObject().value(u"type"_s).toString(), u"blur"_s);
        const QJsonArray tracks = sequence.value(u"visualTracks"_s).toArray();
        QCOMPARE(tracks[0].toObject().value(u"futureTrackField"_s).toInt(), 42);
        QCOMPARE(tracks[0].toObject().value(u"clips"_s).toArray()[0].toObject().value(u"masks"_s).toArray().size(), 1);
        const QJsonObject text = tracks[2].toObject().value(u"clips"_s).toArray()[0].toObject();
        QCOMPARE(text.value(u"kind"_s).toString(), u"text"_s);
        QCOMPARE(text.value(u"text"_s).toString(), u"Ciao Roma!"_s);
        QCOMPARE(text.value(u"style"_s).toObject().value(u"size"_s).toDouble(), 0.06);
        QCOMPARE(text.value(u"style"_s).toObject().value(u"gradient"_s).toObject().value(u"angle"_s).toInt(), 90);
        QCOMPARE(text.value(u"spans"_s).toArray().size(), 1);
        QCOMPARE(tracks[0].toObject().value(u"gainDb"_s).toDouble(), -6.0);
    }

    void exampleFromTheSpecificationLoads()
    {
        QFile file(QFINDTESTDATA("../data/format/v1/example.vproj"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const ProjectLoadResult loaded = projectjson::fromBytes(file.readAll());
        QVERIFY2(loaded.ok(), qPrintable(loaded.error));
        QVERIFY2(loaded.warnings.isEmpty(), qPrintable(loaded.warnings.join(u'\n')));
        QCOMPARE(loaded.sourceFormatVersion, 1);
        QVERIFY(!loaded.migrated);
        const ProjectData &project = *loaded.project;
        QCOMPARE(project.name, u"Spiaggia"_s);
        QCOMPARE(project.settings.frameRate, Rational(30));
        const Clip &clip = project.mainSequence()->visualTracks[0].clips[0];
        QCOMPARE(clip.duration, RationalTime(150, Rational(30)));
        QCOMPARE(clip.media()->sourceIn, RationalTime(60, Rational(30)));
        QCOMPARE(clip.media()->audio.fadeOut, std::optional(RationalTime(15, Rational(30))));
        QCOMPARE(project.mainSequence()->canvas.preset, CanvasPreset::Portrait9x16);
        QCOMPARE(project.createdAt, QDateTime::fromString(u"2026-09-24T17:40:12Z"_s, Qt::ISODate));
    }

    void newerFormatIsRefused()
    {
        QJsonObject json = projectjson::toJson(richProject());
        json.insert(u"formatVersion"_s, kProjectFormatVersion + 1);
        const ProjectLoadResult loaded = projectjson::fromJson(json);
        QVERIFY(!loaded.ok());
        QVERIFY(loaded.error.contains(u"newer version"_s));
    }

    void migrationChain()
    {
        // No migration exists yet: same version passes through, a missing step is reported.
        const auto same = migrations::migrate(QJsonObject{{u"a"_s, 1}}, 1, 1);
        QVERIFY(same.project.has_value());
        QVERIFY(!same.migrated);
        const auto missing = migrations::migrate(QJsonObject{}, 1, 2);
        QVERIFY(!missing.project.has_value());
        QVERIFY(!migrations::migrate(QJsonObject{}, 0, 1).project.has_value());
    }

    void notAProject()
    {
        QVERIFY(!projectjson::fromBytes("{\"format\": \"something.else\"}").ok());
        QVERIFY(!projectjson::fromBytes("[1, 2, 3]").ok());
        const ProjectLoadResult broken = projectjson::fromBytes("{\"format\": \"vedit.proj");
        QVERIFY(!broken.ok());
        QVERIFY(broken.error.contains(u"damaged"_s));
    }

    void missingRequiredFieldIsAnError()
    {
        QJsonObject json = projectjson::toJson(richProject());
        QJsonArray sequences = json.value(u"sequences"_s).toArray();
        QJsonObject sequence = sequences[0].toObject();
        QJsonArray tracks = sequence.value(u"visualTracks"_s).toArray();
        QJsonObject track = tracks[0].toObject();
        QJsonArray clips = track.value(u"clips"_s).toArray();
        QJsonObject clip = clips[0].toObject();
        clip.remove(u"start"_s);
        clips[0] = clip;
        track.insert(u"clips"_s, clips);
        tracks[0] = track;
        sequence.insert(u"visualTracks"_s, tracks);
        sequences[0] = sequence;
        json.insert(u"sequences"_s, sequences);
        const ProjectLoadResult loaded = projectjson::fromJson(json);
        QVERIFY(!loaded.ok());
        QVERIFY2(loaded.error.contains(u"clips[0].start"_s), qPrintable(loaded.error));
    }

    void wrongValuesAreCorrectedWithWarnings()
    {
        QJsonObject json = projectjson::toJson(richProject());
        QJsonArray sequences = json.value(u"sequences"_s).toArray();
        QJsonObject sequence = sequences[0].toObject();
        QJsonArray tracks = sequence.value(u"visualTracks"_s).toArray();
        QJsonObject track = tracks[0].toObject();
        QJsonArray clips = track.value(u"clips"_s).toArray();
        QJsonObject clip = clips[0].toObject();
        clip.insert(u"blendMode"_s, u"sparkles"_s);
        clip.insert(u"speed"_s, 1000.0);
        clip.insert(u"opacity"_s, u"not a number"_s);
        clips[0] = clip;
        track.insert(u"clips"_s, clips);
        track.insert(u"height"_s, -3);
        tracks[0] = track;
        sequence.insert(u"visualTracks"_s, tracks);
        sequences[0] = sequence;
        json.insert(u"sequences"_s, sequences);
        const ProjectLoadResult loaded = projectjson::fromJson(json);
        QVERIFY2(loaded.ok(), qPrintable(loaded.error));
        QCOMPARE(loaded.warnings.size(), 4);
        const Clip &loadedClip = loaded.project->mainSequence()->visualTracks[0].clips[0];
        QCOMPARE(loadedClip.blendMode, BlendMode::Normal);
        QCOMPARE(loadedClip.media()->speed, 100.0);
        QCOMPARE(loadedClip.opacity.numberAt(frames(0)), 1.0);
    }

    void brokenTransitionsAreRepaired()
    {
        ProjectData project = richProject();
        Track &main = project.sequences.front().visualTracks.front();
        main.transitions.front().to = ClipId::create(); // points nowhere
        const ProjectLoadResult loaded = projectjson::fromBytes(projectjson::toBytes(project));
        QVERIFY2(loaded.ok(), qPrintable(loaded.error));
        QVERIFY(loaded.project->mainSequence()->visualTracks.front().transitions.empty());
        QVERIFY(!loaded.warnings.isEmpty());
    }

    void invariantViolationIsReported()
    {
        ProjectData project = richProject();
        Track &main = project.sequences.front().visualTracks.front();
        main.clips[1].start = frames(5000); // gap on the magnetic main track
        const ProjectLoadResult loaded = projectjson::fromBytes(projectjson::toBytes(project));
        QVERIFY(!loaded.ok());
        QVERIFY(loaded.error.contains(u"gap"_s));
    }

    void atomicFileWrite()
    {
        QTemporaryDir dir(QDir::tempPath() + u"/vedit-test-XXXXXX"_s);
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(u"sub/project.vproj"_s);
        const ProjectData project = richProject();
        QVERIFY(projectfile::save(path, project));
        const ProjectLoadResult loaded = projectfile::load(path);
        QVERIFY2(loaded.ok(), qPrintable(loaded.error));
        QVERIFY2(*loaded.project == project, qPrintable(firstDifference(*loaded.project, project)));
        // Overwrite: only the final file remains, no temporary leftovers.
        QVERIFY(projectfile::writeAtomically(path, "{}\n"));
        QCOMPARE(QDir(dir.filePath(u"sub"_s)).entryList(QDir::Files | QDir::Hidden), QStringList{u"project.vproj"_s});
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), QByteArray("{}\n"));
        QVERIFY(!projectfile::load(dir.filePath(u"missing.vproj"_s)).ok());
    }
};

QTEST_GUILESS_MAIN(TestSerialization)
#include "tst_serialization.moc"
