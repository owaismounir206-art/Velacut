// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "theme/ColorScheme.h"
#include "theme/SchemeGenerator.h"
#include "theme/SystemAppearance.h"
#include "theme/ThemeColors.h"
#include "theme/ThemeTokens.h"
#include "theme/ThemeTypography.h"

#include <QColor>
#include <QImage>
#include <QObject>
#include <QPointer>
#include <QVariantAnimation>
#include <QtQml/qqmlregistration.h>

class QQmlEngine;
class QJSEngine;

namespace vedit::theme {

// The QML singleton `Theme` (import Vedit.Theme): every color, size, radius and duration used by the UI
// comes from here (SPEC §4, "Sistema di design a token"). Owns the user's theme preferences, resolves the
// seed color from the chosen source, generates the Material 3 scheme and animates scheme changes.
class ThemeManager : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(Theme)
    QML_SINGLETON

    Q_PROPERTY(vedit::theme::ThemeColors *color READ color CONSTANT FINAL)
    Q_PROPERTY(vedit::theme::ThemeTypography *type READ type CONSTANT FINAL)
    Q_PROPERTY(vedit::theme::ThemeShape *shape READ shape CONSTANT FINAL)
    Q_PROPERTY(vedit::theme::ThemeState *state READ state CONSTANT FINAL)
    Q_PROPERTY(vedit::theme::ThemeSpace *space READ space CONSTANT FINAL)
    Q_PROPERTY(vedit::theme::ThemeElevation *elevation READ elevation CONSTANT FINAL)
    Q_PROPERTY(vedit::theme::ThemeMotion *motion READ motion CONSTANT FINAL)
    Q_PROPERTY(vedit::theme::ThemeEditor *editor READ editor CONSTANT FINAL)

    Q_PROPERTY(Mode mode READ mode WRITE setMode NOTIFY settingsChanged FINAL)
    Q_PROPERTY(Contrast contrast READ contrast WRITE setContrast NOTIFY settingsChanged FINAL)
    Q_PROPERTY(Variant variant READ variant WRITE setVariant NOTIFY settingsChanged FINAL)
    Q_PROPERTY(SeedSource seedSource READ seedSource WRITE setSeedSource NOTIFY settingsChanged FINAL)
    Q_PROPERTY(QColor manualSeed READ manualSeed WRITE setManualSeed NOTIFY settingsChanged FINAL)
    Q_PROPERTY(Density density READ density WRITE setDensity NOTIFY settingsChanged FINAL)
    Q_PROPERTY(Motion motionPreference READ motionPreference WRITE setMotionPreference NOTIFY settingsChanged FINAL)

    Q_PROPERTY(bool dark READ dark NOTIFY schemeChanged FINAL)
    Q_PROPERTY(QColor seedColor READ seedColor NOTIFY schemeChanged FINAL)
    // Which source actually provided the seed (the chosen one may be unavailable): see SeedSource.
    Q_PROPERTY(SeedSource effectiveSeedSource READ effectiveSeedSource NOTIFY schemeChanged FINAL)
    Q_PROPERTY(QString systemAccentOrigin READ systemAccentOrigin NOTIFY schemeChanged FINAL)
    Q_PROPERTY(bool systemAccentAvailable READ systemAccentAvailable NOTIFY schemeChanged FINAL)
    Q_PROPERTY(bool softwareRendering READ softwareRendering NOTIFY softwareRenderingChanged FINAL)
    Q_PROPERTY(QString fontFamily READ fontFamily CONSTANT FINAL)
    Q_PROPERTY(QString iconFontFamily READ iconFontFamily CONSTANT FINAL)
    // Seeds to try for the manual colour source (Preferences, component gallery): any other can be picked.
    Q_PROPERTY(QVariantList seedSuggestions READ seedSuggestions CONSTANT FINAL)

public:
    enum class Mode
    {
        Auto,
        Light,
        Dark,
    };
    Q_ENUM(Mode)
    enum class Contrast
    {
        System,
        Standard,
        Medium,
        High,
    };
    Q_ENUM(Contrast)
    enum class Variant
    {
        TonalSpot,
        Vibrant,
        Expressive,
        Neutral,
        Fidelity,
        Content,
        Monochrome,
    };
    Q_ENUM(Variant)
    enum class SeedSource
    {
        System,       // desktop accent color
        Wallpaper,    // dominant color of the desktop wallpaper
        ProjectCover, // cover of the current project
        Manual,       // chosen by the user
        Default,      // built-in palette
    };
    Q_ENUM(SeedSource)
    enum class Density
    {
        Comfortable,
        Compact,
    };
    Q_ENUM(Density)
    enum class Motion
    {
        System,
        Full,
        Reduced,
    };
    Q_ENUM(Motion)

    // Built-in fallback seed (SPEC §4, source 5).
    static constexpr QRgb kDefaultSeed = 0xff4f5bd5;

