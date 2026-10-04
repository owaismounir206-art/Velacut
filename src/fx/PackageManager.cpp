// SPDX-License-Identifier: GPL-3.0-or-later
#include "PackageManager.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTemporaryDir>

using namespace Qt::StringLiterals;

namespace vedit::fx {

namespace {

// The folder holding pack.json: the folder itself, or the only folder inside it (an archive of the pack's folder).
QString packRoot(const QString &folder)
{
    if (QFileInfo::exists(folder + u"/pack.json"_s)) {
        return folder;
    }
    const QStringList entries = QDir(folder).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    if (entries.size() == 1 && QFileInfo::exists(folder + u"/"_s + entries.front() + u"/pack.json"_s)) {
        return folder + u"/"_s + entries.front();
    }
    return {};
}

} // namespace

PackageManager::PackageManager(QObject *parent)
    : QObject(parent)
{
}

PackageManager &PackageManager::instance()
{
    static PackageManager manager;
    return manager;
}

std::vector<PackageInfo> PackageManager::installedPackages() const
{
    std::vector<PackageInfo> packages;
    const Library core = Library::load(u":/vedit/packs/vedit.core"_s);
    packages.push_back({core.packId(), core.packName(), core.packVersion(), core.itemCount(), u":/vedit/packs/vedit.core"_s, true});
    const QDir folder(Library::userPacksFolder());
    for (const QString &entry : folder.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        const Library pack = Library::load(folder.filePath(entry));
        if (pack.errors().isEmpty() && pack.packId() != core.packId()) {
            packages.push_back({pack.packId(), pack.packName(), pack.packVersion(), pack.itemCount(), folder.filePath(entry), false});
        }
    }
    return packages;
}

bool PackageManager::installPackage(const QString &sourcePath, QString *error)
{
    const auto failed = [error](const QString &text) {
        if (error) {
            *error = text;
        }
        return false;
    };
    const QFileInfo source(sourcePath);
    if (!source.exists()) {
        return failed(tr("The file does not exist: %1").arg(sourcePath));
    }
    QTemporaryDir unpacked;
    QString folder = sourcePath;
    if (source.isFile()) {
        if (!unpacked.isValid()) {
            return failed(tr("No room for unpacking the archive."));
        }
        QString reason;
        if (!extractArchive(sourcePath, unpacked.path(), &reason)) {
            return failed(reason);
        }
        folder = unpacked.path();
    }
    const QString root = packRoot(folder);
    if (root.isEmpty()) {
        return failed(tr("This is not a vedit pack: pack.json is missing."));
    }
    const Library pack = Library::load(root);
    if (!pack.errors().isEmpty()) {
        return failed(tr("The pack has errors: %1").arg(pack.errors().join(u"; "_s)));
    }
    static const QRegularExpression safeId(u"^[A-Za-z0-9._-]+$"_s);
    if (!safeId.match(pack.packId()).hasMatch() || pack.packId() == QLatin1String(Library::kCorePack)) {
        return failed(tr("The pack's id is not valid: %1").arg(pack.packId()));
    }
    if (pack.itemCount() == 0) {
        return failed(tr("The pack contains no items."));
    }
    const QString destination = Library::userPacksFolder() + u"/"_s + pack.packId();
    // Copied next to its place first: a failed copy never leaves half a pack where the library looks.
    const QString staging = destination + u".installing"_s;
    QDir(staging).removeRecursively();
    QString reason;
    if (!copyRecursively(root, staging, &reason)) {
        QDir(staging).removeRecursively();
        return failed(reason);
    }
    QDir(destination).removeRecursively(); // an update replaces the installed version
    if (!QDir().rename(staging, destination)) {
        QDir(staging).removeRecursively();
        return failed(tr("The pack could not be installed in %1.").arg(Library::userPacksFolder()));
    }
    Library::reload();
    emit libraryChanged();
    return true;
}

bool PackageManager::removePackage(const QString &packageId, QString *error)
{
    if (packageId == QLatin1String(Library::kCorePack) || packageId.contains(u'/') || packageId.startsWith(u'.')) {
        if (error) {
            *error = tr("The vedit library is part of the application and cannot be removed.");
        }
        return false;
    }
    const QString folder = Library::userPacksFolder() + u"/"_s + packageId;
    if (!QFileInfo(folder).isDir() || !QDir(folder).removeRecursively()) {
        if (error) {
            *error = tr("The pack could not be removed: %1").arg(packageId);
        }
        return false;
    }
    Library::reload();
    emit libraryChanged();
    return true;
}

bool PackageManager::extractArchive(const QString &archive, const QString &destination, QString *error)
{
    // libarchive's bsdtar is part of every Arch system (pacman uses it); unzip as a second choice.
    QString program = QStandardPaths::findExecutable(u"bsdtar"_s);
    QStringList arguments{u"-x"_s, u"-f"_s, archive, u"-C"_s, destination};
    if (program.isEmpty()) {
        program = QStandardPaths::findExecutable(u"unzip"_s);
        arguments = {u"-q"_s, archive, u"-d"_s, destination};
    }
    if (program.isEmpty()) {
        if (error) {
            *error = tr("Installing from an archive needs bsdtar (package libarchive) or unzip.");
        }
        return false;
    }
    QProcess process;
    process.start(program, arguments);
    if (!process.waitForFinished(120000) || process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        if (error) {
            *error = tr("The archive could not be opened: %1").arg(QString::fromLocal8Bit(process.readAllStandardError()).trimmed());
        }
        return false;
    }
    // Nothing may land outside the destination (archives with "../" or absolute paths).
    const QString base = QDir(destination).canonicalPath();
    QDirIterator it(destination, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::System | QDir::Hidden, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QFileInfo info(it.next());
        if (info.isSymLink() || !info.canonicalFilePath().startsWith(base)) {
            if (error) {
                *error = tr("The archive contains links or paths outside the pack: not installed.");
            }
            return false;
        }
    }
    return true;
}

bool PackageManager::copyRecursively(const QString &source, const QString &destination, QString *error)
{
    if (!QDir().mkpath(destination)) {
        if (error) {
            *error = tr("The folder cannot be created: %1").arg(destination);
        }
        return false;
    }
    const QDir from(source);
    for (const QFileInfo &entry : from.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QString target = destination + u"/"_s + entry.fileName();
        if (entry.isSymLink()) {
            continue; // a pack is plain files
        }
        if (entry.isDir()) {
            if (!copyRecursively(entry.filePath(), target, error)) {
                return false;
            }
        } else if (!QFile::copy(entry.filePath(), target)) {
            if (error) {
                *error = tr("The file cannot be copied: %1").arg(entry.filePath());
            }
            return false;
        }
    }
    return true;
}

} // namespace vedit::fx
