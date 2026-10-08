// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/serialization/ProjectJson.h"

#include <QByteArray>
#include <QString>

namespace velacut::projectfile {

struct WriteResult
{
    bool ok = false;
    QString error; // for the user, translated

    explicit operator bool() const { return ok; }
};

// Writes `bytes` so that an interruption at any moment leaves either the old or the new file, never a
// partial one: temporary file in the same directory, fsync, atomic rename, fsync of the directory
// (docs/FILE_FORMAT.md §9.1).
WriteResult writeAtomically(const QString &path, const QByteArray &bytes);

WriteResult save(const QString &path, const ProjectData &project);
ProjectLoadResult load(const QString &path);

} // namespace velacut::projectfile
