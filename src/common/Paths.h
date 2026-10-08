// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDir>
#include <QStandardPaths>
#include <QString>

namespace velacut::paths {

// XDG directories of velacut (SPEC §5.16): ~/.config/velacut, ~/.local/share/velacut, ~/.cache/velacut,
// ~/.local/state/velacut. Created on first use. In development builds XDG_* point inside the build tree.
inline QString ensure(QStandardPaths::StandardLocation base)
{
    const QString path = QStandardPaths::writableLocation(base) + QLatin1StringView("/velacut");
    QDir().mkpath(path);
    return path;
}

inline QString configDir() { return ensure(QStandardPaths::GenericConfigLocation); }
inline QString dataDir() { return ensure(QStandardPaths::GenericDataLocation); }
inline QString cacheDir() { return ensure(QStandardPaths::GenericCacheLocation); }
inline QString stateDir() { return ensure(QStandardPaths::GenericStateLocation); }

// Default folder of exported videos: the user's Videos folder. In development builds with the sandbox active it is
// inside the build tree too, so that trying the export never writes into the user's home (D-03).
inline QString videosDir()
{
#ifdef VELACUT_DEV_HOME
    if (qEnvironmentVariableIntValue("VELACUT_NO_SANDBOX") == 0) {
        const QString path = QString::fromUtf8(VELACUT_DEV_HOME) + QLatin1StringView("/Videos");
        QDir().mkpath(path);
        return path;
    }
#endif
    const QString videos = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    return videos.isEmpty() ? QDir::homePath() : videos;
}

} // namespace velacut::paths
