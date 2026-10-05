// SPDX-License-Identifier: GPL-3.0-or-later
#include "ThemeManager.h"

#include "common/fonts/BundledFonts.h"

#include <QEasingCurve>
#include <QFile>
#include <QHash>
#include <QSet>
#include <QFontDatabase>
#include <QJSEngine>
#include <QLoggingCategory>
#include <QMetaEnum>
#include <QSettings>

using namespace Qt::StringLiterals;

Q_LOGGING_CATEGORY(lcTheme, "vedit.theme")

namespace vedit::theme {

namespace {

ThemeManager *s_instance = nullptr;

// name -> codepoint, from the codepoints file published next to the Material Symbols font.
const QHash<QString, char32_t> &iconCodepoints()
{
    static const QHash<QString, char32_t> codepoints = [] {
        QHash<QString, char32_t> map;
        QFile file(u":/vedit/icons/MaterialSymbolsRounded.codepoints"_s);
        if (!file.open(QIODevice::ReadOnly)) {
            qCWarning(lcTheme) << "icon codepoints not found";
            return map;
        }
        while (!file.atEnd()) {
            const QList<QByteArray> parts = file.readLine().trimmed().split(' ');
            bool ok = false;
            const uint codepoint = parts.size() == 2 ? parts[1].toUInt(&ok, 16) : 0;
            if (ok) {
                map.insert(QString::fromLatin1(parts[0]), static_cast<char32_t>(codepoint));
            }
        }
        return map;
    }();
    return codepoints;
}
QString s_fontFamily;
QString s_iconFontFamily;

template<typename E>
QString enumKey(E value)
{
    return QString::fromLatin1(QMetaEnum::fromType<E>().valueToKey(static_cast<int>(value)));
}

template<typename E>
E enumFromKey(const QString &key, E fallback)
{
    bool ok = false;
    const int value = QMetaEnum::fromType<E>().keyToValue(key.toLatin1().constData(), &ok);
    return ok ? static_cast<E>(value) : fallback;
}

SchemeVariant toSchemeVariant(ThemeManager::Variant variant)
{
    switch (variant) {
    case ThemeManager::Variant::TonalSpot:
        return SchemeVariant::TonalSpot;
    case ThemeManager::Variant::Vibrant:
        return SchemeVariant::Vibrant;
    case ThemeManager::Variant::Expressive:
        return SchemeVariant::Expressive;
    case ThemeManager::Variant::Neutral:
        return SchemeVariant::Neutral;
    case ThemeManager::Variant::Fidelity:
        return SchemeVariant::Fidelity;
    case ThemeManager::Variant::Content:
        return SchemeVariant::Content;
    case ThemeManager::Variant::Monochrome:
        return SchemeVariant::Monochrome;
    }
    return SchemeVariant::TonalSpot;
}

QEasingCurve emphasizedCurve()
{
    // M3 "emphasized" as two cubic segments (same points as ThemeMotion::emphasized()).
    QEasingCurve curve(QEasingCurve::BezierSpline);
    curve.addCubicBezierSegment({0.05, 0.0}, {0.133333, 0.06}, {0.166666, 0.4});
    curve.addCubicBezierSegment({0.208333, 0.82}, {0.25, 1.0}, {1.0, 1.0});
    return curve;
}

} // namespace

ThemeManager::ThemeManager(SystemAppearance *appearance, QObject *parent)
    : QObject(parent)
    , m_appearance(appearance)
{
    m_animation.setStartValue(0.0);
    m_animation.setEndValue(1.0);
    m_animation.setEasingCurve(emphasizedCurve());
    connect(&m_animation, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
        m_colors.setScheme(ColorScheme::interpolate(m_from, m_target, value.toDouble()));
    });
    if (m_appearance) {
        connect(m_appearance, &SystemAppearance::changed, this, [this] { update(true); });
    }
    m_typography.configure(fontFamily(), 1.0);
    update(false);
}

