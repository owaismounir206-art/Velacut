// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QString>

namespace velacut {

// In development builds, redirect every XDG base directory into the build tree before any Qt
// object is created, so that the app, helper processes and caches (Mesa, fontconfig) never write
// into the user's home. Disabled in packaged builds (VELACUT_DEV_SANDBOX=OFF) or with VELACUT_NO_SANDBOX=1.
// Returns the sandbox root, or an empty string when the sandbox is not active.
inline QString applyDevSandbox()
{
#ifdef VELACUT_DEV_HOME
    if (qEnvironmentVariableIntValue("VELACUT_NO_SANDBOX") != 0) {
        return {};
    }
    const QString root = QString::fromUtf8(VELACUT_DEV_HOME);
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
        // Remember the user's real value: reading desktop settings (accent color, wallpaper) must still
        // look at the real configuration. Only the first call records it.
        const QByteArray hostVariable = QByteArray("VELACUT_HOST_") + dir.variable;
        if (!qEnvironmentVariableIsSet(hostVariable.constData())) {
            qputenv(hostVariable.constData(), qgetenv(dir.variable));
        }
        qputenv(dir.variable, QFile::encodeName(path));
    }
    return root;
#else
    return {};
#endif
}

// The user's real XDG config directory, even when the development sandbox redirected XDG_CONFIG_HOME.
// Used only to *read* desktop settings; the app never writes there.
inline QString hostConfigHome()
{
    const QByteArray host = qgetenv("VELACUT_HOST_XDG_CONFIG_HOME");
    const QByteArray value = qEnvironmentVariableIsSet("VELACUT_HOST_XDG_CONFIG_HOME") ? host : qgetenv("XDG_CONFIG_HOME");
    if (!value.isEmpty()) {
        return QFile::decodeName(value);
    }
    return QDir::homePath() + QLatin1StringView("/.config");
}

} // namespace velacut
