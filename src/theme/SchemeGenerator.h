// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "theme/ColorScheme.h"

#include <QColor>
#include <QImage>
#include <QString>

#include <optional>

namespace velacut::theme {

// Scheme variants offered to the user (SPEC §4, "colori dinamici").
enum class SchemeVariant
{
    TonalSpot,
    Vibrant,
    Expressive,
    Neutral,
    Fidelity,
    Content,
    Monochrome,
};

enum class ContrastLevel
{
    Standard,
    Medium,
    High,
};

QString schemeVariantName(SchemeVariant variant);
std::optional<SchemeVariant> schemeVariantFromName(QStringView name);
QString contrastLevelName(ContrastLevel level);
std::optional<ContrastLevel> contrastLevelFromName(QStringView name);

// Generates the full Material 3 scheme from a seed color with material-color-utilities (HCT space).
// `neutralSurfaces`: the surfaces, outlines and text greys come from nearly grey palettes (the seed's hue at a chroma
// of 2–3 instead of the variant's), as the grey surroundings of video editors that keep the eye neutral to judge the
// video's colours; primary, secondary, tertiary and error stay dynamic. Same tones, so the same contrast.
ColorScheme generateScheme(const QColor &seed, SchemeVariant variant, bool dark, ContrastLevel contrast,
                           bool neutralSurfaces = false);

// Most suitable seed color of an image (wallpaper, project cover), using the MCU quantizer and scorer.
// Returns nullopt for an empty image.
std::optional<QColor> seedFromImage(const QImage &image);

} // namespace velacut::theme