ThemeManager::~ThemeManager()
{
    if (s_instance == this) {
        s_instance = nullptr;
    }
}

void ThemeManager::setInstance(ThemeManager *instance)
{
    s_instance = instance;
}

ThemeManager *ThemeManager::instance()
{
    return s_instance;
}

ThemeManager *ThemeManager::create(QQmlEngine *, QJSEngine *)
{
    Q_ASSERT_X(s_instance, "ThemeManager::create", "ThemeManager::setInstance() must be called before QML loads");
    QJSEngine::setObjectOwnership(s_instance, QJSEngine::CppOwnership);
    return s_instance;
}

void ThemeManager::loadFonts()
{
    const auto load = [](const QString &resource) -> QString {
        const int id = QFontDatabase::addApplicationFont(resource);
        const QStringList families = QFontDatabase::applicationFontFamilies(id);
        if (id < 0 || families.isEmpty()) {
            qCWarning(lcTheme) << "cannot load bundled font" << resource;
            return {};
        }
        return families.constFirst();
    };
    s_fontFamily = fonts::loadBundled();
    if (s_fontFamily.isEmpty()) {
        qCWarning(lcTheme) << "cannot load the bundled Inter font";
    }
    s_iconFontFamily = load(u":/vedit/icons/MaterialSymbolsRounded.woff2"_s);
    if (s_instance) {
        s_instance->m_typography.configure(s_instance->fontFamily(), 1.0);
    }
}

QString ThemeManager::fontFamily() const
{
    return s_fontFamily.isEmpty() ? u"sans-serif"_s : s_fontFamily;
}

QString ThemeManager::iconFontFamily() const
{
    return s_iconFontFamily;
}

void ThemeManager::setMode(Mode mode)
{
    if (mode != m_mode) {
        m_mode = mode;
        emit settingsChanged();
        update(true);
    }
}

void ThemeManager::setContrast(Contrast contrast)
{
    if (contrast != m_contrast) {
        m_contrast = contrast;
        emit settingsChanged();
        update(true);
    }
}

void ThemeManager::setVariant(Variant variant)
{
    if (variant != m_variant) {
        m_variant = variant;
        emit settingsChanged();
        update(true);
    }
}

void ThemeManager::setNeutralSurfaces(bool neutral)
{
    if (neutral != m_neutralSurfaces) {
        m_neutralSurfaces = neutral;
        emit settingsChanged();
        update(true);
    }
}

void ThemeManager::setSeedSource(SeedSource source)
{
    if (source != m_seedSource) {
        m_seedSource = source;
        emit settingsChanged();
        update(true);
    }
}

void ThemeManager::setManualSeed(const QColor &color)
{
    if (color.isValid() && color.rgb() != m_manualSeed.rgb()) {
        m_manualSeed = QColor::fromRgb(color.rgb());
        emit settingsChanged();
        update(true);
    }
}

void ThemeManager::setDensity(Density density)
{
    if (density != m_density) {
        m_density = density;
        m_space.setDensity(density == Density::Compact ? -1 : 0);
        emit settingsChanged();
    }
}

void ThemeManager::setMotionPreference(Motion preference)
{
    if (preference != m_motionPreference) {
        m_motionPreference = preference;
        emit settingsChanged();
        update(false);
    }
}

void ThemeManager::setSoftwareRendering(bool software)
{
    if (software != m_softwareRendering) {
        m_softwareRendering = software;
        emit softwareRenderingChanged();
    }
}

void ThemeManager::setWallpaperSeed(const std::optional<QColor> &color)
{
    m_wallpaperSeed = color;
    update(true);
}

void ThemeManager::setProjectCoverSeed(const std::optional<QColor> &color)
{
    m_coverSeed = color;
    update(true);
}

QString ThemeManager::systemAccentOrigin() const
{
    return m_appearance ? m_appearance->accentSource() : QString();
}

bool ThemeManager::systemAccentAvailable() const
{
    return m_appearance && m_appearance->accentColor().has_value();
}

