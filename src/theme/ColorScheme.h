// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QColor>
#include <QLatin1StringView>

#include <array>

namespace vedit::theme {

// Every Material 3 color role (docs/DESIGN_SYSTEM.md). Generated list: keep in sync with kColorRoleNames.
enum class ColorRole
{
    Primary,
    OnPrimary,
    PrimaryContainer,
    OnPrimaryContainer,
    InversePrimary,
    Secondary,
    OnSecondary,
    SecondaryContainer,
    OnSecondaryContainer,
    Tertiary,
    OnTertiary,
    TertiaryContainer,
    OnTertiaryContainer,
    Error,
    OnError,
    ErrorContainer,
    OnErrorContainer,
    Background,
    OnBackground,
    Surface,
    OnSurface,
    SurfaceVariant,
    OnSurfaceVariant,
    SurfaceDim,
    SurfaceBright,
    SurfaceContainerLowest,
    SurfaceContainerLow,
    SurfaceContainer,
    SurfaceContainerHigh,
    SurfaceContainerHighest,
    InverseSurface,
    InverseOnSurface,
    Outline,
    OutlineVariant,
    Shadow,
    Scrim,
    SurfaceTint,
    PrimaryFixed,
    PrimaryFixedDim,
    OnPrimaryFixed,
    OnPrimaryFixedVariant,
    SecondaryFixed,
    SecondaryFixedDim,
    OnSecondaryFixed,
    OnSecondaryFixedVariant,
    TertiaryFixed,
    TertiaryFixedDim,
    OnTertiaryFixed,
    OnTertiaryFixedVariant,
    Count,
};

inline constexpr int kColorRoleCount = static_cast<int>(ColorRole::Count);

// Role names as used in QML (Theme.color.<name>).
QLatin1StringView colorRoleName(ColorRole role);

// A complete color scheme: one color per role.
struct ColorScheme
{
    std::array<QColor, kColorRoleCount> colors;

    const QColor &operator[](ColorRole role) const { return colors[static_cast<size_t>(role)]; }
    QColor &operator[](ColorRole role) { return colors[static_cast<size_t>(role)]; }

    // Linear interpolation of every role in sRGB (used for the animated scheme change).
    static ColorScheme interpolate(const ColorScheme &from, const ColorScheme &to, double t);

    friend bool operator==(const ColorScheme &, const ColorScheme &) = default;
};

// WCAG 2.x contrast ratio between two opaque colors (1..21).
double contrastRatio(const QColor &a, const QColor &b);

} // namespace vedit::theme
