// SPDX-License-Identifier: GPL-3.0-or-later
// Drafts and continuous save (docs/FILE_FORMAT.md §6.1, §9): what is edited is on disk within ~2 s without ever
// pressing "Save", and reopening gives the identical project.
#include "../unit/ProjectFixture.h"

#include "core/serialization/ProjectFile.h"
#include "document/AutoSaver.h"
#include "document/Document.h"
#include "document/DraftLock.h"
#include "document/DraftStore.h"

#include <QElapsedTimer>
#include <QJsonDocument>
#include <QProcess>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>

using namespace vedit;
using namespace vedit::document;
using namespace vedit::test;
using namespace Qt::StringLiterals;

namespace {

// Equal except for the modification date (set by each save).
bool sameContent(ProjectData a, ProjectData b)
{
    a.modifiedAt = b.modifiedAt = {};
    if (!(a == b)) {
        qWarning("first difference: %s", qPrintable(firstDifference(a, b)));
        return false;
    }
    return true;
}

TimelineEditor editorOf(Document &document)
{
    return TimelineEditor(document.data(), document.data().mainSequenceId);
}

// What importing a file does (one command): the media item enters the project.
bool addMedia(Document &document, const Media &media)
{
    EditResult add;
    add.script.push_back(edits::insertMedia(static_cast<int>(document.data().media.size()), media));
    add.text = u"import"_s;
    return document.apply(std::move(add));
}

} // namespace

class TestDocument : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;
    Fixture m_fixture;

    // A new draft whose project already has the fixture's media.
    std::unique_ptr<Document> newDraft(const DraftStore &store)
    {
        QString error;
        ProjectData data = m_fixture.data;
        data.id = ProjectId::create();
        data.name = store.uniqueName(u"Test"_s);
        std::unique_ptr<Document> document = Document::create(store.directoryOf(data.id), std::move(data), &error);
        if (!document) {
            qWarning("cannot create a draft: %s", qPrintable(error));
        }
        return document;
    }

    static bool waitSaved(Document &document, int timeout = 5000)
    {
        QDeadlineTimer deadline(timeout);
        while (!deadline.hasExpired()) {
            if (document.saveState() == Document::SaveState::Saved) {
                return true;
            }
            QTest::qWait(10);
        }
        return false;
    }