std::pair<QColor, ThemeManager::SeedSource> ThemeManager::resolveSeed() const
{
    const std::optional<QColor> system = m_appearance ? m_appearance->accentColor() : std::nullopt;
    switch (m_seedSource) {
    case SeedSource::Manual:
        return {m_manualSeed, SeedSource::Manual};
    case SeedSource::Wallpaper:
        if (m_wallpaperSeed) {
            return {*m_wallpaperSeed, SeedSource::Wallpaper};
        }
        break;
    case SeedSource::ProjectCover:
        if (m_coverSeed) {
            return {*m_coverSeed, SeedSource::ProjectCover};
        }
        break;
    case SeedSource::System:
    case SeedSource::Default:
        break;
    }
    // Unavailable sources fall back to the system accent, then to the built-in palette.
    if (m_seedSource != SeedSource::Default && system) {
        return {*system, SeedSource::System};
    }
    return {QColor::fromRgb(kDefaultSeed), SeedSource::Default};
}

void ThemeManager::update(bool animate)
{
    // Dark by default, as suits a video editor (SPEC §4), unless the user or the system says otherwise.
    bool dark = true;
    if (m_mode == Mode::Light) {
        dark = false;
    } else if (m_mode == Mode::Auto && m_appearance &&
               m_appearance->colorScheme() == SystemAppearance::ColorSchemePreference::Light) {
        dark = false;
    }
    ContrastLevel contrast = ContrastLevel::Standard;
    switch (m_contrast) {
    case Contrast::System:
        contrast = (m_appearance && m_appearance->highContrast()) ? ContrastLevel::High : ContrastLevel::Standard;
        break;
    case Contrast::Standard:
        contrast = ContrastLevel::Standard;
        break;
    case Contrast::Medium:
        contrast = ContrastLevel::Medium;
        break;
    case Contrast::High:
        contrast = ContrastLevel::High;
        break;
    }
    const bool reduced = m_motionPreference == Motion::Reduced ||
                         (m_motionPreference == Motion::System && m_appearance && m_appearance->reducedMotion());
    m_motion.setReduced(reduced);

    const auto [seed, source] = resolveSeed();
    const ColorScheme target = generateScheme(seed, toSchemeVariant(m_variant), dark, contrast, m_neutralSurfaces);
    const bool changed = !m_initialized || target != m_target || dark != m_dark || seed != m_seed ||
                         source != m_effectiveSource;
    m_dark = dark;
    m_seed = seed;
    m_effectiveSource = source;
    if (target != m_target || !m_initialized) {
        m_from = m_colors.scheme();
        m_target = target;
        m_animation.stop();
        if (animate && m_initialized && !reduced) {
            m_animation.setDuration(m_motion.medium4());
            m_animation.start();
        } else {
            m_colors.setScheme(target);
        }
    }
    m_initialized = true;
    if (changed) {
        emit schemeChanged();
    }
}

QColor ThemeManager::surfaceAt(int level) const
{
    const ColorScheme &scheme = m_colors.scheme();
    switch (level) {
    case 0:
        return scheme[ColorRole::Surface];
    case 1:
        return scheme[ColorRole::SurfaceContainerLow];
    case 2:
        return scheme[ColorRole::SurfaceContainer];
    case 3:
        return scheme[ColorRole::SurfaceContainerHigh];
    default:
        return scheme[ColorRole::SurfaceContainerHighest];
    }
}

QString ThemeManager::icon(const QString &name) const
{
    const auto &codepoints = iconCodepoints();
    auto it = codepoints.constFind(name);
    if (it == codepoints.constEnd()) {
        if (!name.isEmpty()) {
            static QSet<QString> reported;
            if (!reported.contains(name)) {
                reported.insert(name);
                qCWarning(lcTheme) << "unknown icon name:" << name;
            }
        }
        if (name.isEmpty()) {
            return {};
        }
        it = codepoints.constFind(u"help"_s);
        if (it == codepoints.constEnd()) {
            return {};
        }
    }
    const char32_t codepoint = *it;
    return QString::fromUcs4(&codepoint, 1);
}

