// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDir>
#include <QStandardPaths>
#include <QString>

namespace vedit::paths {

// XDG directories of vedit (SPEC §5.16): ~/.config/vedit, ~/.local/share/vedit, ~/.cache/vedit,
// ~/.local/state/vedit. Created on first use. In development builds XDG_* point inside the build tree.
inline QString ensure(QStandardPaths::StandardLocation base)
{
    const QString path = QStandardPaths::writableLocation(base) + QLatin1StringView("/vedit");
    QDir().mkpath(path);
    return path;
}

inline QString configDir() { return ensure(QStandardPaths::GenericConfigLocation); }
inline QString dataDir() { return ensure(QStandardPaths::GenericDataLocation); }
inline QString cacheDir() { return ensure(QStandardPaths::GenericCacheLocation); }
inline QString stateDir() { return ensure(QStandardPaths::GenericStateLocation); }

} // namespace vedit::paths