private slots:
    void editsAreOnDiskWithoutSavingAndReopenIdentical()
    {
        DraftStore store(m_dir.filePath(u"identical"_s));
        auto document = newDraft(store);
        QVERIFY(document);
        QVERIFY(document->apply(editorOf(*document).insertMedia(m_fixture.video10s, frames(0))));
        QVERIFY(document->apply(editorOf(*document).insertMedia(m_fixture.video4sMute, frames(300))));
        QVERIFY(document->apply(editorOf(*document).insertMedia(m_fixture.photo, frames(420))));
        const auto clip = [&](int i) { return document->data().mainSequence()->visualTracks.front().clips[i].id; };
        QVERIFY(document->apply(editorOf(*document).trimClip(clip(0), ClipEdge::End, frames(100))));
        QVERIFY(document->apply(editorOf(*document).splitClip(clip(1), frames(150))));
        QVERIFY(document->apply(editorOf(*document).moveClip(clip(3), frames(0))));
        QCOMPARE(document->saveState(), Document::SaveState::Saving);
        QVERIFY(waitSaved(*document));
        // On disk already, before closing.
        const ProjectLoadResult onDisk = projectfile::load(document->directory() + u"/project.vproj"_s);
        QVERIFY(onDisk.ok());
        QVERIFY(sameContent(*onDisk.project, document->data()));
        QVERIFY(onDisk.project->modifiedAt > document->data().createdAt.addSecs(-1));

        const ProjectData before = document->data();
        const ProjectId id = before.id;
        QVERIFY(document->close());
        document.reset();
        QString error;
        auto reopened = store.openDraft(id, &error);
        QVERIFY2(reopened, qPrintable(error));
        QVERIFY(!reopened->recovered());
        QVERIFY(sameContent(reopened->data(), before));
    }

    void continuousEditsAreDebounced()
    {
        DraftStore store(m_dir.filePath(u"debounce"_s));
        auto document = newDraft(store);
        QVERIFY(document->apply(editorOf(*document).insertMedia(m_fixture.video10s, frames(0))));
        QVERIFY(waitSaved(*document));
        const int initial = document->writeCount();
        // A burst of edits (a drag): one write after 300 ms of quiet.
        const ClipId clip = document->data().mainSequence()->visualTracks.front().clips.front().id;
        for (int i = 1; i <= 10; ++i) {
            QVERIFY(document->apply(editorOf(*document).trimClip(clip, ClipEdge::End, frames(300 - i))));
            QTest::qWait(20);
        }
        QCOMPARE(document->writeCount(), initial);
        QVERIFY(waitSaved(*document));
        QCOMPARE(document->writeCount(), initial + 1);

        // Continuous editing: still written at most ~2 s after the first unsaved change.
        QSignalSpy states(document.get(), &Document::saveStateChanged);
        QElapsedTimer clock;
        clock.start();
        qint64 firstWrite = -1;
        for (int i = 1; clock.elapsed() < 2600; ++i) {
            QVERIFY(document->apply(editorOf(*document).trimClip(clip, ClipEdge::End, frames(200 - (i % 50)))));
            QTest::qWait(100);
            if (firstWrite < 0 && document->writeCount() > initial + 1) {
                firstWrite = clock.elapsed();
            }
        }
        QVERIFY2(firstWrite >= AutoSaver::kMaxDelayMs - 100 && firstWrite <= AutoSaver::kMaxDelayMs + 400,
                 qPrintable(QString::number(firstWrite)));
    }

    void unchangedContentIsNotWritten()
    {
        DraftStore store(m_dir.filePath(u"unchanged"_s));
        auto document = newDraft(store);
        QVERIFY(document->apply(editorOf(*document).insertMedia(m_fixture.video10s, frames(0))));
        QVERIFY(waitSaved(*document));
        const int writes = document->writeCount();
        QVERIFY(document->apply(editorOf(*document).insertMedia(m_fixture.photo, frames(300))));
        document->undoStack().undo(); // back to what is on disk
        QTest::qWait(AutoSaver::kIdleDelayMs + 200);
        QVERIFY(waitSaved(*document));
        QCOMPARE(document->writeCount(), writes);
    }

    void failedWritesAreRetried()
    {
        DraftStore store(m_dir.filePath(u"failure"_s));
        auto document = newDraft(store);
        const QString directory = document->directory();
        QFile::Permissions permissions = QFile::permissions(directory);
        QVERIFY(QFile::setPermissions(directory, QFile::ReadOwner | QFile::ExeOwner));
        QVERIFY(document->apply(editorOf(*document).insertMedia(m_fixture.video10s, frames(0))));
        QTRY_COMPARE_WITH_TIMEOUT(document->saveState(), Document::SaveState::Failed, 3000);
        QVERIFY(!document->saveError().isEmpty());
        // Memory is intact and the next attempt succeeds once the disk is writable again.
        QCOMPARE(document->data().mainSequence()->visualTracks.front().clips.size(), size_t(1));
        QVERIFY(QFile::setPermissions(directory, permissions));
        QVERIFY(waitSaved(*document, 5000));
        const ProjectLoadResult onDisk = projectfile::load(directory + u"/project.vproj"_s);
        QVERIFY(sameContent(*onDisk.project, document->data()));
    }

    void lockAndRecovery()
    {
        DraftStore store(m_dir.filePath(u"lock"_s));
        auto document = newDraft(store);
        const ProjectId id = document->data().id;
        const QString directory = document->directory();
        QVERIFY(DraftLock::isHeld(directory));
        QString error;
        QVERIFY(!store.openDraft(id, &error)); // already open (here: in this process)
        QVERIFY(!error.isEmpty());
        QVERIFY(!store.renameDraft(id, u"x"_s, &error));
        QVERIFY(!store.removeDraft(id, &error));
        QVERIFY(document->close());
        QVERIFY(!DraftLock::isHeld(directory));

        // A crash leaves the lock of a process that no longer exists: the draft opens, marked as recovered.
        auto session = store.openDraft(id, &error);
        QVERIFY(session);
        QFile lock(directory + u"/lock"_s);
        QVERIFY(lock.open(QIODevice::ReadOnly));
        QJsonObject json = QJsonDocument::fromJson(lock.readAll()).object();
        lock.close();
        QProcess finished;
        finished.start(QStandardPaths::findExecutable(u"true"_s));
        QVERIFY(finished.waitForFinished());
        json.insert(u"pid"_s, finished.processId() > 0 ? finished.processId() : 999999);
        session.reset(); // closes and removes the lock…
        QVERIFY(projectfile::writeAtomically(directory + u"/lock"_s, QJsonDocument(json).toJson())); // …as if crashed
        auto recovered = store.openDraft(id, &error);
        QVERIFY2(recovered, qPrintable(error));
        QVERIFY(recovered->recovered());
        recovered.reset();
        // A live pid running another program (pid reused after the crash) is stale too.
        json.insert(u"pid"_s, QCoreApplication::applicationPid());
        json.insert(u"program"_s, u"not-vedit"_s);
        QVERIFY(projectfile::writeAtomically(directory + u"/lock"_s, QJsonDocument(json).toJson()));
        auto reused = store.openDraft(id, &error);
        QVERIFY(reused);
        QVERIFY(reused->recovered());
    }

    void draftsOnTheHomeScreen()
    {
        DraftStore store(m_dir.filePath(u"home"_s));
        QString error;
        ProjectId first;
        ProjectId second;
        {
            auto a = store.createDraft(&error);
            QVERIFY2(a, qPrintable(error));
            first = a->data().id;
            QVERIFY(addMedia(*a, *m_fixture.data.findMedia(m_fixture.video10s)));
            QVERIFY(a->apply(editorOf(*a).insertMedia(m_fixture.video10s, frames(0))));
            a->setThumbnail(QImage(64, 36, QImage::Format_RGB32));
            a->setUiState(QJsonObject{{u"playhead"_s, 42}});
        }
        QTest::qWait(1100); // distinct modification times (ISO dates have seconds)
        {
            auto b = store.createDraft(&error);
            QVERIFY(b);
            second = b->data().id;
            QVERIFY(b->data().name != store.info(first)->name); // "25 September (2)"
        }
        QList<DraftInfo> drafts = store.list();
        QCOMPARE(drafts.size(), 2);
        QCOMPARE(drafts[0].id, second); // most recent first
        QCOMPARE(drafts[1].id, first);
        QCOMPARE(drafts[1].duration, frames(300));
        QCOMPARE(drafts[1].canvas, QSize(1920, 1080));
        QVERIFY(!drafts[1].thumbnailPath.isEmpty());
        QVERIFY(!drafts[1].openElsewhere);

        // UI state comes back with the draft.
        {
            auto a = store.openDraft(first, &error);
            QCOMPARE(a->uiState().value(u"playhead"_s).toInt(), 42);
        }

        QVERIFY(store.renameDraft(first, u"  Holiday  "_s, &error));
        QCOMPARE(store.info(first)->name, u"Holiday"_s); // draft.json regenerated from the project
        QVERIFY(!store.renameDraft(first, u"   "_s, &error));

        const std::optional<ProjectId> copy = store.duplicateDraft(first, &error);
        QVERIFY(copy);
        QVERIFY(*copy != first);
        QCOMPARE(store.info(*copy)->name, QCoreApplication::translate("vedit::document::DraftStore", "%1 copy").arg(u"Holiday"_s));
        QCOMPARE(store.info(*copy)->duration, frames(300));
        QVERIFY(!store.info(*copy)->thumbnailPath.isEmpty());
        auto opened = store.openDraft(*copy, &error);
        QCOMPARE(opened->data().id, *copy);
        opened.reset();

        QVERIFY2(store.removeDraft(second, &error), qPrintable(error));
        QCOMPARE(store.list().size(), 2);
        QVERIFY(!QFileInfo::exists(store.directoryOf(second)));
    }

    void damagedDraftJsonIsRegenerated()
    {
        DraftStore store(m_dir.filePath(u"cache"_s));
        auto document = newDraft(store);
        QVERIFY(document->apply(editorOf(*document).insertMedia(m_fixture.video4sMute, frames(0))));
        const ProjectId id = document->data().id;
        document.reset();
        QFile draft(store.directoryOf(id) + u"/draft.json"_s);
        QVERIFY(draft.open(QIODevice::WriteOnly));
        draft.write("{ not json");
        draft.close();
        const std::optional<DraftInfo> info = store.info(id);
        QVERIFY(info);
        QCOMPARE(info->duration, frames(120));
        QCOMPARE(info->name, u"Test"_s);
    }
};

QTEST_GUILESS_MAIN(TestDocument)
#include "tst_document.moc"