    // `appearance` may be null (tests): then the system provides nothing.
    explicit ThemeManager(SystemAppearance *appearance, QObject *parent = nullptr);
    ~ThemeManager() override;

    // The application instance returned to QML; must be set before the QML engine loads.
    static void setInstance(ThemeManager *instance);
    static ThemeManager *instance();
    static ThemeManager *create(QQmlEngine *qmlEngine, QJSEngine *jsEngine);

    // Loads the bundled fonts (Inter, Material Symbols). Requires a QGuiApplication.
    static void loadFonts();

    ThemeColors *color() { return &m_colors; }
    ThemeTypography *type() { return &m_typography; }
    ThemeShape *shape() { return &m_shape; }
    ThemeState *state() { return &m_state; }
    ThemeSpace *space() { return &m_space; }
    ThemeEditor *editor() { return &m_editor; }
    ThemeElevation *elevation() { return &m_elevation; }
    ThemeMotion *motion() { return &m_motion; }

    Mode mode() const { return m_mode; }
    void setMode(Mode mode);
    Contrast contrast() const { return m_contrast; }
    void setContrast(Contrast contrast);
    Variant variant() const { return m_variant; }
    void setVariant(Variant variant);
    SeedSource seedSource() const { return m_seedSource; }
    void setSeedSource(SeedSource source);
    QColor manualSeed() const { return m_manualSeed; }
    void setManualSeed(const QColor &color);
    Density density() const { return m_density; }
    void setDensity(Density density);
    Motion motionPreference() const { return m_motionPreference; }
    void setMotionPreference(Motion preference);

    bool dark() const { return m_dark; }
    QColor seedColor() const { return m_seed; }
    SeedSource effectiveSeedSource() const { return m_effectiveSource; }
    QString systemAccentOrigin() const;
    bool systemAccentAvailable() const;
    bool softwareRendering() const { return m_softwareRendering; }
    // Set by the app when Qt Quick runs on the software backend: decorative shadows and blurs are disabled.
    void setSoftwareRendering(bool software);
    QString fontFamily() const;
    QString iconFontFamily() const;
    static QVariantList seedSuggestions();

    // Seed sources computed elsewhere (in background): wallpaper color, current project cover.
    void setWallpaperSeed(const std::optional<QColor> &color);
    void setProjectCoverSeed(const std::optional<QColor> &color);

    // Tonal surface color of an elevation level (0–5), M3 style.
    Q_INVOKABLE QColor surfaceAt(int level) const;
    // Material Symbols glyph for an icon name (via the bundled codepoints file, no ligature shaping needed).
    // Unknown names give the "help" glyph and a warning in the log, instead of drawing the name as text.
    Q_INVOKABLE QString icon(const QString &name) const;
    Q_INVOKABLE bool hasIcon(const QString &name) const;
    // Names of every color role, in declaration order (component gallery, documentation).
    Q_INVOKABLE QStringList colorRoleNames() const;
    // Black or white, whichever reads better on `background` (labels on arbitrary colors, e.g. swatches).
    Q_INVOKABLE QColor readableOn(const QColor &background) const;
    // Session-only overrides (command line, automatic screenshots): not written to the settings.
    void setSessionOverrides(std::optional<Mode> mode, std::optional<Contrast> contrast);
    // `color` with the given opacity (for state layers).
    Q_INVOKABLE QColor alpha(const QColor &color, qreal opacity) const;

    // Target scheme (without animation), for tests.
    const ColorScheme &targetScheme() const { return m_target; }
    // Persistence of the preferences (QSettings, group "theme").
    void loadSettings();
    void saveSettings() const;

signals:
    void settingsChanged();
    void schemeChanged();
    void softwareRenderingChanged();

private:
    void update(bool animate);
    std::pair<QColor, SeedSource> resolveSeed() const;

    QPointer<SystemAppearance> m_appearance;
    ThemeColors m_colors;
    ThemeTypography m_typography;
    ThemeShape m_shape;
    ThemeState m_state;
    ThemeSpace m_space;
    ThemeEditor m_editor;
    ThemeElevation m_elevation;
    ThemeMotion m_motion;
    QVariantAnimation m_animation;
    ColorScheme m_from;
    ColorScheme m_target;

    Mode m_mode = Mode::Auto;
    Contrast m_contrast = Contrast::System;
    Variant m_variant = Variant::TonalSpot;
    SeedSource m_seedSource = SeedSource::System;
    QColor m_manualSeed = QColor::fromRgb(kDefaultSeed);
    Density m_density = Density::Comfortable;
    Motion m_motionPreference = Motion::System;
    std::optional<QColor> m_wallpaperSeed;
    std::optional<QColor> m_coverSeed;

    bool m_dark = true;
    QColor m_seed = QColor::fromRgb(kDefaultSeed);
    SeedSource m_effectiveSource = SeedSource::Default;
    bool m_softwareRendering = false;
    bool m_initialized = false;
    bool m_sessionOverride = false;
};

} // namespace vedit::theme