bool ThemeManager::hasIcon(const QString &name) const
{
    return iconCodepoints().contains(name);
}

QVariantList ThemeManager::seedSuggestions()
{
    // The built-in seed first, then a hue wheel of seeds that give distinct schemes in every variant.
    QVariantList seeds;
    for (const QRgb rgb : {kDefaultSeed, QRgb(0xff4f5bd5), QRgb(0xff1e8e3e), QRgb(0xffa8870b), QRgb(0xffe8710a),
                           QRgb(0xffd93025), QRgb(0xffc2185b), QRgb(0xff7b1fa2), QRgb(0xff777777)}) {
        seeds.append(QColor::fromRgba(rgb));
    }
    return seeds;
}

QStringList ThemeManager::colorRoleNames() const
{
    QStringList names;
    for (int i = 0; i < kColorRoleCount; ++i) {
        names.append(QString(colorRoleName(static_cast<ColorRole>(i))));
    }
    return names;
}

QColor ThemeManager::readableOn(const QColor &background) const
{
    return contrastRatio(background, Qt::black) >= contrastRatio(background, Qt::white) ? QColor(Qt::black) : QColor(Qt::white);
}

void ThemeManager::setSessionOverrides(std::optional<Mode> mode, std::optional<Contrast> contrast)
{
    m_sessionOverride = mode.has_value() || contrast.has_value();
    if (mode) {
        m_mode = *mode;
    }
    if (contrast) {
        m_contrast = *contrast;
    }
    emit settingsChanged();
    update(false);
}

QColor ThemeManager::alpha(const QColor &color, qreal opacity) const
{
    QColor result = color;
    result.setAlphaF(static_cast<float>(std::clamp(opacity, 0.0, 1.0) * color.alphaF()));
    return result;
}

void ThemeManager::loadSettings()
{
    QSettings settings;
    settings.beginGroup(u"theme"_s);
    m_mode = enumFromKey(settings.value(u"mode"_s).toString(), Mode::Auto);
    m_contrast = enumFromKey(settings.value(u"contrast"_s).toString(), Contrast::System);
    m_variant = enumFromKey(settings.value(u"variant"_s).toString(), Variant::TonalSpot);
    m_neutralSurfaces = settings.value(u"neutralSurfaces"_s, true).toBool();
    m_seedSource = enumFromKey(settings.value(u"seedSource"_s).toString(), SeedSource::System);
    const QColor manual(settings.value(u"manualSeed"_s).toString());
    if (manual.isValid()) {
        m_manualSeed = manual;
    }
    m_density = enumFromKey(settings.value(u"density"_s).toString(), Density::Comfortable);
    m_space.setDensity(m_density == Density::Compact ? -1 : 0);
    m_motionPreference = enumFromKey(settings.value(u"motion"_s).toString(), Motion::System);
    settings.endGroup();
    emit settingsChanged();
    update(false);
}

void ThemeManager::saveSettings() const
{
    if (m_sessionOverride) {
        return; // values forced from the command line are not the user's preferences
    }
    QSettings settings;
    settings.beginGroup(u"theme"_s);
    settings.setValue(u"mode"_s, enumKey(m_mode));
    settings.setValue(u"contrast"_s, enumKey(m_contrast));
    settings.setValue(u"variant"_s, enumKey(m_variant));
    settings.setValue(u"neutralSurfaces"_s, m_neutralSurfaces);
    settings.setValue(u"seedSource"_s, enumKey(m_seedSource));
    settings.setValue(u"manualSeed"_s, m_manualSeed.name(QColor::HexRgb));
    settings.setValue(u"density"_s, enumKey(m_density));
    settings.setValue(u"motion"_s, enumKey(m_motionPreference));
    settings.endGroup();
}

} // namespace vedit::theme
