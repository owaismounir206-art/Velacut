// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

#include <vector>

namespace vedit::fx {

// Information about an installed asset package.
struct PackageInfo
{
    QString id;      // unique package identifier (e.g., "user.myfilters")
    QString name;    // display name
    QString version; // version string
    QString author;  // package author
    QString path;    // absolute path to the package directory
    bool builtIn = false; // true for vedit.core and other built-in packs
};

// Manages user-installed asset packages (effects, transitions, LUT, stickers, templates).
// Packages are installed to ~/.local/share/vedit/packs/ and loaded alongside the core pack.
class PackageManager : public QObject
{
    Q_OBJECT

public:
    explicit PackageManager(QObject *parent = nullptr);

    // Lists all installed packages (built-in + user-installed).
    std::vector<PackageInfo> installedPackages() const;

    // Installs a package from a directory or .zip archive.
    // Returns true on success, false with error message on failure.
    bool installPackage(const QString &sourcePath, QString *error = nullptr);

    // Removes a user-installed package by ID. Built-in packages cannot be removed.
    // Returns true on success, false with error message on failure.
    bool removePackage(const QString &packageId, QString *error = nullptr);

    // Returns the user packages directory (~/.local/share/vedit/packs/).
    static QString userPackagesDir();

signals:
    void packagesChanged();

private:
    PackageInfo loadPackageInfo(const QString &packageDir) const;
    bool copyRecursively(const QString &srcPath, const QString &dstPath, QString *error);
    bool extractZip(const QString &zipPath, const QString &destDir, QString *error);
};

} // namespace vedit::fx
