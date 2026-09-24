// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QString>

namespace vedit {

// In development builds, redirect every XDG base directory into the build tree before any Qt
// object is created, so that the app, helper processes and caches (Mesa, fontconfig) never write
// into the user's home. Disabled in packaged builds (VEDIT_DEV_SANDBOX=OFF) or with VEDIT_NO_SANDBOX=1.
// Returns the sandbox root, or an empty string when the sandbox is not active.
inline QString applyDevSandbox()
{
#ifdef VEDIT_DEV_HOME
    if (qEnvironmentVariableIntValue("VEDIT_NO_SANDBOX") != 0) {
        return {};
    }
    const QString root = QString::fromUtf8(VEDIT_DEV_HOME);
    const struct
    {
        const char *variable;
        const char *subdir;
    } dirs[] = {{"XDG_CONFIG_HOME", "config"},
                {"XDG_DATA_HOME", "data"},
                {"XDG_CACHE_HOME", "cache"},
                {"XDG_STATE_HOME", "state"}};
    for (const auto &dir : dirs) {
        const QString path = root + QLatin1Char('/') + QLatin1StringView(dir.subdir);
        QDir().mkpath(path);
        qputenv(dir.variable, QFile::encodeName(path));
    }
    return root;
#else
    return {};
#endif
}

} // namespace vedit
