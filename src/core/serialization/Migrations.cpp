// SPDX-License-Identifier: GPL-3.0-or-later
#include "Migrations.h"

#include <QCoreApplication>

#include <span>

namespace vedit::migrations {

namespace {

// The registry of migrations, one per format version step. Version 1 is the first format, so the
// list is empty; each future schema change appends its step here, with fixtures in tests/data/format.
constexpr std::span<const Migration> registry()
{
    return {};
}

const Migration *find(int from)
{
    for (const Migration &migration : registry()) {
        if (migration.from == from) {
            return &migration;
        }
    }
    return nullptr;
}

} // namespace

Result migrate(QJsonObject project, int version, int targetVersion)
{
    Result result;
    if (version < 1) {
        result.error = QCoreApplication::translate("vedit::migrations", "The project file has an invalid format version.");
        return result;
    }
    if (version > targetVersion) {
        result.error = QCoreApplication::translate(
            "vedit::migrations", "This project was created with a newer version of vedit (format %1). "
                                 "Update vedit to open it.")
                           .arg(version);
        return result;
    }
    while (version < targetVersion) {
        const Migration *step = find(version);
        if (!step) {
            result.error = QCoreApplication::translate("vedit::migrations",
                                                       "No migration is available from project format %1.")
                               .arg(version);
            return result;
        }
        project = step->apply(std::move(project));
        project.insert(QStringLiteral("formatVersion"), ++version);
        result.migrated = true;
    }
    result.project = std::move(project);
    return result;
}

} // namespace vedit::migrations
