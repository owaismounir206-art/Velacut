// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QColor>
#include <QObject>
#include <QString>

#include <optional>

class QDBusVariant;

namespace vedit::theme {

// Desktop appearance settings read (never written) from the system: the xdg-desktop-portal
// "org.freedesktop.appearance" namespace, with fallbacks for desktops whose portal does not expose
// the accent color (docs/ARCHITECTURE.md §8.1, D-13). Updated live when the portal signals a change.
class SystemAppearance : public QObject
{
    Q_OBJECT

public:
    enum class ColorSchemePreference
    {
        NoPreference,
        Dark,
        Light,
    };

    explicit SystemAppearance(QObject *parent = nullptr);

    // Reads everything now (blocking, bounded by short timeouts). Called once at startup.
    void refresh();

    ColorSchemePreference colorScheme() const noexcept { return m_colorScheme; }
    bool highContrast() const noexcept { return m_highContrast; }
    bool reducedMotion() const noexcept { return m_reducedMotion; }
    std::optional<QColor> accentColor() const { return m_accentColor; }
    // Where the accent color came from ("portal", "gnome", "kde"), for Preferences and bug reports.
    QString accentSource() const { return m_accentSource; }

    // Best-effort path of the current desktop wallpaper (GNOME, KDE Plasma, hyprpaper, swww).
    // Blocking: call from a worker thread.
    static std::optional<QString> wallpaperPath();

    // Parsers, public for tests.
    static std::optional<QColor> parsePortalAccent(double red, double green, double blue);
    static std::optional<QColor> parseGnomeAccent(const QString &gsettingsOutput);
    static std::optional<QColor> parseKdeAccent(const QString &kdeglobals);
    static std::optional<QString> parseGnomeWallpaper(const QString &gsettingsOutput);
    static std::optional<QString> parsePlasmaWallpaper(const QString &appletsrc);
    static std::optional<QString> parseHyprpaper(const QString &listActiveOutput);
    static std::optional<QString> parseSwww(const QString &queryOutput);

signals:
    void changed();

private slots:
    void onPortalSettingChanged(const QString &nameSpace, const QString &key, const QDBusVariant &value);

private:
    void applyPortalValue(const QString &key, const QVariant &value);
    void readFallbackAccent();

    ColorSchemePreference m_colorScheme = ColorSchemePreference::NoPreference;
    bool m_highContrast = false;
    bool m_reducedMotion = false;
    std::optional<QColor> m_accentColor;
    QString m_accentSource;
    bool m_portalAccent = false;
};

} // namespace vedit::theme
