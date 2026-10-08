// SPDX-License-Identifier: GPL-3.0-or-later
// Probe program: prints what velacut reads from the desktop (portal, accent fallbacks, wallpaper) and the
// resulting theme seed. Read-only. Run: ./build/tools/probes/appearance_probe
#include "common/DevSandbox.h"
#include "theme/SchemeGenerator.h"
#include "theme/SystemAppearance.h"
#include "theme/ThemeManager.h"

#include <QGuiApplication>
#include <QImageReader>
#include <QTextStream>

using namespace velacut::theme;

int main(int argc, char **argv)
{
    velacut::applyDevSandbox();
    QGuiApplication app(argc, argv);
    QTextStream out(stdout);
    SystemAppearance appearance;
    appearance.refresh();
    const char *schemes[] = {"no preference", "dark", "light"};
    out << "color-scheme:   " << schemes[static_cast<int>(appearance.colorScheme())] << '\n';
    out << "high contrast:  " << (appearance.highContrast() ? "yes" : "no") << '\n';
    out << "reduced motion: " << (appearance.reducedMotion() ? "yes" : "no") << '\n';
    out << "accent color:   " << (appearance.accentColor() ? appearance.accentColor()->name() : QStringLiteral("none"))
        << " (source: " << (appearance.accentSource().isEmpty() ? QStringLiteral("-") : appearance.accentSource()) << ")\n";
    const auto wallpaper = SystemAppearance::wallpaperPath();
    out << "wallpaper:      " << wallpaper.value_or(QStringLiteral("not found")) << '\n';
    if (wallpaper) {
        QImageReader reader(*wallpaper);
        reader.setScaledSize(QSize(256, 256 * reader.size().height() / qMax(1, reader.size().width())));
        const auto seed = seedFromImage(reader.read());
        out << "wallpaper seed: " << (seed ? seed->name() : QStringLiteral("none")) << '\n';
    }
    ThemeManager theme(&appearance);
    out << "theme: dark=" << theme.dark() << " seed=" << theme.seedColor().name()
        << " primary=" << theme.color()->primary().name() << " surface=" << theme.color()->surface().name() << '\n';
    return 0;
}
