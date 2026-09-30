// SPDX-License-Identifier: GPL-3.0-or-later
#include "PackageManager.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

using namespace Qt::StringLiterals;

namespace vedit::fx {

PackageManager::PackageManager(QObject *parent)
    : QObject(parent)
{
}

QString PackageManager::userPackagesDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + u"/vedit/packs"_s;
}

std::vector<PackageInfo> PackageManager::installedPackages() const
{
    std::vector<PackageInfo> packages;

    // Built-in core pack
    PackageInfo core;
    core.id = u"vedit.core"_s;
    core.name = u"Core Assets"_s;
    core.version = u"1.0"_s;
    core.author = u"vedit"_s;
    core.path = u":/vedit/packs/vedit.core"_s;
    core.builtIn = true;
    packages.push_back(core);

    // User-installed packages
    const QString userDir = userPackagesDir();
    if (QDir(userDir).exists()) {
        const QStringList entries = QDir(userDir).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QString &entry : entries) {
            const QString packagePath = userDir + u"/"_s + entry;
            PackageInfo info = loadPackageInfo(packagePath);
            if (!info.id.isEmpty()) {
                packages.push_back(info);
            }
        }
    }

    return packages;
}

PackageInfo PackageManager::loadPackageInfo(const QString &packageDir) const
{
    PackageInfo info;
    const QString manifestPath = packageDir + u"/manifest.json"_s;
    QFile file(manifestPath);
    if (!file.open(QIODevice::ReadOnly)) {
        return info;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject()) {
        return info;
    }

    const QJsonObject obj = doc.object();
    info.id = obj.value(u"id"_s).toString();
    info.name = obj.value(u"name"_s).toString();
    info.version = obj.value(u"version"_s).toString(u"1.0"_s);
    info.author = obj.value(u"author"_s).toString();
    info.path = packageDir;
    info.builtIn = false;

    return info;
}

bool PackageManager::installPackage(const QString &sourcePath, QString *error)
{
    const QFileInfo sourceInfo(sourcePath);
    if (!sourceInfo.exists()) {
        if (error) {
            *error = tr("Source path does not exist: %1").arg(sourcePath);
        }
        return false;
    }

    const QString userDir = userPackagesDir();
    QDir().mkpath(userDir);

    // Handle .zip archives
    if (sourceInfo.suffix().toLower() == u"zip"_s) {
        // TODO: Extract zip to a temporary directory, load manifest, then move to final location
        if (error) {
            *error = tr("ZIP package installation not yet implemented.");
        }
        return false;
    }

    // Handle directories
    if (!sourceInfo.isDir()) {
        if (error) {
            *error = tr("Source must be a directory or .zip archive.");
        }
        return false;
    }

    // Load package info to get the ID
    const PackageInfo info = loadPackageInfo(sourcePath);
    if (info.id.isEmpty()) {
        if (error) {
            *error = tr("Invalid package: manifest.json not found or invalid.");
        }
        return false;
    }

    // Check if already installed
    const QString destPath = userDir + u"/"_s + info.id;
    if (QDir(destPath).exists()) {
        if (error) {
            *error = tr("Package '%1' is already installed.").arg(info.id);
        }
        return false;
    }

    // Copy the package directory
    if (!QFile::copy(sourcePath, destPath)) {
        // Qt's QFile::copy doesn't work for directories, use a recursive copy
        if (!copyRecursively(sourcePath, destPath, error)) {
            return false;
        }
    }

    emit packagesChanged();
    return true;
}

bool PackageManager::removePackage(const QString &packageId, QString *error)
{
    // Cannot remove built-in packages
    if (packageId == u"vedit.core"_s) {
        if (error) {
            *error = tr("Cannot remove built-in package.");
        }
        return false;
    }

    const QString packagePath = userPackagesDir() + u"/"_s + packageId;
    if (!QDir(packagePath).exists()) {
        if (error) {
            *error = tr("Package not found: %1").arg(packageId);
        }
        return false;
    }

    // Remove the directory
    if (!QDir(packagePath).removeRecursively()) {
        if (error) {
            *error = tr("Failed to remove package directory: %1").arg(packagePath);
        }
        return false;
    }

    emit packagesChanged();
    return true;
}

bool PackageManager::copyRecursively(const QString &srcPath, const QString &dstPath, QString *error)
{
    const QDir srcDir(srcPath);
    if (!srcDir.exists()) {
        if (error) {
            *error = tr("Source directory does not exist: %1").arg(srcPath);
        }
        return false;
    }

    if (!QDir().mkpath(dstPath)) {
        if (error) {
            *error = tr("Failed to create destination directory: %1").arg(dstPath);
        }
        return false;
    }

    const QStringList entries = srcDir.entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &entry : entries) {
        const QString srcEntry = srcPath + u"/"_s + entry;
        const QString dstEntry = dstPath + u"/"_s + entry;

        const QFileInfo info(srcEntry);
        if (info.isDir()) {
            if (!copyRecursively(srcEntry, dstEntry, error)) {
                return false;
            }
        } else {
            if (!QFile::copy(srcEntry, dstEntry)) {
                if (error) {
                    *error = tr("Failed to copy file: %1 to %2").arg(srcEntry, dstEntry);
                }
                return false;
            }
        }
    }

    return true;
}

} // namespace vedit::fx
