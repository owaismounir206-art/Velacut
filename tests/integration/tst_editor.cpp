// SPDX-License-Identifier: GPL-3.0-or-later
// The Phase 1 criterion through the editor controller, as the interface drives it (SPEC §8): import 3 clips, cut
// and reorder them, export a correct MP4, close and reopen the identical draft without ever pressing "Save".
#include "../unit/ProjectFixture.h"
#include "TestMedia.h"

#include "core/serialization/ProjectJson.h"
#include "document/Document.h"
#include "document/DraftStore.h"
#include "engine/analysis/MediaAnalysis.h"
#include "engine/mlt/MltRuntime.h"
#include "ui/controllers/ActionRegistry.h"
#include "ui/controllers/ClipInspector.h"
#include "ui/items/AssetThumbnail.h"
#include "ui/models/AssetLibraryModel.h"
#include "ui/controllers/EditorController.h"

#include <QElapsedTimer>
#include <QSignalSpy>
#include <QTemporaryDir>

using namespace vedit;
using namespace vedit::ui;
using namespace vedit::test;
using namespace Qt::StringLiterals;

class TestEditor : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;
    TestMediaFiles m_files;

    static const Track &mainTrack(EditorController &editor) { return editor.data().mainSequence()->visualTracks.front(); }

private slots:
    void initTestCase()
    {
        m_files = generateTestMedia(m_dir.filePath(u"media"_s));
        if (!m_files.ok) {
            QSKIP("ffmpeg is needed to generate the test media");
        }
        QVERIFY(engine::MltRuntime::waitUntilReady());
    }

    void phaseOneCriterion()
    {
        document::DraftStore store(m_dir.filePath(u"drafts"_s));
        engine::MediaAnalysis analysis(m_dir.filePath(u"cache"_s));
        QString error;
        auto editor = std::make_unique<EditorController>(store.createDraft(&error), analysis,
                                                         QStringLiteral(VEDIT_RENDER_EXECUTABLE));
        editor->player()->setVolume(0.0);
        const ProjectId id = editor->data().id;
        QSignalSpy messages(editor.get(), &EditorController::message);

        // Three files dropped on the empty timeline: imported in order, placed one after the other.
        editor->importAndInsertPaths({m_files.landscape, m_files.vertical, m_files.photo}, 0, editor->timeline()->mainRow());
        QTRY_COMPARE_WITH_TIMEOUT(mainTrack(*editor).clips.size(), size_t(3), 20000);
        QVERIFY2(messages.isEmpty(), messages.isEmpty() ? "" : qPrintable(messages.first().first().toString()));
        QCOMPARE(editor->data().media.size(), size_t(3));
        // Canvas and frame rate from the first clip.
        QCOMPARE(editor->canvasSize(), QSize(320, 180));
        QCOMPARE(editor->formatText(), u"16:9"_s);
        QCOMPARE(editor->frameRate(), 30.0);
        QCOMPARE(editor->timeline()->duration(), 120 + 120 + 90); // photo: 3 s
        const QString vertical = mainTrack(*editor).clips[1].id.toString();
        const QString photo = mainTrack(*editor).clips[2].id.toString();

        // Cut: trim the first clip, split the second one at the playhead, delete a part, reorder.
        QVERIFY(editor->trimClip(mainTrack(*editor).clips[0].id.toString(), false, 60));
        editor->clearSelection();
        editor->player()->seek(100); // inside the vertical clip (60–180)
        QVERIFY(editor->canSplit());
        QVERIFY(editor->split());
        QCOMPARE(mainTrack(*editor).clips.size(), size_t(4));
        editor->select(vertical, false);
        QVERIFY(editor->deleteSelection());
        QCOMPARE(messages.last().at(1).toBool(), true); // "Clip deleted — Undo"
        QVERIFY(editor->moveClip(photo, 0, editor->timeline()->mainRow()));
        QCOMPARE(mainTrack(*editor).clips.front().id.toString(), photo);
        QCOMPARE(editor->timeline()->duration(), 90 + 60 + 80);
        // Undo and redo go through the same commands.
        editor->undo();
        QCOMPARE(mainTrack(*editor).clips.back().id.toString(), photo);
        editor->redo();
        QCOMPARE(mainTrack(*editor).clips.front().id.toString(), photo);

        // Export with the recommended settings.
        const QVariantMap defaults = editor->exportDefaults();
        QCOMPARE(defaults.value(u"frameRate"_s).toString(), u"30"_s);
        QCOMPARE(defaults.value(u"quality"_s).toInt(), 1);
        QCOMPARE(defaults.value(u"resolution"_s).toInt(), 180); // the project's own size, not upscaled
        QCOMPARE(defaults.value(u"resolutions"_s).toList().first().toMap().value(u"label"_s).toString(), u"180p"_s);
        QVERIFY(!editor->exportEstimate(180, u"30"_s, 1).isEmpty());
        const QString folder = m_dir.filePath(u"videos"_s);
        QDir().mkpath(folder);
        QSignalSpy exported(editor.get(), &EditorController::exportFinished);
        QVERIFY(editor->startExport(defaults.value(u"fileName"_s).toString(), folder, 180, u"30"_s, 1));
        QVERIFY(exported.wait(60000));
        const QString output = exported.first().first().toString();
        const QJsonObject probe = ffprobe(output);
        QCOMPARE(streamOfType(probe, u"video"_s).value(u"nb_read_frames"_s).toString().toInt(), 230);
        QCOMPARE(streamOfType(probe, u"video"_s).value(u"width"_s).toInt(), 320);
        QCOMPARE(streamOfType(probe, u"audio"_s).value(u"codec_name"_s).toString(), u"aac"_s);

        // Close and reopen: identical, and "Save" was never pressed (there is none).
        QTRY_COMPARE_WITH_TIMEOUT(editor->saveState(), int(EditorController::Saved), 5000);
        ProjectData before = editor->data();
        QVERIFY(editor->close());
        editor.reset();
        auto reopened = std::make_unique<EditorController>(store.openDraft(id, &error), analysis,
                                                           QStringLiteral(VEDIT_RENDER_EXECUTABLE));
        ProjectData after = reopened->data();
        before.modifiedAt = after.modifiedAt = {};
        QVERIFY2(before == after, qPrintable(firstDifference(before, after)));
        QVERIFY(!reopened->recovered());
        QCOMPARE(reopened->player()->position(), 100); // the playhead where it was left
        // The draft shows on the home screen with its thumbnail.
        QVERIFY(!store.info(id)->thumbnailPath.isEmpty());
    }

    // The properties panel (Phase 2): every change is a command, a drag is one undo step, "Apply to all", reset,
    // copy/paste attributes, auto enhance, texts and their styles.
    void inspector()
    {
        document::DraftStore store(m_dir.filePath(u"drafts-inspector"_s));
        engine::MediaAnalysis analysis(m_dir.filePath(u"cache"_s));
        QString error;
        EditorController editor(store.createDraft(&error), analysis, QStringLiteral(VEDIT_RENDER_EXECUTABLE));
        editor.player()->setVolume(0.0);
        ClipInspector &inspector = *editor.inspector();
        editor.importAndInsertPaths({m_files.landscape, m_files.photo}, 0, editor.timeline()->mainRow());
        QTRY_COMPARE_WITH_TIMEOUT(mainTrack(editor).clips.size(), size_t(2), 20000);
        const QString video = mainTrack(editor).clips[0].id.toString();
        const QString photo = mainTrack(editor).clips[1].id.toString();
        QUndoStack &undo = editor.document().undoStack();

        editor.clearSelection();
        QVERIFY(!inspector.active());
        editor.select(video, false);
        QVERIFY(inspector.active());
        QCOMPARE(inspector.kind(), int(ClipInspector::Video));
        QCOMPARE(inspector.sections(), (QStringList{u"video"_s, u"background"_s, u"audio"_s, u"speed"_s, u"animation"_s,
                                                   u"filter"_s, u"adjust"_s}));
        QVERIFY(inspector.modifiedSections().isEmpty());

        // A slider drag: many values, one undo step.
        const int steps = undo.count();
        for (const double opacity : {0.9, 0.7, 0.5}) {
            QVERIFY(inspector.set(u"opacity"_s, opacity));
        }
        inspector.endGesture();
        QCOMPARE(undo.count(), steps + 1);
        QCOMPARE(inspector.values().value(u"opacity"_s).toDouble(), 0.5);
        QVERIFY(inspector.modifiedSections().contains(u"video"_s));
        editor.undo();
        QCOMPARE(inspector.values().value(u"opacity"_s).toDouble(), 1.0);

        // A filter: click applies, click again removes; "Apply to all" puts it on the photo too, one undo step.
        QVERIFY(inspector.toggleFilter(u"filters/vivid"_s));
        QCOMPARE(inspector.values().value(u"filter"_s).toString(), u"filters/vivid"_s);
        QVERIFY(inspector.toggleFilter(u"filters/vivid"_s));
        QCOMPARE(inspector.values().value(u"filter"_s).toString(), QString());
        QVERIFY(inspector.toggleFilter(u"filters/film"_s));
        QVERIFY(inspector.set(u"filter.intensity"_s, 0.4));
        inspector.endGesture();
        QVERIFY(inspector.applyToAll(u"filter"_s));
        const auto filterOf = [&](int index) -> QString {
            for (const Effect &effect : mainTrack(editor).clips[static_cast<size_t>(index)].effects) {
                if (effect.type == u"vedit.filter"_s) {
                    return effect.preset->id + u'@' + QString::number(std::get<double>(effect.intensity.staticValue()));
                }
            }
            return QString();
        };
        QCOMPARE(filterOf(1), u"filters/film@0.4"_s);
        editor.undo();
        QCOMPARE(filterOf(1), QString());
        QCOMPARE(filterOf(0), u"filters/film@0.4"_s);
        // Hovering a filter changes only the preview: the model stays as it is.
        inspector.previewFilter(u"filters/bw"_s);
        QCOMPARE(filterOf(0), u"filters/film@0.4"_s);
        inspector.clearPreview();

        // Adjustments and "Reset".
        QVERIFY(inspector.set(u"adjust.exposure"_s, 0.5));
        inspector.endGesture();
        QVERIFY(inspector.modifiedSections().contains(u"adjust"_s));
        QCOMPARE(inspector.values().value(u"adjust.exposure"_s).toDouble(), 0.5);
        QVERIFY(inspector.reset(u"adjust"_s));
        QVERIFY(!inspector.modifiedSections().contains(u"adjust"_s));
        QCOMPARE(inspector.values().value(u"adjust.exposure"_s).toDouble(), 0.0);

        // Speed: the clip keeps its material, so it gets shorter and the photo moves along.
        QCOMPARE(mainTrack(editor).clips[0].duration.value(), 120);
        QVERIFY(inspector.set(u"speed"_s, 2.0));
        inspector.endGesture();
        QCOMPARE(mainTrack(editor).clips[0].duration.value(), 60);
        QCOMPARE(mainTrack(editor).clips[1].start.value(), 60);
        QVERIFY(inspector.set(u"reversed"_s, true));
        QVERIFY(inspector.reset(u"speed"_s));
        QCOMPARE(mainTrack(editor).clips[0].duration.value(), 120);
        QVERIFY(!mainTrack(editor).clips[0].media()->reversed);

        // Volume and fades.
        QVERIFY(inspector.set(u"volume"_s, -6.0));
        QVERIFY(inspector.set(u"fadeIn"_s, 0.5));
        inspector.endGesture();
        QCOMPARE(mainTrack(editor).clips[0].media()->audio.fadeIn->value(), 15);
        QCOMPARE(inspector.values().value(u"fadeIn"_s).toDouble(), 0.5);

        // Background: blurred, then the default of the whole video.
        QVERIFY(inspector.set(u"background.type"_s, 1));
        QVERIFY(mainTrack(editor).clips[0].background.has_value());
        QVERIFY(inspector.applyToAll(u"background"_s));
        QVERIFY(!mainTrack(editor).clips[0].background.has_value());
        QCOMPARE(editor.data().mainSequence()->defaultBackground->type, BackgroundType::Blur);
        editor.select(photo, false);
        QCOMPARE(inspector.values().value(u"background.type"_s).toInt(), 1);
        QCOMPARE(inspector.kind(), int(ClipInspector::Image));
        QVERIFY(!inspector.sections().contains(u"speed"_s));
        QVERIFY(!inspector.sections().contains(u"audio"_s));

        // Copy and paste attributes: the look and the placement of the video onto the photo.
        editor.select(video, false);
        QVERIFY(inspector.set(u"scale"_s, 1.5));
        inspector.endGesture();
        inspector.copyAttributes();
        QVERIFY(inspector.canPaste());
        editor.select(photo, false);
        QVERIFY(inspector.pasteAttributes());
        QCOMPARE(std::get<Vec2>(mainTrack(editor).clips[1].transform.scale.staticValue()).x, 1.5);
        QCOMPARE(filterOf(1), u"filters/film@0.4"_s);

        // Auto enhance: adjustments from the pictures (a dark test pattern gets brighter) and the volume from the peaks.
        editor.select(video, false);
        const Media &media = *editor.data().findMedia(mainTrack(editor).clips[0].media()->mediaId);
        QTRY_VERIFY_WITH_TIMEOUT(!analysis.thumbnails(media).isNull() && analysis.waveform(media), 20000);
        QVERIFY(inspector.autoEnhance());
        QVERIFY(inspector.modifiedSections().contains(u"adjust"_s));
        QVERIFY(inspector.values().value(u"volume"_s).toDouble() != -6.0);

        // Texts: added at the playhead with the default style, edited, restyled from the library, reset.
        editor.player()->seek(30);
        QVERIFY(editor.addText());
        QCOMPARE(inspector.kind(), int(ClipInspector::Text));
        QCOMPARE(inspector.sections(), (QStringList{u"text"_s, u"video"_s, u"animation"_s}));
        QVERIFY(inspector.values().value(u"text.stroke"_s).toBool()); // readable on any picture
        QVERIFY(inspector.set(u"text.content"_s, u"Ciao"_s));
        QVERIFY(inspector.set(u"text.content"_s, u"Ciao a tutti"_s));
        inspector.endGesture();
        const int afterTyping = undo.count();
        QCOMPARE(inspector.values().value(u"text.content"_s).toString(), u"Ciao a tutti"_s);
        QVERIFY(inspector.set(u"text.size"_s, 0.1));
        inspector.endGesture();
        QVERIFY(inspector.applyTextStyle(u"text/label-black"_s));
        QCOMPARE(inspector.values().value(u"text.preset"_s).toString(), u"text/label-black"_s);
        QVERIFY(inspector.values().value(u"text.background"_s).toBool());
        QCOMPARE(inspector.values().value(u"text.size"_s).toDouble(), 0.1); // the style keeps the size
        QVERIFY(inspector.reset(u"text"_s));
        QVERIFY(!inspector.modifiedSections().contains(u"text"_s));
        editor.undo();
        editor.undo();
        editor.undo();
        QCOMPARE(undo.index(), afterTyping);
        QCOMPARE(inspector.values().value(u"text.content"_s).toString(), u"Ciao a tutti"_s);
        QVERIFY(editor.close());
    }

    // The libraries of the core pack and their pictures; transitions from the library onto the right cut.
    void librariesAndTransitions()
    {
        AssetLibraryModel library;
        QCOMPARE(library.kind(), AssetLibraryModel::Filters);
        const int filters = library.rowCount();
        QVERIFY(filters >= 30);
        library.setCategory(u"bw"_s);
        QVERIFY(library.rowCount() > 0 && library.rowCount() < filters);
        library.setCategory({});
        library.setSearch(u"sepia"_s);
        QCOMPARE(library.rowCount(), 1);
        library.setSearch({});
        library.setKind(AssetLibraryModel::Transitions);
        QVERIFY(library.rowCount() >= 18);
        QVERIFY(!library.categories().isEmpty());
        library.setKind(AssetLibraryModel::TextStyles);
        QVERIFY(library.rowCount() >= 24);

        // Pictures: a filter changes the sample, a transition goes from A (0) to B (1), a text style draws something.
        const QSize size(96, 72);
        const QImage plain = AssetThumbnail::render(AssetLibraryModel::Filters, u"filters/clear"_s, 0, {}, size);
        const QImage bw = AssetThumbnail::render(AssetLibraryModel::Filters, u"filters/bw"_s, 0, {}, size);
        QCOMPARE(plain.size(), size);
        QVERIFY(plain != bw);
        const QRgb grey = bw.pixel(48, 20);
        QVERIFY(std::abs(qRed(grey) - qBlue(grey)) <= 2); // black and white
        const QImage start = AssetThumbnail::render(AssetLibraryModel::Transitions, u"transitions/wipe-left"_s, 0, {}, size);
        const QImage end = AssetThumbnail::render(AssetLibraryModel::Transitions, u"transitions/wipe-left"_s, 1, {}, size);
        QVERIFY(start != end);
        const QImage text = AssetThumbnail::render(AssetLibraryModel::TextStyles, u"text/outline-yellow"_s, 0, {}, size);
        const QImage none = AssetThumbnail::render(AssetLibraryModel::TextStyles, u"missing"_s, 0, {}, size);
        QVERIFY(text != none);

        // Transitions: three clips; nothing selected = the cut nearest to the playhead.
        document::DraftStore store(m_dir.filePath(u"drafts-transitions"_s));
        engine::MediaAnalysis analysis(m_dir.filePath(u"cache"_s));
        QString error;
        EditorController editor(store.createDraft(&error), analysis, QStringLiteral(VEDIT_RENDER_EXECUTABLE));
        editor.player()->setVolume(0.0);
        ClipInspector &inspector = *editor.inspector();
        editor.importAndInsertPaths({m_files.landscape, m_files.vertical, m_files.photo}, 0, editor.timeline()->mainRow());
        QTRY_COMPARE_WITH_TIMEOUT(mainTrack(editor).clips.size(), size_t(3), 20000);
        editor.clearSelection();
        editor.player()->seek(230); // near the second cut (240)
        QCOMPARE(editor.transitionTarget(), mainTrack(editor).clips[1].id);
        QCOMPARE(editor.timeline()->cuts().size(), 2);
        const QVariantMap window = inspector.previewTransition(u"transitions/dissolve"_s);
        QCOMPARE(window.value(u"start"_s).toInt(), 240 - 7);
        QCOMPARE(window.value(u"end"_s).toInt(), 240 - 7 + 15);
        QVERIFY(mainTrack(editor).transitions.empty()); // a preview only
        inspector.clearPreview();
        QVERIFY(inspector.toggleTransition(u"transitions/dissolve"_s));
        QCOMPARE(mainTrack(editor).transitions.size(), size_t(1));
        QCOMPARE(inspector.kind(), int(ClipInspector::Transition));
        QCOMPARE(inspector.values().value(u"transition.duration"_s).toDouble(), 0.5);
        // Another type keeps the duration chosen; the same type again removes it.
        QVERIFY(inspector.set(u"transition.duration"_s, 1.0));
        inspector.endGesture();
        QVERIFY(inspector.toggleTransition(u"transitions/wipe-left"_s));
        QCOMPARE(mainTrack(editor).transitions.front().type.id, u"transitions/wipe-left"_s);
        QCOMPARE(mainTrack(editor).transitions.front().duration.value(), 30);
        QVERIFY(inspector.applyToAll(u"transition"_s));
        QCOMPARE(mainTrack(editor).transitions.size(), size_t(2));
        QVERIFY(inspector.toggleTransition(u"transitions/wipe-left"_s));
        QCOMPARE(mainTrack(editor).transitions.size(), size_t(1));
        QVERIFY(inspector.randomTransitions());
        QCOMPARE(mainTrack(editor).transitions.size(), size_t(2));
        QVERIFY(inspector.removeAllTransitions());
        QVERIFY(mainTrack(editor).transitions.empty());

        // A selected clip: the cut after it (the last clip: the cut before it).
        editor.select(mainTrack(editor).clips[2].id.toString(), false);
        QCOMPARE(editor.transitionTarget(), mainTrack(editor).clips[1].id);
        editor.select(mainTrack(editor).clips[0].id.toString(), false);
        QCOMPARE(editor.transitionTarget(), mainTrack(editor).clips[0].id);
        // A cut clicked on the timeline.
        QSignalSpy libraryRequests(&editor, &EditorController::libraryRequested);
        editor.selectCut(mainTrack(editor).clips[1].id.toString());
        QCOMPARE(libraryRequests.count(), 1);
        QVERIFY(editor.selection().isEmpty());
        QCOMPARE(editor.transitionTarget(), mainTrack(editor).clips[1].id);

        // A filter with nothing selected: the clip on screen.
        editor.clearSelection();
        editor.player()->seek(10);
        QVERIFY(inspector.toggleFilter(u"filters/vivid"_s));
        QCOMPARE(editor.selection(), QStringList{mainTrack(editor).clips[0].id.toString()});
        QVERIFY(editor.close());
    }

    // The Phase 2 criterion (SPEC §8): a vertical 9:16 video with texts, music, transitions and filters,
    // exported and verified.
    void phaseTwoCriterion()
    {
        document::DraftStore store(m_dir.filePath(u"drafts-phase2"_s));
        engine::MediaAnalysis analysis(m_dir.filePath(u"cache"_s));
        QString error;
        auto editor = std::make_unique<EditorController>(store.createDraft(&error), analysis,
                                                         QStringLiteral(VEDIT_RENDER_EXECUTABLE));
        editor->player()->setVolume(0.0);
        ClipInspector &inspector = *editor->inspector();

        // 1. Clips: import vertical video and a photo, and audio underneath.
        editor->importAndInsertPaths({m_files.vertical, m_files.photo}, 0, editor->timeline()->mainRow());
        QTRY_COMPARE_WITH_TIMEOUT(mainTrack(*editor).clips.size(), size_t(2), 20000);
        editor->player()->seek(0);
        editor->importAndInsertPaths({m_files.music}, 0, 1);
        QTRY_COMPARE_WITH_TIMEOUT(editor->data().mainSequence()->audioTracks.size(), size_t(1), 20000);

        // 2. Format: vertical 9:16.
        editor->setCanvasPreset(int(CanvasPreset::Portrait9x16));
        QCOMPARE(editor->canvasSize(), QSize(180, 320));
        QCOMPARE(editor->formatText(), u"9:16"_s);

        // 3. Text: add a title at the beginning, style it.
        editor->player()->seek(0);
        editor->clearSelection();
        QVERIFY(editor->addText());
        QCOMPARE(inspector.kind(), int(ClipInspector::Text));
        QVERIFY(inspector.set(u"text.content"_s, u"Shorts Title"_s));
        QVERIFY(inspector.applyTextStyle(u"text/outline-yellow"_s));
        inspector.endGesture();

        // 4. Transitions: dissolve between the two clips.
        editor->clearSelection();
        editor->player()->seek(120);
        QVERIFY(inspector.toggleTransition(u"transitions/dissolve"_s));
        QCOMPARE(mainTrack(*editor).transitions.size(), size_t(1));

        // 5. Filters: vivid filter on the video clip.
        editor->select(mainTrack(*editor).clips[0].id.toString(), false);
        QVERIFY(inspector.toggleFilter(u"filters/vivid"_s));
        inspector.endGesture();

        // 6. Export and verify output: 9:16 (180x320), H.264, AAC.
        const QVariantMap defaults = editor->exportDefaults();
        const QString folder = m_dir.filePath(u"videos-phase2"_s);
        QDir().mkpath(folder);
        QSignalSpy exported(editor.get(), &EditorController::exportFinished);
        const int res = defaults.value(u"resolution"_s).toInt();
        QVERIFY(editor->startExport(defaults.value(u"fileName"_s).toString(), folder, res, u"30"_s, 1));
        QVERIFY(exported.wait(60000));
        const QString output = exported.first().first().toString();
        const QJsonObject probe = ffprobe(output);
        QCOMPARE(streamOfType(probe, u"video"_s).value(u"width"_s).toInt(), 180);
        QCOMPARE(streamOfType(probe, u"video"_s).value(u"height"_s).toInt(), 320);
        QCOMPARE(streamOfType(probe, u"audio"_s).value(u"codec_name"_s).toString(), u"aac"_s);

        // 7. Continuous save: close and reopen identical without explicit save.
        QTRY_COMPARE_WITH_TIMEOUT(editor->saveState(), int(EditorController::Saved), 5000);
        const ProjectId id = editor->data().id;
        ProjectData before = editor->data();
        QVERIFY(editor->close());
        editor.reset();
        auto reopened = std::make_unique<EditorController>(store.openDraft(id, &error), analysis,
                                                           QStringLiteral(VEDIT_RENDER_EXECUTABLE));
        ProjectData after = reopened->data();
        before.modifiedAt = after.modifiedAt = {};
        QVERIFY2(before == after, qPrintable(firstDifference(before, after)));
    }

    // Markers at the playhead (on the selected clip, or on the video) and jumping between them; preset animations.
    void markersAndAnimations()
    {
        document::DraftStore store(m_dir.filePath(u"drafts-markers"_s));
        engine::MediaAnalysis analysis(m_dir.filePath(u"cache"_s));
        QString error;
        EditorController editor(store.createDraft(&error), analysis, QStringLiteral(VEDIT_RENDER_EXECUTABLE));
        editor.player()->setVolume(0.0);
        editor.importAndInsertPaths({m_files.landscape, m_files.vertical}, 0, editor.timeline()->mainRow());
        QTRY_COMPARE_WITH_TIMEOUT(mainTrack(editor).clips.size(), size_t(2), 20000);
        const ClipId second = mainTrack(editor).clips[1].id;

        // Nothing selected: a marker of the video.
        editor.clearSelection();
        editor.player()->seek(30);
        QVERIFY(editor.addMarker(u"Start"_s));
        QCOMPARE(editor.data().mainSequence()->markers.size(), size_t(1));
        QCOMPARE(editor.timeline()->markers().size(), 1);
        // A selected clip at 2×: the marker goes on the clip, in its keyframe time (source time).
        editor.select(second.toString(), false);
        QVERIFY(editor.inspector()->set(u"speed"_s, 2.0));
        editor.player()->seek(120 + 20); // 20 frames into the clip = 40 frames of its source
        QVERIFY(editor.addMarker(u"Beat"_s));
        const Clip &clip = *editor.data().findClip(second);
        QCOMPARE(clip.markers.size(), size_t(1));
        QCOMPARE(clip.markers.front().time.value(), 40);

        // Jumping from marker to marker.
        editor.player()->seek(0);
        editor.nextMarker();
        QTRY_COMPARE(editor.player()->position(), 30);
        editor.nextMarker();
        QTRY_COMPARE(editor.player()->position(), 140);
        editor.nextMarker(); // none after: stays
        QTRY_COMPARE(editor.player()->position(), 140);
        editor.previousMarker();
        QTRY_COMPARE(editor.player()->position(), 30);

        // From the library: preview (only the projection), click adds, click again removes; the length is editable.
        ClipInspector &inspector = *editor.inspector();
        const QVariantMap window = inspector.previewAnimation(u"animations/out/fade"_s);
        QCOMPARE(window.value(u"end"_s).toInt(), 120 + 60 - 1); // the clip ends at 180 (2×)
        QVERIFY(editor.data().findClip(second)->animations.isEmpty());
        QVERIFY(inspector.toggleAnimation(u"animations/out/fade"_s));
        QCOMPARE(inspector.values().value(u"animation.out"_s).toString(), u"animations/out/fade"_s);
        QVERIFY(inspector.set(u"animation.out.duration"_s, 1.5));
        inspector.endGesture();
        QCOMPARE(editor.data().findClip(second)->animations.out->duration.value(), 45);
        QVERIFY(inspector.toggleAnimation(u"animations/out/zoom_in"_s)); // same kind: replaced, length kept
        QCOMPARE(editor.data().findClip(second)->animations.out->duration.value(), 45);
        QVERIFY(inspector.toggleAnimation(u"animations/out/zoom_in"_s));
        QVERIFY(!editor.data().findClip(second)->animations.out);

        // Preset animations on the selected clip: one per kind, replaced, removed.
        QVERIFY(editor.applyAnimation(u"animations/in/zoom_in"_s));
        QVERIFY(editor.applyAnimation(u"animations/out/fade"_s, 1.0));
        QCOMPARE(editor.data().findClip(second)->animations.in->type.id, u"animations/in/zoom_in"_s);
        QCOMPARE(editor.data().findClip(second)->animations.out->duration.value(), 30);
        QVERIFY(editor.applyAnimation(u"animations/in/fade"_s));
        QCOMPARE(editor.data().findClip(second)->animations.in->type.id, u"animations/in/fade"_s);
        QVERIFY(editor.removeAnimation(u"in"_s));
        QVERIFY(!editor.data().findClip(second)->animations.in);
        QVERIFY(editor.data().findClip(second)->animations.out);
        QVERIFY(editor.removeAnimation());
        QVERIFY(editor.data().findClip(second)->animations.isEmpty());
        QVERIFY(editor.close());
    }

    // The actions of the selection (toolbar, right-click menu), the universal search, the freeze frame.
    void actionsSearchAndFreeze()
    {
        document::DraftStore store(m_dir.filePath(u"drafts-actions"_s));
        engine::MediaAnalysis analysis(m_dir.filePath(u"cache"_s));
        QString error;
        EditorController editor(store.createDraft(&error), analysis, QStringLiteral(VEDIT_RENDER_EXECUTABLE));
        editor.player()->setVolume(0.0);
        ActionRegistry &actions = *editor.actions();
        editor.importAndInsertPaths({m_files.landscape, m_files.photo}, 0, editor.timeline()->mainRow());
        QTRY_COMPARE_WITH_TIMEOUT(mainTrack(editor).clips.size(), size_t(2), 20000);
        const auto ids = [&actions] {
            QStringList list;
            for (const QVariant &action : actions.toolbar()) {
                list << action.toMap().value(u"id"_s).toString();
            }
            return list;
        };
        // SPEC 0bis rule 3: what shows for each selection.
        editor.clearSelection();
        QCOMPARE(ids(), (QStringList{u"split"_s, u"freeze"_s, u"addText"_s, u"addAudio"_s}));
        editor.select(mainTrack(editor).clips[0].id.toString(), false);
        QCOMPARE(ids(), (QStringList{u"split"_s, u"delete"_s, u"duplicate"_s, u"speed"_s, u"volume"_s, u"animation"_s,
                                     u"freeze"_s, u"reverse"_s, u"mirror"_s, u"rotate"_s, u"enhance"_s}));
        editor.select(mainTrack(editor).clips[1].id.toString(), false);
        QVERIFY(!ids().contains(u"speed"_s) && ids().contains(u"mirror"_s));

        // Toolbar buttons that open a page of the properties panel.
        QSignalSpy pages(&editor, &EditorController::propertiesRequested);
        editor.select(mainTrack(editor).clips[0].id.toString(), false);
        QVERIFY(actions.trigger(u"speed"_s));
        QCOMPARE(pages.last().first().toString(), u"speed"_s);
        QVERIFY(actions.trigger(u"mirror"_s));
        QVERIFY(mainTrack(editor).clips[0].transform.flipH);
        QVERIFY(actions.trigger(u"rotate"_s));
        QCOMPARE(std::get<double>(mainTrack(editor).clips[0].transform.rotation.staticValue()), 90.0);

        // Copy and paste attributes through the actions.
        QVERIFY(actions.trigger(u"copyAttributes"_s));
        editor.select(mainTrack(editor).clips[1].id.toString(), false);
        QVERIFY(actions.isEnabled(u"pasteAttributes"_s));
        QVERIFY(actions.trigger(u"pasteAttributes"_s));
        QVERIFY(mainTrack(editor).clips[1].transform.flipH);

        // Universal search: commands, library items (either language, accents ignored), the project's media.
        QCOMPARE(actions.search(u"speed"_s).first().toMap().value(u"id"_s).toString(), u"speed"_s);
        const QVariantMap sepia = actions.search(u"seppia"_s).value(0).toMap();
        QCOMPARE(sepia.value(u"kind"_s).toString(), u"filter"_s);
        QVERIFY(!actions.search(u"dissolvenza"_s).isEmpty());
        QCOMPARE(actions.search(u"landscape"_s).first().toMap().value(u"kind"_s).toString(), u"media"_s);
        QVERIFY(actions.search(u"zzzz"_s).isEmpty());
        QVERIFY(actions.activate(sepia.value(u"kind"_s).toString(), sepia.value(u"id"_s).toString()));
        QCOMPARE(editor.inspector()->values().value(u"filter"_s).toString(), sepia.value(u"id"_s).toString());

        // Freeze: 3 s of the frame at the playhead inserted there; the picture and its clip are one undo step.
        editor.clearSelection();
        editor.player()->seek(60);
        const size_t clips = mainTrack(editor).clips.size();
        const size_t media = editor.data().media.size();
        QVERIFY(actions.trigger(u"freeze"_s));
        QCOMPARE(mainTrack(editor).clips.size(), clips + 2); // split around the still
        QCOMPARE(editor.data().media.size(), media + 1);
        const Media &still = editor.data().media.back();
        QCOMPARE(still.kind, MediaKind::Image);
        QVERIFY(QFileInfo::exists(still.path));
        QCOMPARE(QImage(still.path).size(), QSize(320, 180));
        editor.undo();
        QCOMPARE(mainTrack(editor).clips.size(), clips);
        QCOMPARE(editor.data().media.size(), media);
        QVERIFY(editor.close());
    }

    void snappingAndFormat()
    {
        document::DraftStore store(m_dir.filePath(u"drafts2"_s));
        engine::MediaAnalysis analysis(m_dir.filePath(u"cache"_s));
        QString error;
        EditorController editor(store.createDraft(&error), analysis, QStringLiteral(VEDIT_RENDER_EXECUTABLE));
        editor.player()->setVolume(0.0);
        editor.importAndInsertPaths({m_files.landscape}, 0, editor.timeline()->mainRow());
        QTRY_COMPARE_WITH_TIMEOUT(mainTrack(editor).clips.size(), size_t(1), 20000);
        editor.player()->seek(50);
        // Edges: 0, 120 (clip end), 50 (playhead).
        QCOMPARE(editor.snap(47, {}, 5), 50);
        QCOMPARE(editor.snap(118, {}, 5), 120);
        QCOMPARE(editor.snap(80, {}, 5), 80);
        // A 30-frame range dragged near the clip end snaps its start or its end.
        QCOMPARE(editor.snapRange(123, 30, {}, 5), 120);
        QCOMPARE(editor.snapRange(18, 30, {}, 5), 20); // its end on the playhead
        // "+" of a media item: at the playhead.
        const QString media = editor.data().media.front().id.toString();
        QVERIFY(editor.addMedia(media));
        QCOMPARE(mainTrack(editor).clips.size(), size_t(2));
        // Format in one click: 9:16 at the same resolution class.
        editor.setCanvasPreset(int(CanvasPreset::Portrait9x16));
        QCOMPARE(editor.canvasSize(), QSize(180, 320));
        QCOMPARE(editor.formatText(), u"9:16"_s);
        editor.undo();
        QCOMPARE(editor.canvasSize(), QSize(320, 180));
        // Duplicate the selection.
        editor.select(mainTrack(editor).clips.front().id.toString(), false);
        QVERIFY(editor.duplicateSelection());
        QCOMPARE(mainTrack(editor).clips.size(), size_t(3));
        // A clip dragged into the space above the tracks gets a new track.
        QVERIFY(editor.moveClip(mainTrack(editor).clips.back().id.toString(), 10, -1));
        QCOMPARE(editor.data().mainSequence()->visualTracks.size(), size_t(2));
        QCOMPARE(editor.timeline()->trackRowCount(), 2);
        QCOMPARE(editor.timeline()->mainRow(), 1);
    }

    // SPEC §6: 500+ clips stay responsive. One edit = command + projection patch + timeline model diff; the continuous
    // save serializes the project on the UI thread (ARCHITECTURE §10: < 30 ms with 500 clips).
    void largeProjectStaysResponsive()
    {
        ProjectData data = ProjectData::createEmpty(u"Large"_s);
        data.settings.frameRate = Rational(30);
        data.sequences.front().canvas = Canvas{320, 180, CanvasPreset::Landscape16x9};
        Media media = testMedia(MediaKind::Video, m_files.landscape, RationalTime(120, Rational(30)), 320, 180, true);
        data.media = {media};
        Track &main = data.sequences.front().visualTracks.front();
        for (int i = 0; i < 500; ++i) {
            Clip clip;
            clip.id = ClipId::create();
            clip.start = frames(i * 30);
            clip.duration = frames(30);
            MediaClipData payload;
            payload.mediaId = media.id;
            payload.sourceIn = frames((i * 7) % 90);
            payload.streams = Streams::AudioVideo;
            clip.payload = payload;
            main.clips.push_back(std::move(clip));
        }
        QVERIFY(data.checkInvariants().isEmpty());
        document::DraftStore store(m_dir.filePath(u"drafts-large"_s));
        engine::MediaAnalysis analysis(m_dir.filePath(u"cache"_s));
        QString error;
        auto document = document::Document::create(store.directoryOf(data.id), data, &error);
        QVERIFY2(document, qPrintable(error));
        EditorController editor(std::move(document), analysis, QStringLiteral(VEDIT_RENDER_EXECUTABLE));
        editor.player()->setVolume(0.0);
        QCOMPARE(editor.timeline()->duration(), 15000);

        QTest::qWait(500); // media opened in background enter the preview graph first (measured separately)
        QElapsedTimer timer;
        qint64 worstEdit = 0;
        for (int i = 0; i < 10; ++i) {
            const Clip &clip = editor.data().mainSequence()->visualTracks.front().clips[250 + i];
            timer.start();
            QVERIFY(editor.trimClip(clip.id.toString(), false, static_cast<int>(clip.end().value()) - 5));
            worstEdit = std::max(worstEdit, timer.elapsed());
        }
        timer.start();
        const QByteArray bytes = projectjson::toBytes(editor.data());
        const qint64 serialization = timer.elapsed();
        qInfo("500 clips: worst edit %lld ms, serialization %lld ms (%lld KiB)", worstEdit, serialization,
              static_cast<long long>(bytes.size() / 1024));
#if defined(NDEBUG) && !defined(__SANITIZE_ADDRESS__)
        QVERIFY2(worstEdit < 50, qPrintable(QString::number(worstEdit)));
        QVERIFY2(serialization < 30, qPrintable(QString::number(serialization)));
#endif
    }

    void cleanupTestCase() {}
};

QTEST_MAIN(TestEditor)
#include "tst_editor.moc"
