// SPDX-License-Identifier: GPL-3.0-or-later
// The asset pack manager (SPEC §5.13): a pack installed from a folder or a .zip shows its items in the library at once,
// can be updated and removed; broken packs and unsafe archives are refused; the core pack stays.
#include "fx/Library.h"
#include "fx/PackageManager.h"

#include <QDir>
#include <QFile>
#include <QProcess>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

using namespace velacut::fx;
using namespace Qt::StringLiterals;

class TestPackages : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;

    static void write(const QString &path, const QByteArray &content)
    {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(content);
    }
    // A pack with one filter (and its category) and, optionally, a sticker picture.
    QString makePack(const QString &name, const QString &id, int version, const QString &filterId)
    {
        const QString folder = m_dir.filePath(name);
        write(folder + u"/pack.json"_s, QStringLiteral(R"({"format": "vedit.pack", "formatVersion": 1, "id": "%1", "version": %2,
                                                         "name": {"en": "My looks", "it": "I miei look"}})")
                                            .arg(id).arg(version).toUtf8());
        write(folder + u"/filters.json"_s, QStringLiteral(R"({"categories": [{"id": "mine", "name": {"en": "Mine", "it": "Miei"}}],
            "items": [{"kind": "filter", "id": "%1", "version": 1, "category": "mine",
                       "name": {"en": "Sunset", "it": "Tramonto"}, "look": {"temperature": 0.4}}]})")
                                               .arg(filterId).toUtf8());
        return folder;
    }

private slots:
    void initTestCase()
    {
        // Packs are installed in XDG_DATA_HOME: only inside the build tree (CTest and tools/run-test.sh set it), never in
        // the user's real data folder.
        const QString data = qEnvironmentVariable("XDG_DATA_HOME");
        if (data.isEmpty() || !data.contains(u"/test-home/"_s)) {
            QSKIP("run through CTest or tools/run-test.sh (XDG_DATA_HOME inside the build tree)");
        }
        QDir(Library::userPacksFolder()).removeRecursively(); // what a previous run left
        QVERIFY(Library::core().filter(u"filters/vivid"_s)); // the core pack is there
    }

    void installFromFolderUpdateAndRemove()
    {
        PackageManager &manager = PackageManager::instance();
        QSignalSpy changed(&manager, &PackageManager::libraryChanged);
        const Library &before = Library::core();
        QVERIFY(!before.filter(u"mine/sunset"_s));

        QString error;
        QVERIFY2(manager.installPackage(makePack(u"pack-a"_s, u"test.looks"_s, 1, u"mine/sunset"_s), &error), qPrintable(error));
        QCOMPARE(changed.count(), 1);
        const FilterPreset *sunset = Library::core().filter(u"mine/sunset"_s);
        QVERIFY(sunset);
        QCOMPARE(sunset->name.en, u"Sunset"_s);
        QVERIFY(std::any_of(Library::core().filterCategories().begin(), Library::core().filterCategories().end(),
                            [](const Category &c) { return c.id == u"mine"_s; }));
        // A reference taken before the reload is still valid (other threads may be using it).
        QVERIFY(before.filter(u"filters/vivid"_s));
        QVERIFY(Library::core().filter(u"filters/vivid"_s)); // the core pack stays

        const auto installed = manager.installedPackages();
        QCOMPARE(installed.size(), size_t(2));
        QCOMPARE(installed.front().id, u"vedit.core"_s);
        QVERIFY(installed.front().builtIn);
        QCOMPARE(installed.back().id, u"test.looks"_s);
        QCOMPARE(installed.back().items, 1);

        // Same id, newer version: an update.
        QVERIFY2(manager.installPackage(makePack(u"pack-a2"_s, u"test.looks"_s, 2, u"mine/sunset"_s), &error), qPrintable(error));
        QCOMPARE(manager.installedPackages().back().version, 2);
        QCOMPARE(manager.installedPackages().size(), size_t(2));

        QVERIFY(manager.removePackage(u"test.looks"_s, &error));
        QVERIFY(!Library::core().filter(u"mine/sunset"_s));
        QCOMPARE(manager.installedPackages().size(), size_t(1));
        QVERIFY(!manager.removePackage(u"vedit.core"_s, &error)); // never
        QVERIFY(!error.isEmpty());
    }

    void installFromZip()
    {
        const QString bsdtar = QStandardPaths::findExecutable(u"bsdtar"_s);
        if (bsdtar.isEmpty()) {
            QSKIP("bsdtar is needed to build the test archive");
        }
        const QString folder = makePack(u"zipped/my-pack"_s, u"test.zipped"_s, 1, u"zipped/warm"_s);
        const QString archive = m_dir.filePath(u"my-pack.zip"_s);
        QCOMPARE(QProcess::execute(bsdtar, {u"-a"_s, u"-c"_s, u"-f"_s, archive, u"-C"_s, m_dir.filePath(u"zipped"_s), u"my-pack"_s}), 0);
        QString error;
        QVERIFY2(PackageManager::instance().installPackage(archive, &error), qPrintable(error));
        QVERIFY(Library::core().filter(u"zipped/warm"_s));
        QVERIFY(PackageManager::instance().removePackage(u"test.zipped"_s, &error));
        Q_UNUSED(folder);
    }

    void refusesBrokenPacks()
    {
        PackageManager &manager = PackageManager::instance();
        QString error;
        // No pack.json.
        const QString empty = m_dir.filePath(u"empty"_s);
        QDir().mkpath(empty);
        QVERIFY(!manager.installPackage(empty, &error));
        QVERIFY(!error.isEmpty());
        // The core pack's id cannot be taken over.
        QVERIFY(!manager.installPackage(makePack(u"fake-core"_s, u"vedit.core"_s, 9, u"x/y"_s), &error));
        // An id that would escape the packs folder.
        QVERIFY(!manager.installPackage(makePack(u"evil"_s, u"../evil"_s, 1, u"x/z"_s), &error));
        // A transition with an unknown kernel: errors, refused.
        const QString broken = m_dir.filePath(u"broken"_s);
        write(broken + u"/pack.json"_s, R"({"format": "vedit.pack", "id": "test.broken", "version": 1})");
        write(broken + u"/transitions.json"_s, R"({"items": [{"id": "t/x", "kernel": "noSuchKernel"}]})");
        QVERIFY(!manager.installPackage(broken, &error));
        QVERIFY(error.contains(u"noSuchKernel"_s));
        QCOMPARE(manager.installedPackages().size(), size_t(1));
    }
};

QTEST_GUILESS_MAIN(TestPackages)
#include "tst_packages.moc"
