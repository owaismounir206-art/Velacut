// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QJsonObject>
#include <QString>

#include <optional>

namespace velacut::migrations {

// A migration turns a project JSON of version `from` into version `from + 1` (docs/FILE_FORMAT.md §8).
// Migrations are pure functions on JSON and are applied in chain by migrate().
struct Migration
{
    int from;
    QJsonObject (*apply)(QJsonObject project);
};

struct Result
{
    std::optional<QJsonObject> project;
    QString error;
    bool migrated = false;
};

// Brings `project` (declaring `version`) to `targetVersion`. Fails if a step is missing,
// or if the file is newer than this build (never downgraded).
Result migrate(QJsonObject project, int version, int targetVersion);

} // namespace velacut::migrations
