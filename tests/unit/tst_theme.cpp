// SPDX-License-Identifier: GPL-3.0-or-later
#include "theme/SchemeGenerator.h"
#include "theme/SystemAppearance.h"
#include "theme/ThemeManager.h"

#include <QFont>
#include <QFontDatabase>
#include <QFontInfo>
#include <QImage>
#include <QPainter>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace vedit::theme;
using namespace Qt::StringLiterals;

namespace {

struct Pair
{
    ColorRole foreground;
    ColorRole background;
};

// Text/icon roles and the containers they are drawn on.
const Pair kTextPairs[] = {
    {ColorRole::OnPrimary, ColorRole::Primary},
    {ColorRole::OnPrimaryContainer, ColorRole::PrimaryContainer},
    {ColorRole::OnSecondary, ColorRole::Secondary},
    {ColorRole::OnSecondaryContainer, ColorRole::SecondaryContainer},
    {ColorRole::OnTertiary, ColorRole::Tertiary},
    {ColorRole::OnTertiaryContainer, ColorRole::TertiaryContainer},
    {ColorRole::OnError, ColorRole::Error},
    {ColorRole::OnErrorContainer, ColorRole::ErrorContainer},
    {ColorRole::OnSurface, ColorRole::Surface},
    {ColorRole::OnSurface, ColorRole::SurfaceContainerHighest},
    {ColorRole::OnSurfaceVariant, ColorRole::Surface},
    {ColorRole::InverseOnSurface, ColorRole::InverseSurface},
};

const QColor kSeeds[] = {QColor(0x4f5bd5), QColor(0xe62d42), QColor(0x3a944a), QColor(0xc88800), QColor(0x777777)};
const SchemeVariant kVariants[] = {SchemeVariant::TonalSpot, SchemeVariant::Vibrant,  SchemeVariant::Expressive,
                                   SchemeVariant::Neutral,   SchemeVariant::Fidelity, SchemeVariant::Content,
                                   SchemeVariant::Monochrome};

} // namespace

