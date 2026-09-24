// SPDX-License-Identifier: GPL-3.0-or-later
// Integration test required by SPEC §7: open a project, edit it, save it, reopen it identical.
#include "../unit/ProjectFixture.h"

#include "core/serialization/ProjectFile.h"

#include <QDir>
#include <QTemporaryDir>

using namespace vedit;
using namespace vedit::test;
using namespace Qt::StringLiterals;

class TestProjectRoundTrip : public QObject
{
    Q_OBJECT

private slots:
    void openEditSaveReopen()
    {
        const ProjectLoadResult opened = projectfile::load(QFINDTESTDATA("../data/format/v1/example.vproj"));
        QVERIFY2(opened.ok(), qPrintable(opened.error));

        Session session(*opened.project);
        const ClipId clip = session.mainTrack().clips.front().id;
        const RationalTime rateFrame(1, session.data().settings.frameRate);
        // Split, delete the first half, append the media again, move it to the front.
        EditResult split = session.editor().splitClip(clip, rateFrame * 40);
        const ClipId second = split.primaryClip;
        QVERIFY(session.apply(std::move(split)));
        QVERIFY(session.apply(session.editor().deleteClips({clip})));
        EditResult insert = session.editor().insertMedia(session.data().media.front().id, rateFrame * 100000);
        const ClipId appended = insert.primaryClip;
        QVERIFY(session.apply(std::move(insert)));
        QVERIFY(session.apply(session.editor().moveClip(appended, rateFrame * 0)));
        QCOMPARE(session.mainTrack().clips.front().id, appended);
        QCOMPARE(session.mainTrack().clips.back().id, second);

        QTemporaryDir dir(QDir::tempPath() + u"/vedit-roundtrip-XXXXXX"_s);
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(u"edited.vproj"_s);
        QVERIFY(projectfile::save(path, session.data()));

        const ProjectLoadResult reopened = projectfile::load(path);
        QVERIFY2(reopened.ok(), qPrintable(reopened.error));
        QVERIFY(reopened.warnings.isEmpty());
        QVERIFY2(*reopened.project == session.data(), qPrintable(firstDifference(*reopened.project, session.data())));

        // Undo history still works on the live project after saving, and the saved file is untouched.
        while (session.stack.canUndo()) {
            session.stack.undo();
        }
        QVERIFY(session.data() == *opened.project);
        QVERIFY(*projectfile::load(path).project == *reopened.project);
    }
};

QTEST_GUILESS_MAIN(TestProjectRoundTrip)
#include "tst_projectroundtrip.moc"
