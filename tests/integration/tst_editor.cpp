// SPDX-License-Identifier: GPL-3.0-or-later
// The Phase 1 criterion through the editor controller, as the interface drives it (SPEC §8): import 3 clips, cut
// and reorder them, export a correct MP4, close and reopen the identical draft without ever pressing "Save".
#include "../unit/ProjectFixture.h"
#include "TestMedia.h"

#include "core/serialization/ProjectJson.h"
#include "document/Document.h"
#include "document/DraftStore.h"
#include "engine/analysis/Decoding.h"
#include "engine/analysis/MediaAnalysis.h"
#include "engine/mlt/MltRuntime.h"
#include "fx/Library.h"
#include "ui/controllers/ActionRegistry.h"
#include "ui/controllers/AiController.h"
#include "ui/controllers/ClipInspector.h"
#include "ui/items/AssetThumbnail.h"
#include "ui/models/AssetLibraryModel.h"
#include "ui/models/BrandKitModel.h"
#include "ui/controllers/EditorController.h"

#include <QElapsedTimer>
#include <QImage>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTextStream>

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
                                                   u"cutout"_s, u"filter"_s, u"effects"_s, u"adjust"_s}));
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
        QCOMPARE(inspector.sections(), (QStringList{u"text"_s, u"video"_s, u"animation"_s, u"effects"_s}));
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
        QVERIFY(library.rowCount() >= 100);
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

    // The frame on screen saved as an image (SPEC §5.15): PNG in the folder asked for, never overwriting.
    void exportsTheFrameOnScreen()
    {
        document::DraftStore store(m_dir.filePath(u"drafts-frame"_s));
        engine::MediaAnalysis analysis(m_dir.filePath(u"cache-frame"_s));
        QString error;
        EditorController editor(store.createDraft(&error), analysis, QStringLiteral(VEDIT_RENDER_EXECUTABLE));
        editor.player()->setVolume(0.0);
        editor.importAndInsertPaths({m_files.landscape}, 0, editor.timeline()->mainRow());
        QTRY_COMPARE_WITH_TIMEOUT(mainTrack(editor).clips.size(), size_t(1), 20000);
        editor.player()->seek(30);
        QTRY_VERIFY_WITH_TIMEOUT(!editor.player()->sink()->latest().isNull(), 20000);

        const QString folder = m_dir.filePath(u"frames"_s);
        QVERIFY(QDir().mkpath(folder));
        const QString saved = editor.exportCurrentFrame(u"my frame"_s, folder);
        QVERIFY(!saved.isEmpty());
        QCOMPARE(saved, folder + u"/my frame.png"_s);
        QVERIFY(QFile(saved).size() > 0);
        // Never overwrites: the second one gets " (2)".
        const QString again = editor.exportCurrentFrame(u"my frame"_s, folder);
        QCOMPARE(again, folder + u"/my frame (2).png"_s);
        // A folder that does not exist: no crash, no file.
        QVERIFY(editor.exportCurrentFrame(u"x"_s, folder + u"/nope"_s).isEmpty());
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

    // "Split scenes" and "Remove pauses" on a clip with three shots and a second of silence in the middle.
    void aiToolsCutScenesAndPauses()
    {
        const QString file = m_dir.filePath(u"shots.mp4"_s);
        QVERIFY(runFfmpeg({u"-filter_complex"_s,
                           u"color=c=red:s=160x90:r=30:d=1[r];color=c=blue:s=160x90:r=30:d=1[b];"
                           "testsrc=s=160x90:r=30:d=1[t];[r][b][t]concat=n=3:v=1:a=0,format=yuv420p[v];"
                           "aevalsrc='if(between(t\\,1\\,2)\\,0\\,0.5*sin(2*PI*440*t))':s=48000:d=3[a]"_s,
                           u"-map"_s, u"[v]"_s, u"-map"_s, u"[a]"_s, u"-c:v"_s, u"libx264"_s, u"-c:a"_s, u"aac"_s, file}));
        document::DraftStore store(m_dir.filePath(u"drafts-ai"_s));
        engine::MediaAnalysis analysis(m_dir.filePath(u"cache"_s));
        QString error;
        EditorController editor(store.createDraft(&error), analysis, QStringLiteral(VEDIT_RENDER_EXECUTABLE));
        editor.player()->setVolume(0.0);
        editor.importAndInsertPaths({file}, 0, editor.timeline()->mainRow());
        QTRY_COMPARE_WITH_TIMEOUT(mainTrack(editor).clips.size(), size_t(1), 20000);
        editor.select(mainTrack(editor).clips[0].id.toString(), false);
        AiController &ai = *editor.ai();
        QVERIFY(ai.canSplitScenes() && ai.canRemovePauses());

        QVERIFY(ai.splitScenes());
        QVERIFY(ai.busy());
        QTRY_VERIFY_WITH_TIMEOUT(!ai.busy(), 20000);
        QCOMPARE(mainTrack(editor).clips.size(), size_t(3));
        QCOMPARE(mainTrack(editor).clips[1].start, RationalTime(30, Rational(30)));
        QCOMPARE(mainTrack(editor).clips[2].start, RationalTime(60, Rational(30)));
        editor.undo(); // one step
        QCOMPARE(mainTrack(editor).clips.size(), size_t(1));

        editor.select(mainTrack(editor).clips[0].id.toString(), false);
        const RationalTime before = mainTrack(editor).clips[0].duration;
        QVERIFY(ai.removePauses());
        QTRY_VERIFY_WITH_TIMEOUT(!ai.busy(), 20000);
        // The second of silence minus 0.15 s kept on each side: about 0.7 s (21 frames) shorter, in two pieces.
        QCOMPARE(mainTrack(editor).clips.size(), size_t(2));
        const RationalTime after = mainTrack(editor).clips[0].duration + mainTrack(editor).clips[1].duration;
        const std::int64_t removed = (before - after).value();
        QVERIFY2(removed >= 18 && removed <= 24, qPrintable(QString::number(removed)));
        QVERIFY(std::abs(mainTrack(editor).clips[1].media()->sourceIn.value() - 56) <= 2); // 1.85 s

        // A second run on the same file uses what was found (no new analysis).
        editor.undo();
        editor.select(mainTrack(editor).clips[0].id.toString(), false);
        QVERIFY(ai.removePauses());
        QVERIFY(!ai.busy());
        QCOMPARE(mainTrack(editor).clips.size(), size_t(2));

        // "Stabilize": the shake is measured, the clip gets the effect; how much is a slider, and it can be removed.
        editor.undo();
        editor.select(mainTrack(editor).clips[0].id.toString(), false);
        QVERIFY(ai.canStabilize());
        QVERIFY(ai.stabilize());
        QTRY_VERIFY_WITH_TIMEOUT(!ai.busy(), 20000);
        const Clip &steadied = mainTrack(editor).clips[0];
        const auto stabilize = std::find_if(steadied.effects.begin(), steadied.effects.end(),
                                            [](const Effect &e) { return e.type == u"vedit.stabilize"_s; });
        QVERIFY(stabilize != steadied.effects.end());
        QVERIFY(!std::get<QString>(stabilize->params.at(u"motion"_s).staticValue()).isEmpty());
        QCOMPARE(std::get<QString>(stabilize->params.at(u"motionRate"_s).staticValue()), u"30"_s);
        ClipInspector &inspector = *editor.inspector();
        QVERIFY(inspector.values().value(u"stabilize.on"_s).toBool());
        QVERIFY(inspector.set(u"stabilize.strength"_s, 0.9));
        inspector.endGesture();
        QCOMPARE(inspector.values().value(u"stabilize.strength"_s).toDouble(), 0.9);
        QVERIFY(inspector.set(u"stabilize.on"_s, false));
        QVERIFY(!inspector.values().value(u"stabilize.on"_s).toBool());
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
        QCOMPARE(ids(), (QStringList{u"split"_s, u"rippleTrimLeft"_s, u"rippleTrimRight"_s, u"freeze"_s, u"addText"_s,
                                     u"addAudio"_s, u"captions"_s}));
        editor.select(mainTrack(editor).clips[0].id.toString(), false);
        QCOMPARE(ids(), (QStringList{u"split"_s, u"rippleTrimLeft"_s, u"rippleTrimRight"_s, u"delete"_s, u"duplicate"_s,
                                     u"speed"_s, u"volume"_s, u"animation"_s, u"freeze"_s, u"reverse"_s, u"mirror"_s,
                                     u"rotate"_s, u"enhance"_s, u"removePauses"_s, u"splitScenes"_s, u"stabilize"_s, u"replace"_s}));
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

    // Keyframes (SPEC §5.6, the Phase 3 criterion): a diamond at the playhead, an animated parameter gets keyframes
    // when changed, easing, jumping from one to the next, removing the last leaves its value.
    void keyframes()
    {
        document::DraftStore store(m_dir.filePath(u"drafts-keyframes"_s));
        engine::MediaAnalysis analysis(m_dir.filePath(u"cache"_s));
        QString error;
        EditorController editor(store.createDraft(&error), analysis, QStringLiteral(VEDIT_RENDER_EXECUTABLE));
        editor.player()->setVolume(0.0);
        ClipInspector &inspector = *editor.inspector();
        editor.importAndInsertPaths({m_files.landscape, m_files.photo}, 0, editor.timeline()->mainRow());
        QTRY_COMPARE_WITH_TIMEOUT(mainTrack(editor).clips.size(), size_t(2), 20000);
        const ClipId clip = mainTrack(editor).clips.front().id;
        editor.select(clip.toString(), false);
        const auto value = [&inspector](const char *key) { return inspector.values().value(QString::fromLatin1(key)); };
        const auto seek = [&editor](int frame) {
            editor.player()->seek(frame);
            QTRY_COMPARE(editor.player()->position(), frame);
        };

        seek(0);
        QVERIFY(value("kf.available").toBool());
        QCOMPARE(value("kf.opacity").toInt(), 0);
        QVERIFY(inspector.toggleKeyframe(u"opacity"_s));
        QCOMPARE(value("kf.opacity").toInt(), 2);
        seek(30);
        QCOMPARE(value("kf.opacity").toInt(), 1);
        // Changing an animated value adds a keyframe where the playhead is.
        QVERIFY(inspector.set(u"opacity"_s, 0.2));
        inspector.endGesture();
        QCOMPARE(value("kf.opacity").toInt(), 2);
        QCOMPARE(inspector.keyframes(), (QVariantList{0, 30}));
        seek(15);
        QVERIFY(std::abs(value("opacity").toDouble() - 0.6) < 0.01); // linear half way
        // Easing of the movement from the keyframe at 0: "hold" keeps the value until the next one.
        seek(0);
        QVERIFY(inspector.setKeyframeEasing(u"hold"_s));
        QCOMPARE(value("kf.easing").toString(), u"hold"_s);
        seek(15);
        QCOMPARE(value("opacity").toDouble(), 1.0);
        seek(0);
        QVERIFY(inspector.setKeyframeEasing(u"easeInOut"_s));
        QCOMPARE(value("kf.easing").toString(), u"easeInOut"_s);
        QCOMPARE(value("kf.curve").toList(), (QVariantList{0.42, 0.0, 0.58, 1.0}));
        // A custom curve: fast at first (half way in time, most of the way done).
        QVERIFY(inspector.setKeyframeCurve(0.0, 0.9, 0.3, 1.0));
        inspector.endGesture();
        QCOMPARE(value("kf.easing").toString(), u"custom"_s);
        QVERIFY(value("kf.here").toBool());
        seek(15);
        QVERIFY(!value("kf.here").toBool());
        QVERIFY2(value("opacity").toDouble() < 0.35, qPrintable(value("opacity").toString())); // from 1 to 0.2: mostly done
        seek(0);
        // Jumping between keyframes.
        inspector.jumpKeyframe(1);
        QTRY_COMPARE(editor.player()->position(), 30);
        inspector.jumpKeyframe(-1);
        QTRY_COMPARE(editor.player()->position(), 0);
        // Outside the clip (on the photo) there is no playhead keyframe.
        seek(150);
        QVERIFY(!value("kf.available").toBool());
        QVERIFY(!inspector.toggleKeyframe(u"opacity"_s));
        // Removing the keyframes: the last one leaves its value.
        seek(30);
        QVERIFY(inspector.toggleKeyframe(u"opacity"_s));
        seek(0);
        QVERIFY(inspector.toggleKeyframe(u"opacity"_s));
        QVERIFY(!editor.data().findClip(clip)->opacity.isAnimated());
        QCOMPARE(std::get<double>(editor.data().findClip(clip)->opacity.staticValue()), 1.0);
        QVERIFY(inspector.keyframes().isEmpty());
        // Position: x and y share one diamond.
        QVERIFY(inspector.toggleKeyframe(u"position"_s));
        seek(60);
        QVERIFY(inspector.set(u"x"_s, 0.25));
        inspector.endGesture();
        seek(30);
        QVERIFY(std::abs(value("x").toDouble() - 0.125) < 0.01);
        QCOMPARE(value("y").toDouble(), 0.0);
        QVERIFY(editor.close());
    }

    // Cutout (Phase 3): a mask from the canvas, a colour picked on the clip and removed, blend modes, grouping.
    void cutoutBlendAndGrouping()
    {
        document::DraftStore store(m_dir.filePath(u"drafts-cutout"_s));
        engine::MediaAnalysis analysis(m_dir.filePath(u"cache"_s));
        QString error;
        EditorController editor(store.createDraft(&error), analysis, QStringLiteral(VEDIT_RENDER_EXECUTABLE));
        editor.player()->setVolume(0.0);
        ClipInspector &inspector = *editor.inspector();
        editor.importAndInsertPaths({m_files.landscape, m_files.photo}, 0, editor.timeline()->mainRow());
        QTRY_COMPARE_WITH_TIMEOUT(mainTrack(editor).clips.size(), size_t(2), 20000);
        const ClipId video = mainTrack(editor).clips.front().id;
        editor.select(video.toString(), false);
        QVERIFY(inspector.sections().contains(u"cutout"_s));
        const auto value = [&inspector](const char *key) { return inspector.values().value(QString::fromLatin1(key)); };

        // A circle mask, moved and resized from its box on the canvas (320×180): the picture fills it.
        QVERIFY(inspector.maskBox().isEmpty());
        QVERIFY(inspector.set(u"mask.shape"_s, 2));
        QCOMPARE(value("mask.shape").toInt(), 2);
        QVariantMap box = inspector.maskBox();
        QCOMPARE(box.value(u"x"_s).toDouble(), 160.0);
        QCOMPARE(box.value(u"width"_s).toDouble(), 160.0);
        QVERIFY(inspector.setMaskGeometry(200, 90, 80, 90));
        inspector.endGesture();
        QVERIFY(std::abs(value("mask.x").toDouble() - 0.125) < 1e-6);
        QVERIFY(std::abs(value("mask.width").toDouble() - 0.25) < 1e-6);
        QVERIFY(std::abs(value("mask.height").toDouble() - 0.5) < 1e-6);
        box = inspector.maskBox();
        QCOMPARE(box.value(u"x"_s).toDouble(), 200.0);
        QVERIFY(inspector.set(u"mask.feather"_s, 0.3));
        QVERIFY(inspector.set(u"mask.invert"_s, true));
        QVERIFY(editor.data().findClip(video)->masks.front().invert);
        QVERIFY(inspector.modifiedSections().contains(u"cutout"_s));

        // Picking a colour on the picture: the red bar at the bottom left of the test pattern.
        editor.player()->seek(10);
        QTRY_COMPARE(editor.player()->position(), 10);
        inspector.setCanvasMode(u"pick"_s);
        QVERIFY(inspector.pickKeyColor(15, 150));
        QCOMPARE(inspector.canvasMode(), QString());
        QVERIFY(value("chroma.enabled").toBool());
        const QColor key = value("chroma.color").value<QColor>();
        QVERIFY2(key.red() > 150 && key.green() < 100 && key.blue() < 100, qPrintable(key.name()));
        QVERIFY(!inspector.pickKeyColor(-50, 90)); // outside the clip
        QVERIFY(inspector.set(u"chroma.similarity"_s, 0.6));
        inspector.endGesture();
        QVERIFY(inspector.reset(u"cutout"_s));
        QVERIFY(editor.data().findClip(video)->masks.empty());
        QVERIFY(!value("chroma.enabled").toBool());

        // Blend mode.
        QVERIFY(inspector.set(u"blend"_s, int(BlendMode::Screen)));
        QCOMPARE(editor.data().findClip(video)->blendMode, BlendMode::Screen);

        // Grouping into a compound clip and back; an adjustment layer.
        ActionRegistry &actions = *editor.actions();
        editor.select(mainTrack(editor).clips[0].id.toString(), false);
        editor.select(mainTrack(editor).clips[1].id.toString(), true);
        QVERIFY(actions.trigger(u"createCompound"_s));
        QCOMPARE(mainTrack(editor).clips.size(), size_t(1));
        editor.select(mainTrack(editor).clips[0].id.toString(), false);
        QVERIFY(actions.isEnabled(u"expandCompound"_s));
        QVERIFY(actions.trigger(u"expandCompound"_s));
        QCOMPARE(mainTrack(editor).clips.size(), size_t(2));
        editor.clearSelection();
        const size_t tracks = editor.data().mainSequence()->visualTracks.size();
        QVERIFY(actions.trigger(u"addAdjustment"_s));
        QCOMPARE(editor.data().mainSequence()->visualTracks.size(), tracks + 1);
        // Selected, it offers its looks: a filter, effects and adjustments for everything under it.
        QCOMPARE(inspector.kind(), int(ClipInspector::Adjustment));
        QCOMPARE(inspector.sections(), (QStringList{u"filter"_s, u"effects"_s, u"adjust"_s}));
        QVERIFY(actions.toolbar().first().toMap().value(u"id"_s) == u"split"_s);
        QVERIFY(inspector.toggleFilter(u"filters/bw"_s));
        QCOMPARE(value("filter").toString(), u"filters/bw"_s);
        QVERIFY(inspector.set(u"adjust.contrast"_s, 0.3));
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

    void audioProcessingAndLoudness()
    {
        document::DraftStore store(m_dir.filePath(u"drafts-audio"_s));
        engine::MediaAnalysis analysis(m_dir.filePath(u"cache"_s));
        QString error;
        EditorController editor(store.createDraft(&error), analysis, QStringLiteral(VEDIT_RENDER_EXECUTABLE));
        editor.player()->setVolume(0.0);
        ClipInspector &inspector = *editor.inspector();
        editor.importAndInsertPaths({m_files.landscape}, 0, editor.timeline()->mainRow());
        QTRY_COMPARE_WITH_TIMEOUT(mainTrack(editor).clips.size(), size_t(1), 20000);
        const QString clipId = mainTrack(editor).clips[0].id.toString();

        editor.select(clipId, false);
        QVERIFY(inspector.active());

        // 1. Volume keyframing
        QCOMPARE(inspector.values().value(u"volume"_s).toDouble(), 0.0);
        inspector.toggleKeyframe(u"volume"_s);
        QCOMPARE(inspector.keyframes().size(), 1);

        editor.player()->seek(15);
        inspector.set(u"volume"_s, -6.0);
        inspector.endGesture();
        QCOMPARE(inspector.keyframes().size(), 2);

        // 2. Audio effects: Denoise, Voice Effect, EQ, Compressor
        inspector.set(u"audio.denoise"_s, true);
        inspector.set(u"audio.denoiseAmount"_s, 0.8);
        inspector.set(u"audio.voiceEffect"_s, u"robot"_s);
        inspector.set(u"audio.eq.low"_s, 3.0);
        inspector.set(u"audio.compressor.enabled"_s, true);
        inspector.endGesture();

        QCOMPARE(inspector.values().value(u"audio.denoise"_s).toBool(), true);
        QCOMPARE(inspector.values().value(u"audio.denoiseAmount"_s).toDouble(), 0.8);
        QCOMPARE(inspector.values().value(u"audio.voiceEffect"_s).toString(), u"robot"_s);
        QCOMPARE(inspector.values().value(u"audio.eq.low"_s).toDouble(), 3.0);
        QCOMPARE(inspector.values().value(u"audio.compressor.enabled"_s).toBool(), true);

        // 3. Loudness normalization to -14 LUFS
        inspector.normalizeLoudness(-14.0);
        QVERIFY(inspector.values().contains(u"volume"_s));

        // 4. Reset audio
        inspector.reset(u"audio"_s);
        QCOMPARE(inspector.values().value(u"volume"_s).toDouble(), 0.0);
        QCOMPARE(inspector.values().value(u"audio.denoise"_s).toBool(), false);
        QCOMPARE(inspector.values().value(u"audio.voiceEffect"_s).toString(), u"none"_s);
    }

    void recordingAndTeleprompter()
    {
        document::DraftStore store(m_dir.filePath(u"drafts-record"_s));
        engine::MediaAnalysis analysis(m_dir.filePath(u"cache"_s));
        QString error;
        EditorController editor(store.createDraft(&error), analysis, QStringLiteral(VEDIT_RENDER_EXECUTABLE));
        editor.player()->setVolume(0.0);
        RecordController *recorder = editor.recorder();
        QVERIFY(recorder);
        recorder->setTestMode(true);

        // 1. Open in voiceover mode
        recorder->open(RecordController::VoiceOver);
        QVERIFY(recorder->isActive());
        QCOMPARE(recorder->mode(), int(RecordController::VoiceOver));

        // 2. Teleprompter setup
        recorder->setTeleprompterVisible(true);
        recorder->setTeleprompterText(u"Benvenuti a questa dimostrazione di vedit."_s);
        recorder->setTeleprompterSpeed(120.0);
        recorder->setTeleprompterMirrored(true);
        QCOMPARE(recorder->teleprompterText(), u"Benvenuti a questa dimostrazione di vedit."_s);
        QCOMPARE(recorder->teleprompterSpeed(), 120.0);
        QCOMPARE(recorder->teleprompterMirrored(), true);

        // 3. Start recording and verify countdown
        recorder->startCountdown();
        QCOMPARE(recorder->status(), int(RecordController::CountingDown));
        QCOMPARE(recorder->countdown(), 3);

        // Advance to recording
        recorder->startRecording();
        QCOMPARE(recorder->status(), int(RecordController::Recording));
        QVERIFY(recorder->isRecording());
        QVERIFY(!recorder->isPaused());

        // Pause and resume
        recorder->pauseRecording();
        QVERIFY(recorder->isPaused());
        recorder->resumeRecording();
        QVERIFY(!recorder->isPaused());

        // Stop and save
        recorder->stopRecording();
        QCOMPARE(recorder->status(), int(RecordController::Idle));
        QVERIFY(!recorder->isActive());
        QVERIFY(!recorder->lastSavedPath().isEmpty());
        QVERIFY(QFile::exists(recorder->lastSavedPath()));

        // Check that the clip was inserted on the audio track
        QTRY_VERIFY_WITH_TIMEOUT(!editor.data().mainSequence()->audioTracks.empty(), 10000);
        const auto &audioTrack = editor.data().mainSequence()->audioTracks.front();
        QTRY_VERIFY_WITH_TIMEOUT(!audioTrack.clips.empty(), 10000);
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

    // The Phase 4 criterion (SPEC §8):
    // "Correggo colore con LUT e curve, il mix audio rispetta −14 LUFS, e monto un'intervista a due camere sincronizzate dall'audio."
    void phaseFourCriterion()
    {
        document::DraftStore store(m_dir.filePath(u"drafts-phase4"_s));
        engine::MediaAnalysis analysis(m_dir.filePath(u"cache"_s));
        QString error;
        auto editor = std::make_unique<EditorController>(store.createDraft(&error), analysis,
                                                         QStringLiteral(VEDIT_RENDER_EXECUTABLE));
        editor->player()->setVolume(0.0);
        ClipInspector &inspector = *editor->inspector();

        // 1. Prepare two camera clips for the interview: Camera 1 and Camera 2
        const QString cam1 = m_files.landscape;
        const QString cam2 = m_dir.filePath(u"media/cam2.mp4"_s);
        QFile::remove(cam2);
        QVERIFY(QFile::copy(cam1, cam2));

        // Import both cameras onto the timeline
        editor->importAndInsertPaths({cam1, cam2}, 0, editor->timeline()->mainRow());
        QTRY_COMPARE_WITH_TIMEOUT(mainTrack(*editor).clips.size(), size_t(2), 20000);
        const QString cam1ClipId = mainTrack(*editor).clips[0].id.toString();
        const QString cam2ClipId = mainTrack(*editor).clips[1].id.toString();

        // 2. Select both camera clips and create a multicam clip synchronized by audio waveform
        editor->select(cam1ClipId, false);
        editor->select(cam2ClipId, true);
        QCOMPARE(editor->selectedClips().size(), size_t(2));
        QVERIFY(editor->createMulticamFromSelection(u"Interview Multicam"_s));

        // Verify multicam compound clip created with master audio and 2 angles
        QTRY_COMPARE(mainTrack(*editor).clips.size(), size_t(1));
        const Clip &multicamClip = mainTrack(*editor).clips.front();
        QVERIFY(multicamClip.compound() != nullptr);
        QCOMPARE(multicamClip.compound()->activeAngle, 0);

        const Sequence *nested = editor->data().findSequence(multicamClip.compound()->sequenceId);
        QVERIFY(nested != nullptr);
        QCOMPARE(nested->visualTracks.size(), size_t(2));
        QCOMPARE(nested->audioTracks.size(), size_t(1));

        // 3. Multicam editing: switch angles and cut at playhead
        // Frame 60: cut and switch to Angle 2 (index 1)
        editor->player()->seek(60);
        QVERIFY(editor->switchMulticamAngle(1));
        QCOMPARE(mainTrack(*editor).clips.size(), size_t(2));

        const Clip &part1 = mainTrack(*editor).clips[0];
        const Clip &part2 = mainTrack(*editor).clips[1];
        QVERIFY(part1.compound() != nullptr && part2.compound() != nullptr);
        QCOMPARE(part1.compound()->activeAngle, 0);
        QCOMPARE(part1.duration.value(), 60);
        QCOMPARE(part2.compound()->activeAngle, 1);
        QCOMPARE(part2.start.value(), 60);
        QCOMPARE(part2.compound()->sourceIn.value(), 60);

        // Angle shortcut keys (1..9)
        editor->select(part2.id.toString(), false);
        editor->multicamAngleKey(1); // switch back to angle 1 (index 0)
        QCOMPARE(mainTrack(*editor).clips[1].compound()->activeAngle, 0);
        editor->multicamAngleKey(2); // switch to angle 2 (index 1)
        QCOMPARE(mainTrack(*editor).clips[1].compound()->activeAngle, 1);

        // 4. Color grading with .cube LUT and RGB curves
        // Create a custom .cube LUT file
        const QString lutPath = m_dir.filePath(u"interview_grade.cube"_s);
        {
            QFile lutFile(lutPath);
            QVERIFY(lutFile.open(QIODevice::WriteOnly | QIODevice::Text));
            QTextStream out(&lutFile);
            out << "TITLE \"Interview Grade\"\n"
                << "LUT_3D_SIZE 2\n"
                << "0.0 0.0 0.0\n"
                << "1.0 0.0 0.0\n"
                << "0.0 1.0 0.0\n"
                << "1.0 1.0 0.0\n"
                << "0.0 0.0 1.0\n"
                << "1.0 0.0 1.0\n"
                << "0.0 1.0 1.0\n"
                << "1.0 1.0 1.0\n";
        }
        QVERIFY(QFile::exists(lutPath));

        // Select the first angle clip and apply LUT
        editor->select(part1.id.toString(), false);
        QVERIFY(inspector.active());
        QVERIFY(inspector.set(u"lut.path"_s, lutPath));
        QVERIFY(inspector.set(u"lut.intensity"_s, 0.85));
        inspector.endGesture();
        QCOMPARE(inspector.values().value(u"lut.path"_s).toString(), lutPath);
        QCOMPARE(inspector.values().value(u"lut.intensity"_s).toDouble(), 0.85);

        // Apply RGB curves and color wheels/balance
        const QVariantList curvePoints{
            QVariantList{0.0, 0.0},
            QVariantList{0.4, 0.5},
            QVariantList{1.0, 1.0}
        };
        QVERIFY(inspector.set(u"grade.curve.master"_s, curvePoints));
        QVERIFY(inspector.set(u"grade.balance.r"_s, 1.1));
        QVERIFY(inspector.set(u"grade.midtones.level"_s, 0.2));
        QVERIFY(inspector.set(u"grade.hsl.red.saturation"_s, 0.3));
        QVERIFY(inspector.set(u"adjust.temperature"_s, 10.0));
        inspector.endGesture();

        const QVariantList retrievedPoints = inspector.values().value(u"grade.curve.master"_s).toList();
        QCOMPARE(retrievedPoints.size(), 3);
        QCOMPARE(inspector.values().value(u"grade.balance.r"_s).toDouble(), 1.1);
        QCOMPARE(inspector.values().value(u"grade.midtones.level"_s).toDouble(), 0.2);
        QCOMPARE(inspector.values().value(u"grade.hsl.red.saturation"_s).toDouble(), 0.3);
        QCOMPARE(inspector.values().value(u"adjust.temperature"_s).toDouble(), 10.0);

        // 5. Export with audio mix normalized to -14 LUFS (EBU R128)
        const QVariantMap defaults = editor->exportDefaults();
        const QString folder = m_dir.filePath(u"videos-phase4"_s);
        QDir().mkpath(folder);
        QSignalSpy exported(editor.get(), &EditorController::exportFinished);
        const int res = defaults.value(u"resolution"_s).toInt();
        QVERIFY(editor->startExport(u"interview_multicam"_s, folder, res, u"30"_s, 1, true, -14.0));
        QVERIFY(exported.wait(60000));
        const QString output = exported.first().first().toString();
        QVERIFY(QFile::exists(output));

        // 6. Verify export specifications and -14 LUFS loudness
        const QJsonObject probe = ffprobe(output);
        QCOMPARE(streamOfType(probe, u"video"_s).value(u"codec_name"_s).toString(), u"h264"_s);
        QCOMPARE(streamOfType(probe, u"audio"_s).value(u"codec_name"_s).toString(), u"aac"_s);

        const auto loudness = engine::extractLoudness(output);
        QVERIFY(loudness.has_value());
        QVERIFY2(std::abs(loudness->integratedLufs - (-14.0)) <= 0.6,
                 qPrintable(QStringLiteral("Loudness was %1 LUFS, expected -14.0 +/- 0.6")
                                .arg(loudness->integratedLufs)));

        // 7. Continuous save: close and reopen identical without manual save
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

    void rippleTrimShortcuts()
    {
        document::DraftStore store(m_dir.filePath(u"drafts-ripple"_s));
        engine::MediaAnalysis analysis(m_dir.filePath(u"cache-ripple"_s));
        QString error;
        auto editor = std::make_unique<EditorController>(store.createDraft(&error), analysis,
                                                         QStringLiteral(VEDIT_RENDER_EXECUTABLE));
        editor->player()->setVolume(0.0);
        editor->importAndInsertPaths({m_files.landscape, m_files.photo}, 0, editor->timeline()->mainRow());
        QTRY_COMPARE_WITH_TIMEOUT(mainTrack(*editor).clips.size(), size_t(2), 20000);

        // Landscape: 0..120, Photo: 120..210
        // Test Q (rippleTrimLeft) at frame 30
        editor->player()->seek(30);
        QVERIFY(editor->canRippleTrimLeft());
        QVERIFY(editor->rippleTrimLeft());
        // Clip 0 trimmed: duration 90 (starts at 0), playhead moved to cut point 0
        QCOMPARE(mainTrack(*editor).clips[0].duration.value(), 90);
        QCOMPARE(editor->player()->position(), 0);
        QCOMPARE(mainTrack(*editor).clips[1].start.value(), 90);

        // Undo
        editor->undo();
        QCOMPARE(mainTrack(*editor).clips[0].duration.value(), 120);
        QCOMPARE(mainTrack(*editor).clips[1].start.value(), 120);

        // Test W (rippleTrimRight) at frame 70
        editor->player()->seek(70);
        QVERIFY(editor->canRippleTrimRight());
        QVERIFY(editor->rippleTrimRight());
        // Clip 0 trimmed: duration 70, clip 1 shifted left to 70
        QCOMPARE(mainTrack(*editor).clips[0].duration.value(), 70);
        QCOMPARE(mainTrack(*editor).clips[1].start.value(), 70);

        // Test ActionRegistry triggers for Q and W
        editor->undo();
        editor->player()->seek(50);
        QVERIFY(editor->actions()->trigger(u"rippleTrimLeft"_s));
        QCOMPARE(mainTrack(*editor).clips[0].duration.value(), 70);
        editor->undo();
        editor->player()->seek(80);
        QVERIFY(editor->actions()->trigger(u"rippleTrimRight"_s));
        QCOMPARE(mainTrack(*editor).clips[0].duration.value(), 80);
    }

    // SPEC §8, Phase 5 criterion (first half): "I use a template, replace the media and get a complete video".
    void phaseFiveCriterionTemplate()
    {
        document::DraftStore store(m_dir.filePath(u"drafts-template"_s));
        engine::MediaAnalysis analysis(m_dir.filePath(u"cache"_s));
        QString error;
        EditorController editor(store.createDraft(&error), analysis, QStringLiteral(VEDIT_RENDER_EXECUTABLE));
        editor.player()->setVolume(0.0);
        const fx::TemplatePreset *preset = fx::Library::core().templatePreset(u"templates/vlog-day"_s);
        QVERIFY(preset);
        QVERIFY(editor.applyTemplate(preset->spec, preset->name.text()));

        // The template's project: its format, a slot per shot with the filter, transitions on every cut, its title
        // and its progress bar; nothing to undo, the first slot selected.
        const int shots = static_cast<int>(preset->spec.value(u"slots"_s).toArray().size());
        QCOMPARE(editor.placeholderCount(), shots);
        QCOMPARE(editor.canvasPreset(), int(CanvasPreset::Portrait9x16));
        QCOMPARE(editor.data().name, preset->name.text());
        QVERIFY(!editor.canUndo());
        QCOMPARE(editor.selection(), QStringList{editor.firstPlaceholder()});
        const Sequence &sequence = *editor.data().mainSequence();
        QCOMPARE(static_cast<int>(sequence.visualTracks.front().clips.size()), shots);
        QCOMPARE(static_cast<int>(sequence.visualTracks.front().transitions.size()), shots - 1);
        for (const Clip &clip : sequence.visualTracks.front().clips) {
            QVERIFY(clip.placeholder);
            QCOMPARE(clip.transform.fit, FitMode::Cover); // the shots fill the template's format
            QVERIFY(std::any_of(clip.effects.begin(), clip.effects.end(),
                                [](const Effect &e) { return e.type == u"vedit.filter"_s && e.preset && e.preset->id == u"filters/honey"_s; }));
        }
        bool title = false;
        bool progress = false;
        for (const Track &track : sequence.visualTracks) {
            for (const Clip &clip : track.clips) {
                // The title in the language of the interface (English or Italian).
                const QJsonObject written = preset->spec.value(u"texts"_s).toArray().first().toObject().value(u"text"_s).toObject();
                title = title || (clip.text() && (clip.text()->text == written.value(u"en"_s).toString()
                                                  || clip.text()->text == written.value(u"it"_s).toString()));
                progress = progress || (clip.sticker() && clip.sticker()->graphic);
            }
        }
        QVERIFY(title);
        QVERIFY(progress);

        // "+" on a media item fills the selected slot, and the next slot is selected.
        editor.importFiles({QUrl::fromLocalFile(m_files.landscape)});
        QTRY_COMPARE_WITH_TIMEOUT(editor.data().media.size(), size_t(1), 20000);
        QVERIFY(editor.addMedia(editor.data().media.front().id.toString()));
        QCOMPARE(editor.placeholderCount(), shots - 1);
        QCOMPARE(editor.selection(), QStringList{editor.firstPlaceholder()});

        // The other shots at once, in order (what the template asks for when it opens).
        const QStringList files{m_files.vertical, m_files.photo, m_files.landscape, m_files.vertical};
        QList<QUrl> urls;
        for (const QString &file : files) {
            urls << QUrl::fromLocalFile(file);
        }
        urls << QUrl::fromLocalFile(m_files.music); // music goes under the video, not in a slot
        editor.fillPlaceholders(urls);
        QTRY_COMPARE_WITH_TIMEOUT(editor.placeholderCount(), 0, 30000);
        QTRY_COMPARE_WITH_TIMEOUT(editor.data().mainSequence()->audioTracks.size(), size_t(1), 30000);
        const Track &main = editor.data().mainSequence()->visualTracks.front();
        QCOMPARE(static_cast<int>(main.clips.size()), shots);
        // The look stayed: filter and transitions are still there on the real media.
        QCOMPARE(static_cast<int>(main.transitions.size()), shots - 1);
        for (const Clip &clip : main.clips) {
            QVERIFY(clip.media());
            QVERIFY(std::any_of(clip.effects.begin(), clip.effects.end(), [](const Effect &e) { return e.type == u"vedit.filter"_s; }));
        }
        QCOMPARE(editor.data().mainSequence()->audioTracks.front().clips.front().start.value(), 0);

        // A cover for the video (SPEC §5.13ter): it goes into the exported file.
        QSignalSpy cover(&editor, &EditorController::coverChanged);
        QVERIFY(editor.setCoverFromImage(QUrl::fromLocalFile(m_files.photo)));
        QCOMPARE(cover.count(), 1);
        QVERIFY(!editor.coverUrl().isEmpty());

        // And it is a complete video: exported, the right size and length, with sound.
        const QString folder = m_dir.filePath(u"videos-template"_s);
        QDir().mkpath(folder);
        QSignalSpy exported(&editor, &EditorController::exportFinished);
        QVERIFY(editor.startExport(u"template"_s, folder, 180, u"30"_s, 1));
        QVERIFY(exported.wait(120000));
        const QJsonObject probe = ffprobe(exported.first().first().toString());
        QCOMPARE(streamOfType(probe, u"video"_s).value(u"width"_s).toInt(), 180);
        QCOMPARE(streamOfType(probe, u"video"_s).value(u"height"_s).toInt(), 320);
        QCOMPARE(streamOfType(probe, u"video"_s).value(u"nb_read_frames"_s).toString().toInt(), editor.timeline()->duration());
        QCOMPARE(streamOfType(probe, u"audio"_s).value(u"codec_name"_s).toString(), u"aac"_s);
        bool attachedCover = false;
        for (const QJsonValue &stream : probe.value(u"streams"_s).toArray()) {
            attachedCover = attachedCover || stream.toObject().value(u"disposition"_s).toObject().value(u"attached_pic"_s).toInt() == 1;
        }
        QVERIFY(attachedCover);
        // The cover can also be saved as a picture, in YouTube's size.
        const QString thumbnail = editor.exportCover(folder, true);
        QVERIFY(!thumbnail.isEmpty());
        QCOMPARE(QImage(thumbnail).size(), QSize(1280, 720));
    }

    // "Slideshow from photos" (SPEC §5.13bis): the photos in order with a camera move each, the style's transition
    // and filter, the music under them cut to their length with a fade; nothing to undo.
    void slideshowFromPhotos()
    {
        document::DraftStore store(m_dir.filePath(u"drafts-slideshow"_s));
        engine::MediaAnalysis analysis(m_dir.filePath(u"cache"_s));
        QString error;
        // One editor at a time, as in the application (two SDL audio consumers closing crash inside SDL).
        auto first = std::make_unique<EditorController>(store.createDraft(&error), analysis, QStringLiteral(VEDIT_RENDER_EXECUTABLE));
        EditorController &editor = *first;
        editor.player()->setVolume(0.0);
        QList<QUrl> photos;
        const QColor colours[] = {Qt::red, Qt::green, Qt::blue};
        for (int i = 0; i < 3; ++i) {
            QImage image(320, 240, QImage::Format_RGB32);
            image.fill(colours[i]);
            const QString path = m_dir.filePath(u"slide-%1.png"_s.arg(i));
            QVERIFY(image.save(path));
            photos << QUrl::fromLocalFile(path);
        }
        QSignalSpy built(&editor, &EditorController::slideshowChanged);
        editor.buildSlideshow(photos, QUrl::fromLocalFile(m_files.music), 1, false); // dynamic: 2 s, push, vivid
        QVERIFY(editor.buildingSlideshow());
        QTRY_VERIFY_WITH_TIMEOUT(!editor.buildingSlideshow(), 30000);

        const Track &main = editor.data().mainSequence()->visualTracks.front();
        QCOMPARE(main.clips.size(), size_t(3));
        QStringList moves;
        for (const Clip &clip : main.clips) {
            QCOMPARE(clip.duration.value(), 60); // 2 s at 30 fps
            QCOMPARE(clip.transform.fit, FitMode::Cover);
            QVERIFY(clip.animations.loop);
            moves << clip.animations.loop->type.id;
            QVERIFY(std::any_of(clip.effects.begin(), clip.effects.end(),
                                [](const Effect &e) { return e.preset && e.preset->id == u"filters/vivid"_s; }));
        }
        QCOMPARE(moves.removeDuplicates(), 0); // a different move for every photo
        QCOMPARE(main.transitions.size(), size_t(2));
        QCOMPARE(main.transitions.front().type.id, u"transitions/push-left"_s);
        // The song (6 s) is as long as the photos and fades out.
        const auto &audio = editor.data().mainSequence()->audioTracks;
        QCOMPARE(audio.size(), size_t(1));
        const Clip &song = audio.front().clips.front();
        QCOMPARE(song.start.value(), 0);
        QCOMPARE(song.end(), main.clips.back().end());
        QVERIFY(song.media()->audio.fadeOut);
        QVERIFY(!editor.canUndo());
        first.reset();

        // On the beat: with clicks every 0.7 s, a 3 s photo ends on the beat nearest to 3 s (2.8 s), and so on.
        const QString clicks = m_dir.filePath(u"clicks.wav"_s);
        QVERIFY(runFfmpeg({u"-f"_s, u"lavfi"_s, u"-i"_s, u"aevalsrc='if(lt(mod(t\\,0.7)\\,0.03)\\,sin(2*PI*1500*t)\\,0)':s=44100:d=12"_s,
                           clicks}));
        EditorController beat(store.createDraft(&error), analysis, QStringLiteral(VEDIT_RENDER_EXECUTABLE));
        beat.player()->setVolume(0.0);
        beat.buildSlideshow(photos, QUrl::fromLocalFile(clicks), 0, true); // soft: 3 s, on the beat
        QTRY_VERIFY_WITH_TIMEOUT(!beat.buildingSlideshow(), 60000);
        const Track &onBeat = beat.data().mainSequence()->visualTracks.front();
        QCOMPARE(onBeat.clips.size(), size_t(3));
        for (const Clip &clip : onBeat.clips) {
            const double end = clip.end().toSecondsDouble();
            QVERIFY2(std::abs(end / 0.7 - std::round(end / 0.7)) < 0.06, qPrintable(u"cut at %1 s"_s.arg(end)));
        }
    }

    // Brand kits (SPEC §5.13ter): saved once, kept between sessions, used with a click; their colours first in every
    // colour picker.
    void brandKit()
    {
        const QString data = qEnvironmentVariable("XDG_DATA_HOME");
        if (data.isEmpty() || !data.contains(u"/test-home/"_s)) {
            QSKIP("run through CTest or tools/run-test.sh (XDG_DATA_HOME inside the build tree)");
        }
        QDir(BrandKitModel::folder()).removeRecursively();
        {
            BrandKitModel kits;
            QCOMPARE(kits.rowCount(), 0);
            QCOMPARE(kits.createKit(u"Channel"_s), 0);
            kits.addColor(QColor(u"#ff5500"_s));
            kits.addColor(QColor(u"#112233"_s));
            kits.addColor(QColor(u"#ff5500"_s)); // once
            kits.addFont(u"Inter"_s);
            QCOMPARE(kits.addLogo(QUrl::fromLocalFile(m_files.photo)), QString());
            QCOMPARE(kits.setIntro(QUrl::fromLocalFile(m_files.vertical)), QString());
            QCOMPARE(kits.setOutro(QUrl::fromLocalFile(m_files.landscape)), QString());
            QCOMPARE(kits.addMusic(QUrl::fromLocalFile(m_files.music)), QString());
            QCOMPARE(kits.createKit(u"Work"_s), 1); // a second kit, now the one in use
            QCOMPARE(kits.current(), 1);
            kits.setCurrent(0);
        }
        // Another session: the kits, their files (copies inside the kit) and the kit in use are there.
        BrandKitModel kits;
        BrandKitModel::setInstance(&kits);
        QCOMPARE(kits.rowCount(), 2);
        QCOMPARE(kits.name(), u"Channel"_s);
        QCOMPARE(kits.colors(), (QVariantList{QColor(u"#ff5500"_s), QColor(u"#112233"_s)}));
        QCOMPARE(kits.logos().size(), 1);
        QVERIFY(kits.logos().front().toUrl().toLocalFile().startsWith(BrandKitModel::folder()));
        QVERIFY(QFileInfo::exists(kits.intro().toLocalFile()));
        QCOMPARE(kits.music().size(), 1);
        // Deleted, then brought back by "Undo".
        kits.setCurrent(1);
        const QString removed = kits.removeKit();
        QCOMPARE(kits.rowCount(), 1);
        QVERIFY(kits.restoreKit(removed));
        QCOMPARE(kits.rowCount(), 2);
        QCOMPARE(kits.name(), u"Work"_s); // the restored kit is the one in use
        kits.setCurrent(0);
        QCOMPARE(kits.name(), u"Channel"_s);

        document::DraftStore store(m_dir.filePath(u"drafts-brand"_s));
        engine::MediaAnalysis analysis(m_dir.filePath(u"cache"_s));
        QString error;
        EditorController editor(store.createDraft(&error), analysis, QStringLiteral(VEDIT_RENDER_EXECUTABLE));
        editor.player()->setVolume(0.0);
        // The kit's colours first in the colour pickers.
        QCOMPARE(editor.inspector()->swatches().first().value<QColor>(), QColor(u"#ff5500"_s));
        editor.importAndInsertPaths({m_files.landscape}, 0, editor.timeline()->mainRow());
        QTRY_COMPARE_WITH_TIMEOUT(mainTrack(editor).clips.size(), size_t(1), 20000);
        const ClipId video = mainTrack(editor).clips.front().id;
        // Intro before the video, outro after it.
        editor.addIntro(kits.intro());
        QTRY_COMPARE_WITH_TIMEOUT(mainTrack(editor).clips.size(), size_t(2), 20000);
        QVERIFY(mainTrack(editor).clips.front().id != video);
        editor.addOutro(kits.outro());
        QTRY_COMPARE_WITH_TIMEOUT(mainTrack(editor).clips.size(), size_t(3), 20000);
        QCOMPARE(mainTrack(editor).clips[1].id, video);
        // The logo as a watermark over the whole video, in a corner, half-transparent.
        editor.addWatermark(kits.logos().front().toUrl());
        const auto watermark = [&]() -> const Clip * {
            for (const Track &track : editor.data().mainSequence()->visualTracks) {
                for (const Clip &clip : track.clips) {
                    if (clip.sticker()) {
                        return &clip;
                    }
                }
            }
            return nullptr;
        };
        QTRY_VERIFY_WITH_TIMEOUT(watermark(), 20000);
        QCOMPARE(watermark()->start.value(), 0);
        QCOMPARE(watermark()->end(), mainTrack(editor).clips.back().end());
        QVERIFY(std::get<double>(watermark()->opacity.staticValue()) < 1.0);
        // A text in the brand's font and colour.
        QVERIFY(editor.addBrandText(u"Inter"_s, QColor(u"#ff5500"_s)));
        bool brandText = false;
        for (const Track &track : editor.data().mainSequence()->visualTracks) {
            for (const Clip &clip : track.clips) {
                if (const TextClipData *text = clip.text()) {
                    brandText = brandText || std::get<Color>(text->style.color.staticValue()) == Color{0xff, 0x55, 0x00, 255};
                }
            }
        }
        QVERIFY(brandText);
        BrandKitModel::setInstance(nullptr);
    }

    void cleanupTestCase() {}
};

QTEST_MAIN(TestEditor)
#include "tst_editor.moc"