class TestTheme : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QCoreApplication::setOrganizationName(u"vedit"_s);
        QCoreApplication::setApplicationName(u"vedit-tests"_s);
    }

    void everyRoleIsGenerated()
    {
        const ColorScheme scheme = generateScheme(kSeeds[0], SchemeVariant::TonalSpot, true, ContrastLevel::Standard);
        for (int i = 0; i < kColorRoleCount; ++i) {
            const auto role = static_cast<ColorRole>(i);
            QVERIFY2(scheme[role].isValid(), colorRoleName(role).data());
            QVERIFY2(!colorRoleName(role).isEmpty(), "role without name");
        }
        QCOMPARE(colorRoleName(ColorRole::SurfaceContainerHighest), "surfaceContainerHighest"_L1);
    }

    // SPEC §4 accessibility: 4.5:1 for text (M3 standard contrast), 7:1 with high contrast.
    void textContrastRequirements()
    {
        for (const QColor &seed : kSeeds) {
            for (SchemeVariant variant : kVariants) {
                for (bool dark : {false, true}) {
                    for (ContrastLevel level : {ContrastLevel::Standard, ContrastLevel::High}) {
                      for (bool neutral : {false, true}) {
                        const ColorScheme scheme = generateScheme(seed, variant, dark, level, neutral);
                        const double minimum = level == ContrastLevel::High ? 7.0 : 4.5;
                        for (const Pair &pair : kTextPairs) {
                            const double ratio = contrastRatio(scheme[pair.foreground], scheme[pair.background]);
                            if (ratio < minimum - 0.05) {
                                QFAIL(qPrintable(u"%1 on %2: %3 < %4 (seed %5, variant %6, dark %7)"_s
                                                     .arg(colorRoleName(pair.foreground), colorRoleName(pair.background))
                                                     .arg(ratio, 0, 'f', 2)
                                                     .arg(minimum)
                                                     .arg(seed.name(), schemeVariantName(variant))
                                                     .arg(dark)));
                            }
                        }
                      }
                    }
                }
            }
        }
    }

    // Grey surfaces (video editors): the surfaces lose their tint, the accents keep it.
    void neutralSurfacesKeepTheAccent()
    {
        // How far from grey: the spread of the channels (0 = grey, 1 = a pure colour).
        const auto chroma = [](const QColor &c) {
            return (std::max({c.red(), c.green(), c.blue()}) - std::min({c.red(), c.green(), c.blue()})) / 255.0;
        };
        for (const QColor &seed : kSeeds) {
            const ColorScheme tinted = generateScheme(seed, SchemeVariant::Vibrant, true, ContrastLevel::Standard);
            const ColorScheme grey = generateScheme(seed, SchemeVariant::Vibrant, true, ContrastLevel::Standard, true);
            QCOMPARE(grey[ColorRole::Primary], tinted[ColorRole::Primary]);
            QCOMPARE(grey[ColorRole::Tertiary], tinted[ColorRole::Tertiary]);
            for (const ColorRole role : {ColorRole::Surface, ColorRole::SurfaceContainer, ColorRole::SurfaceContainerHighest,
                                         ColorRole::OnSurfaceVariant, ColorRole::OutlineVariant}) {
                QVERIFY2(chroma(grey[role]) <= chroma(tinted[role]) + 1e-6, colorRoleName(role).data());
                QVERIFY2(chroma(grey[role]) < 0.07, colorRoleName(role).data());
            }
        }
    }

    void darkAndLightSurfaces()
    {
        const ColorScheme dark = generateScheme(kSeeds[1], SchemeVariant::TonalSpot, true, ContrastLevel::Standard);
        const ColorScheme light = generateScheme(kSeeds[1], SchemeVariant::TonalSpot, false, ContrastLevel::Standard);
        QVERIFY(dark[ColorRole::Surface].lightnessF() < 0.2);
        QVERIFY(light[ColorRole::Surface].lightnessF() > 0.85);
        // Surface containers get lighter with the level in dark mode.
        QVERIFY(dark[ColorRole::SurfaceContainerLowest].lightnessF() < dark[ColorRole::SurfaceContainerHighest].lightnessF());
    }

    void seedDrivesThePalette()
    {
        const ColorScheme red = generateScheme(QColor(0xe62d42), SchemeVariant::Fidelity, false, ContrastLevel::Standard);
        const ColorScheme green = generateScheme(QColor(0x3a944a), SchemeVariant::Fidelity, false, ContrastLevel::Standard);
        QVERIFY(red != green);
        const int redHue = red[ColorRole::Primary].hsvHue();
        const int greenHue = green[ColorRole::Primary].hsvHue();
        QVERIFY2(redHue < 20 || redHue > 340, qPrintable(QString::number(redHue)));
        QVERIFY2(greenHue > 90 && greenHue < 160, qPrintable(QString::number(greenHue)));
        // Deterministic.
        QVERIFY(red == generateScheme(QColor(0xe62d42), SchemeVariant::Fidelity, false, ContrastLevel::Standard));
        // Monochrome has no chroma.
        const ColorScheme mono = generateScheme(QColor(0xe62d42), SchemeVariant::Monochrome, false, ContrastLevel::Standard);
        QVERIFY(mono[ColorRole::Primary].hsvSaturation() < 10);
    }

    void seedFromImageFindsDominantColor()
    {
        QImage image(200, 100, QImage::Format_ARGB32);
        image.fill(Qt::white);
        QPainter painter(&image);
        painter.fillRect(0, 0, 150, 100, QColor(0x1e40ff));
        painter.end();
        const auto seed = seedFromImage(image);
        QVERIFY(seed.has_value());
        QVERIFY2(seed->hsvHue() > 210 && seed->hsvHue() < 250, qPrintable(seed->name()));
        QVERIFY(!seedFromImage(QImage()).has_value());
    }

    void interpolation()
    {
        const ColorScheme a = generateScheme(kSeeds[0], SchemeVariant::TonalSpot, true, ContrastLevel::Standard);
        const ColorScheme b = generateScheme(kSeeds[1], SchemeVariant::TonalSpot, false, ContrastLevel::Standard);
        QCOMPARE(ColorScheme::interpolate(a, b, 0.0)[ColorRole::Primary].rgb(), a[ColorRole::Primary].rgb());
        QCOMPARE(ColorScheme::interpolate(a, b, 1.0)[ColorRole::Primary].rgb(), b[ColorRole::Primary].rgb());
    }

    void appearanceParsers()
    {
        QCOMPARE(SystemAppearance::parsePortalAccent(1.0, 0.0, 0.5)->rgb(), QColor::fromRgbF(1.0f, 0.0f, 0.5f).rgb());
        QVERIFY(!SystemAppearance::parsePortalAccent(-1.0, 0.0, 0.0).has_value()); // "no accent"
        QCOMPARE(SystemAppearance::parseGnomeAccent(u"'blue'\n"_s), std::optional(QColor(0x3584e4)));
        QCOMPARE(SystemAppearance::parseGnomeAccent(u"'slate'"_s), std::optional(QColor(0x6f8396)));
        QVERIFY(!SystemAppearance::parseGnomeAccent(u"'magenta'"_s).has_value());
        QCOMPARE(SystemAppearance::parseKdeAccent(u"[Colors:View]\nAccentColor=1,2,3\n[General]\nName=x\nAccentColor=61,174,233\n"_s),
                 std::optional(QColor(61, 174, 233)));
        QVERIFY(!SystemAppearance::parseKdeAccent(u"[General]\nAccentColor=300,1,1\n"_s).has_value());

        QTemporaryDir dir;
        const QString wallpaper = dir.filePath(u"wall paper.png"_s);
        QImage(4, 4, QImage::Format_RGB32).save(wallpaper);
        const QString uri = QUrl::fromLocalFile(wallpaper).toString(QUrl::FullyEncoded);
        QCOMPARE(SystemAppearance::parseGnomeWallpaper(u"'"_s + uri + u"'\n"_s), std::optional(wallpaper));
        QCOMPARE(SystemAppearance::parsePlasmaWallpaper(u"[Containments][1][Wallpaper][org.kde.image][General]\nImage="_s + uri + u'\n'),
                 std::optional(wallpaper));
        QCOMPARE(SystemAppearance::parseHyprpaper(u"eDP-1 = "_s + wallpaper + u'\n'), std::optional(wallpaper));
        QCOMPARE(SystemAppearance::parseSwww(u"eDP-1: 1920x1080, scale: 1, currently displaying: image: "_s + wallpaper),
                 std::optional(wallpaper));
        QVERIFY(!SystemAppearance::parseHyprpaper(u"eDP-1 = /does/not/exist.png"_s).has_value());
    }

    void bundledFontsLoad()
    {
        ThemeManager::loadFonts();
        ThemeManager theme(nullptr);
        QCOMPARE(theme.fontFamily(), u"Inter Variable"_s);
        QCOMPARE(theme.iconFontFamily(), u"Material Symbols Rounded"_s);
        // Italic comes from the WOFF2 file: Qt must be able to load WOFF2.
        QVERIFY(QFontDatabase::styles(u"Inter Variable"_s).join(u' ').contains(u"Italic"_s));
        // Projects and packs name it "Inter": that must be the bundled font, not whatever the system substitutes.
        QCOMPARE(QFontInfo(QFont(u"Inter"_s)).family(), u"Inter Variable"_s);
        const QFont body = theme.type()->bodyMedium();
        QCOMPARE(body.pixelSize(), 14);
        QCOMPARE(theme.type()->bodyMediumLineHeight(), 20.0);
        QCOMPARE(theme.type()->labelLarge().weight(), QFont::Medium);
    }

    void iconsResolveThroughCodepoints()
    {
        ThemeManager theme(nullptr);
        QVERIFY(theme.hasIcon(u"play_arrow"_s));
        QCOMPARE(theme.icon(u"play_arrow"_s), QString(QChar(0xe037)));
        QVERIFY(!theme.hasIcon(u"magnet"_s));
        QCOMPARE(theme.icon(u"magnet"_s), theme.icon(u"help"_s)); // never drawn as text
        QVERIFY(theme.icon(QString()).isEmpty());
        // Codepoints beyond the BMP become surrogate pairs.
        QCOMPARE(theme.icon(u"10k"_s).size(), 1);
    }

    void managerDefaultsAndFallbacks()
    {
        ThemeManager theme(nullptr); // no system information at all
        QVERIFY(theme.dark());       // dark by default
        QCOMPARE(theme.effectiveSeedSource(), ThemeManager::SeedSource::Default);
        QCOMPARE(theme.seedColor().rgb(), ThemeManager::kDefaultSeed);
        QVERIFY(theme.color()->primary().isValid());
        // A source that is not available falls back to the built-in palette.
        theme.setSeedSource(ThemeManager::SeedSource::Wallpaper);
        QCOMPARE(theme.effectiveSeedSource(), ThemeManager::SeedSource::Default);
        theme.setWallpaperSeed(QColor(0x3a944a));
        QCOMPARE(theme.effectiveSeedSource(), ThemeManager::SeedSource::Wallpaper);
        QCOMPARE(theme.seedColor(), QColor(0x3a944a));
    }

    void schemeChangeIsAnimatedUnlessReduced()
    {
        ThemeManager theme(nullptr);
        const QColor before = theme.color()->surface();
        QSignalSpy spy(theme.color(), &ThemeColors::changed);
        theme.setMode(ThemeManager::Mode::Light);
        QVERIFY(!theme.dark());
        // Animated: the final color arrives after several intermediate updates.
        QTRY_COMPARE(theme.color()->surface(), theme.targetScheme()[ColorRole::Surface]);
        QVERIFY(spy.count() > 2);
        QVERIFY(theme.color()->surface() != before);

        theme.setMotionPreference(ThemeManager::Motion::Reduced);
        QCOMPARE(theme.motion()->medium2(), 0);
        theme.setMode(ThemeManager::Mode::Dark);
        QCOMPARE(theme.color()->surface(), theme.targetScheme()[ColorRole::Surface]); // immediate
        QCOMPARE(theme.motion()->essentialMedium(), 300);                             // essential motion stays
    }

    void densityAdjustsControls()
    {
        ThemeManager theme(nullptr);
        QCOMPARE(theme.space()->control(40), 40.0);
        theme.setDensity(ThemeManager::Density::Compact);
        QCOMPARE(theme.space()->control(40), 36.0);
        QCOMPARE(theme.space()->minimumTarget(), 40.0);
    }

    void settingsPersist()
    {
        {
            ThemeManager theme(nullptr);
            theme.setMode(ThemeManager::Mode::Light);
            theme.setVariant(ThemeManager::Variant::Expressive);
            theme.setSeedSource(ThemeManager::SeedSource::Manual);
            theme.setManualSeed(QColor(0x123456));
            theme.setContrast(ThemeManager::Contrast::High);
            theme.saveSettings();
        }
        ThemeManager restored(nullptr);
        restored.loadSettings();
        QCOMPARE(restored.mode(), ThemeManager::Mode::Light);
        QCOMPARE(restored.variant(), ThemeManager::Variant::Expressive);
        QCOMPARE(restored.seedSource(), ThemeManager::SeedSource::Manual);
        QCOMPARE(restored.seedColor(), QColor(0x123456));
        QCOMPARE(restored.contrast(), ThemeManager::Contrast::High);
        // The test runs with XDG_CONFIG_HOME inside the build tree: nothing is written to the real home.
        QVERIFY(QSettings().fileName().contains(u"/test-home/"_s));
    }

    void surfaceAtFollowsElevation()
    {
        ThemeManager theme(nullptr);
        QCOMPARE(theme.surfaceAt(0), theme.color()->surface());
        QCOMPARE(theme.surfaceAt(3), theme.color()->surfaceContainerHigh());
        QVERIFY(qAbs(theme.alpha(QColor(Qt::red), 0.5).alphaF() - 0.5f) < 0.001f);
    }
};

QTEST_MAIN(TestTheme)
#include "tst_theme.moc"
