// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/project/ProjectData.h"

#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <optional>

namespace vedit {

// Version of docs/FILE_FORMAT.md implemented by this build.
inline constexpr int kProjectFormatVersion = 1;
inline constexpr QLatin1StringView kProjectFormatName("vedit.project");

struct ProjectLoadResult
{
    std::optional<ProjectData> project; // empty on error
    QString error;                      // why the project cannot be opened (for the user)
    QStringList warnings;               // values corrected or repaired while loading (for the log)
    int sourceFormatVersion = 0;        // formatVersion found in the file
    bool migrated = false;              // true if migrations were applied

    bool ok() const { return project.has_value(); }
};

// JSON <-> model (docs/FILE_FORMAT.md). Writing is canonical: the same model always produces the same
// bytes (sorted keys, fixed indentation), so projects diff cleanly in git.
namespace projectjson {

QJsonObject toJson(const ProjectData &project);
QByteArray toBytes(const ProjectData &project);

// Reads any supported format version (older versions are migrated first). Never throws: every problem
// becomes either a warning (value corrected) or an error (project not opened).
ProjectLoadResult fromJson(const QJsonObject &json);
ProjectLoadResult fromBytes(const QByteArray &bytes);

} // namespace projectjson

} // namespace vedit
