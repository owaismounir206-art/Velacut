// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>
#include <QSettings>
#include <QString>

namespace vedit::ui {

// User preferences for the application (persisted in QSettings).
// Covers general settings, performance, shortcuts, and appearance.
class Preferences : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY languageChanged FINAL)
    Q_PROPERTY(bool autoSave READ autoSave WRITE setAutoSave NOTIFY autoSaveChanged FINAL)
    Q_PROPERTY(int autoSaveInterval READ autoSaveInterval WRITE setAutoSaveInterval NOTIFY autoSaveIntervalChanged FINAL)
    Q_PROPERTY(bool hardwareAcceleration READ hardwareAcceleration WRITE setHardwareAcceleration NOTIFY hardwareAccelerationChanged FINAL)
    Q_PROPERTY(int proxyScale READ proxyScale WRITE setProxyScale NOTIFY proxyScaleChanged FINAL)

public:
    explicit Preferences(QObject *parent = nullptr);

    // Language: "en", "it", or "auto" (system default)
    QString language() const;
    void setLanguage(const QString &language);

    // Auto-save enabled (always true, but user can see the setting)
    bool autoSave() const { return true; } // Always on
    void setAutoSave(bool enabled) { Q_UNUSED(enabled); } // No-op, always on

    // Auto-save interval in seconds (default 2)
    int autoSaveInterval() const;
    void setAutoSaveInterval(int seconds);

    // Hardware acceleration (GPU rendering, default true)
    bool hardwareAcceleration() const;
    void setHardwareAcceleration(bool enabled);

    // Proxy scale: 0 = disabled, 1 = 1/2, 2 = 1/4, 3 = 1/8 (default 0)
    int proxyScale() const;
    void setProxyScale(int scale);

    // Reset all preferences to defaults
    Q_INVOKABLE void resetToDefaults();

signals:
    void languageChanged();
    void autoSaveChanged();
    void autoSaveIntervalChanged();
    void hardwareAccelerationChanged();
    void proxyScaleChanged();

private:
    QSettings m_settings;
};

} // namespace vedit::ui
