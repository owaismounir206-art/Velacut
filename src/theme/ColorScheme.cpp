// SPDX-License-Identifier: GPL-3.0-or-later
#include "ColorScheme.h"

#include <cmath>

using namespace Qt::StringLiterals;

namespace velacut::theme {

namespace {
constexpr QLatin1StringView kColorRoleNames[] = {
    "primary"_L1,
    "onPrimary"_L1,
    "primaryContainer"_L1,
    "onPrimaryContainer"_L1,
    "inversePrimary"_L1,
    "secondary"_L1,
    "onSecondary"_L1,
    "secondaryContainer"_L1,
    "onSecondaryContainer"_L1,
    "tertiary"_L1,
    "onTertiary"_L1,
    "tertiaryContainer"_L1,
    "onTertiaryContainer"_L1,
    "error"_L1,
    "onError"_L1,
    "errorContainer"_L1,
    "onErrorContainer"_L1,
    "background"_L1,
    "onBackground"_L1,
    "surface"_L1,
    "onSurface"_L1,
    "surfaceVariant"_L1,
    "onSurfaceVariant"_L1,
    "surfaceDim"_L1,
    "surfaceBright"_L1,
    "surfaceContainerLowest"_L1,
    "surfaceContainerLow"_L1,
    "surfaceContainer"_L1,
    "surfaceContainerHigh"_L1,
    "surfaceContainerHighest"_L1,
    "inverseSurface"_L1,
    "inverseOnSurface"_L1,
    "outline"_L1,
    "outlineVariant"_L1,
    "shadow"_L1,
    "scrim"_L1,
    "surfaceTint"_L1,
    "primaryFixed"_L1,
    "primaryFixedDim"_L1,
    "onPrimaryFixed"_L1,
    "onPrimaryFixedVariant"_L1,
    "secondaryFixed"_L1,
    "secondaryFixedDim"_L1,
    "onSecondaryFixed"_L1,
    "onSecondaryFixedVariant"_L1,
    "tertiaryFixed"_L1,
    "tertiaryFixedDim"_L1,
    "onTertiaryFixed"_L1,
    "onTertiaryFixedVariant"_L1,
};
static_assert(std::size(kColorRoleNames) == kColorRoleCount);

double channelLuminance(double c)
{
    return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
}

double relativeLuminance(const QColor &color)
{
    return 0.2126 * channelLuminance(color.redF()) + 0.7152 * channelLuminance(color.greenF()) +
           0.0722 * channelLuminance(color.blueF());
}
} // namespace

QLatin1StringView colorRoleName(ColorRole role)
{
    return kColorRoleNames[static_cast<size_t>(role)];
}

ColorScheme ColorScheme::interpolate(const ColorScheme &from, const ColorScheme &to, double t)
{
    ColorScheme result;
    for (size_t i = 0; i < result.colors.size(); ++i) {
        const QColor &a = from.colors[i];
        const QColor &b = to.colors[i];
        result.colors[i] = QColor::fromRgbF(static_cast<float>(a.redF() + (b.redF() - a.redF()) * t),
                                            static_cast<float>(a.greenF() + (b.greenF() - a.greenF()) * t),
                                            static_cast<float>(a.blueF() + (b.blueF() - a.blueF()) * t),
                                            static_cast<float>(a.alphaF() + (b.alphaF() - a.alphaF()) * t));
    }
    return result;
}

double contrastRatio(const QColor &a, const QColor &b)
{
    const double la = relativeLuminance(a);
    const double lb = relativeLuminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

} // namespace velacut::theme
