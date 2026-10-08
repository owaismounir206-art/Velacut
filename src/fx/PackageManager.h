// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "fx/Library.h"

#include <QObject>
#include <QString>

#include <vector>

namespace velacut::fx {

// An asset pack as Preferences → Packs shows it.
struct PackageInfo
{
    QString id;           // "vedit.core", "my.filters"…
    LocalizedText name;
    int version = 1;
    int items = 0;        // filters, transitions, styles, stickers, effects, templates…
    QString path;         // the pack's folder
    bool builtIn = false; // vedit.core: part of the application, cannot be removed
};

// The user's asset packs (SPEC §5.13 "gestore degli asset"; format in docs/EFFECT_FORMAT.md): installed from a folder
// or a .zip archive into Library::userPacksFolder(), removed from there. After every change the library is reloaded
// (Library::reload()) and libraryChanged() is emitted, so the panels show the new items at once.
class PackageManager : public QObject
{
    Q_OBJECT

public:
    explicit PackageManager(QObject *parent = nullptr);

    // The process-wide instance (its signal reaches every library panel).
    static PackageManager &instance();

    // The core pack, then the user's packs that load without errors.
    std::vector<PackageInfo> installedPackages() const;
    // Installs (or updates, same id) the pack in a folder or a .zip archive. False with `error` set on failure.
    bool installPackage(const QString &sourcePath, QString *error = nullptr);
    // Removes a user pack. The core pack cannot be removed.
    bool removePackage(const QString &packageId, QString *error = nullptr);

signals:
    void libraryChanged();

private:
    static bool extractArchive(const QString &archive, const QString &destination, QString *error);
    static bool copyRecursively(const QString &source, const QString &destination, QString *error);
};

} // namespace velacut::fx
