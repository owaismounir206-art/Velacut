// SPDX-License-Identifier: GPL-3.0-or-later
// The Phase 1 criterion through the editor controller, as the interface drives it (SPEC §8): import 3 clips, cut
// and reorder them, export a correct MP4, close and reopen the identical draft without ever pressing "Save".
#include "../unit/ProjectFixture.h"
#include "TestMedia.h"

#include "document/Document.h"
#include "document/DraftStore.h"
#include "engine/analysis/MediaAnalysis.h"
#include "engine/mlt/MltRuntime.h"
#include "ui/controllers/EditorController.h"

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

    void cleanupTestCase() {}
};

QTEST_MAIN(TestEditor)
#include "tst_editor.moc"
