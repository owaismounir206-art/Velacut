// SPDX-License-Identifier: GPL-3.0-or-later
#include "SchemeGenerator.h"

#include "cpp/dynamiccolor/material_dynamic_colors.h"
#include "cpp/quantize/celebi.h"
#include "cpp/scheme/scheme_content.h"
#include "cpp/scheme/scheme_expressive.h"
#include "cpp/scheme/scheme_fidelity.h"
#include "cpp/scheme/scheme_monochrome.h"
#include "cpp/scheme/scheme_neutral.h"
#include "cpp/scheme/scheme_tonal_spot.h"
#include "cpp/scheme/scheme_vibrant.h"
#include "cpp/score/score.h"

#include <algorithm>
#include <memory>

using namespace Qt::StringLiterals;
namespace mcu = material_color_utilities;

namespace vedit::theme {

namespace {

struct RoleBinding
{
    ColorRole role;
    mcu::DynamicColor (*color)();
};

const RoleBinding kBindings[] = {
        {ColorRole::Primary, &mcu::MaterialDynamicColors::Primary},
        {ColorRole::OnPrimary, &mcu::MaterialDynamicColors::OnPrimary},
        {ColorRole::PrimaryContainer, &mcu::MaterialDynamicColors::PrimaryContainer},
        {ColorRole::OnPrimaryContainer, &mcu::MaterialDynamicColors::OnPrimaryContainer},
        {ColorRole::InversePrimary, &mcu::MaterialDynamicColors::InversePrimary},
        {ColorRole::Secondary, &mcu::MaterialDynamicColors::Secondary},
        {ColorRole::OnSecondary, &mcu::MaterialDynamicColors::OnSecondary},
        {ColorRole::SecondaryContainer, &mcu::MaterialDynamicColors::SecondaryContainer},
        {ColorRole::OnSecondaryContainer, &mcu::MaterialDynamicColors::OnSecondaryContainer},
        {ColorRole::Tertiary, &mcu::MaterialDynamicColors::Tertiary},
        {ColorRole::OnTertiary, &mcu::MaterialDynamicColors::OnTertiary},
        {ColorRole::TertiaryContainer, &mcu::MaterialDynamicColors::TertiaryContainer},
        {ColorRole::OnTertiaryContainer, &mcu::MaterialDynamicColors::OnTertiaryContainer},
        {ColorRole::Error, &mcu::MaterialDynamicColors::Error},
        {ColorRole::OnError, &mcu::MaterialDynamicColors::OnError},
        {ColorRole::ErrorContainer, &mcu::MaterialDynamicColors::ErrorContainer},
        {ColorRole::OnErrorContainer, &mcu::MaterialDynamicColors::OnErrorContainer},
        {ColorRole::Background, &mcu::MaterialDynamicColors::Background},
        {ColorRole::OnBackground, &mcu::MaterialDynamicColors::OnBackground},
        {ColorRole::Surface, &mcu::MaterialDynamicColors::Surface},
        {ColorRole::OnSurface, &mcu::MaterialDynamicColors::OnSurface},
        {ColorRole::SurfaceVariant, &mcu::MaterialDynamicColors::SurfaceVariant},
        {ColorRole::OnSurfaceVariant, &mcu::MaterialDynamicColors::OnSurfaceVariant},
        {ColorRole::SurfaceDim, &mcu::MaterialDynamicColors::SurfaceDim},
        {ColorRole::SurfaceBright, &mcu::MaterialDynamicColors::SurfaceBright},
        {ColorRole::SurfaceContainerLowest, &mcu::MaterialDynamicColors::SurfaceContainerLowest},
        {ColorRole::SurfaceContainerLow, &mcu::MaterialDynamicColors::SurfaceContainerLow},
        {ColorRole::SurfaceContainer, &mcu::MaterialDynamicColors::SurfaceContainer},
        {ColorRole::SurfaceContainerHigh, &mcu::MaterialDynamicColors::SurfaceContainerHigh},
        {ColorRole::SurfaceContainerHighest, &mcu::MaterialDynamicColors::SurfaceContainerHighest},
        {ColorRole::InverseSurface, &mcu::MaterialDynamicColors::InverseSurface},
        {ColorRole::InverseOnSurface, &mcu::MaterialDynamicColors::InverseOnSurface},
        {ColorRole::Outline, &mcu::MaterialDynamicColors::Outline},
        {ColorRole::OutlineVariant, &mcu::MaterialDynamicColors::OutlineVariant},
        {ColorRole::Shadow, &mcu::MaterialDynamicColors::Shadow},
        {ColorRole::Scrim, &mcu::MaterialDynamicColors::Scrim},
        {ColorRole::SurfaceTint, &mcu::MaterialDynamicColors::SurfaceTint},
        {ColorRole::PrimaryFixed, &mcu::MaterialDynamicColors::PrimaryFixed},
        {ColorRole::PrimaryFixedDim, &mcu::MaterialDynamicColors::PrimaryFixedDim},
        {ColorRole::OnPrimaryFixed, &mcu::MaterialDynamicColors::OnPrimaryFixed},
        {ColorRole::OnPrimaryFixedVariant, &mcu::MaterialDynamicColors::OnPrimaryFixedVariant},
        {ColorRole::SecondaryFixed, &mcu::MaterialDynamicColors::SecondaryFixed},
        {ColorRole::SecondaryFixedDim, &mcu::MaterialDynamicColors::SecondaryFixedDim},
        {ColorRole::OnSecondaryFixed, &mcu::MaterialDynamicColors::OnSecondaryFixed},
        {ColorRole::OnSecondaryFixedVariant, &mcu::MaterialDynamicColors::OnSecondaryFixedVariant},
        {ColorRole::TertiaryFixed, &mcu::MaterialDynamicColors::TertiaryFixed},
        {ColorRole::TertiaryFixedDim, &mcu::MaterialDynamicColors::TertiaryFixedDim},
        {ColorRole::OnTertiaryFixed, &mcu::MaterialDynamicColors::OnTertiaryFixed},
        {ColorRole::OnTertiaryFixedVariant, &mcu::MaterialDynamicColors::OnTertiaryFixedVariant}
};
static_assert(std::size(kBindings) == kColorRoleCount);

constexpr std::pair<SchemeVariant, QLatin1StringView> kVariantNames[] = {
    {SchemeVariant::TonalSpot, "tonalSpot"_L1}, {SchemeVariant::Vibrant, "vibrant"_L1},
    {SchemeVariant::Expressive, "expressive"_L1}, {SchemeVariant::Neutral, "neutral"_L1},
    {SchemeVariant::Fidelity, "fidelity"_L1},   {SchemeVariant::Content, "content"_L1},
    {SchemeVariant::Monochrome, "monochrome"_L1}};

constexpr std::pair<ContrastLevel, QLatin1StringView> kContrastNames[] = {
    {ContrastLevel::Standard, "standard"_L1}, {ContrastLevel::Medium, "medium"_L1}, {ContrastLevel::High, "high"_L1}};

double contrastValue(ContrastLevel level)
{
    switch (level) {
    case ContrastLevel::Standard:
        return 0.0;
    case ContrastLevel::Medium:
        return 0.5;
    case ContrastLevel::High:
        return 1.0;
    }
    return 0.0;
}

std::unique_ptr<mcu::DynamicScheme> makeScheme(const mcu::Hct &source, SchemeVariant variant, bool dark, double contrast)
{
    switch (variant) {
    case SchemeVariant::TonalSpot:
        return std::make_unique<mcu::SchemeTonalSpot>(source, dark, contrast);
    case SchemeVariant::Vibrant:
        return std::make_unique<mcu::SchemeVibrant>(source, dark, contrast);
    case SchemeVariant::Expressive:
        return std::make_unique<mcu::SchemeExpressive>(source, dark, contrast);
    case SchemeVariant::Neutral:
        return std::make_unique<mcu::SchemeNeutral>(source, dark, contrast);
    case SchemeVariant::Fidelity:
        return std::make_unique<mcu::SchemeFidelity>(source, dark, contrast);
    case SchemeVariant::Content:
        return std::make_unique<mcu::SchemeContent>(source, dark, contrast);
    case SchemeVariant::Monochrome:
        return std::make_unique<mcu::SchemeMonochrome>(source, dark, contrast);
    }
    return std::make_unique<mcu::SchemeTonalSpot>(source, dark, contrast);
}

} // namespace

QString schemeVariantName(SchemeVariant variant)
{
    for (const auto &[value, name] : kVariantNames) {
        if (value == variant) {
            return name;
        }
    }
    return {};
}

std::optional<SchemeVariant> schemeVariantFromName(QStringView name)
{
    for (const auto &[value, text] : kVariantNames) {
        if (name == text) {
            return value;
        }
    }
    return std::nullopt;
}

QString contrastLevelName(ContrastLevel level)
{
    for (const auto &[value, name] : kContrastNames) {
        if (value == level) {
            return name;
        }
    }
    return {};
}

std::optional<ContrastLevel> contrastLevelFromName(QStringView name)
{
    for (const auto &[value, text] : kContrastNames) {
        if (name == text) {
            return value;
        }
    }
    return std::nullopt;
}

ColorScheme generateScheme(const QColor &seed, SchemeVariant variant, bool dark, ContrastLevel contrast,
                           bool neutralSurfaces)
{
    const mcu::Hct source(static_cast<mcu::Argb>(seed.rgb() | 0xff000000u));
    const auto scheme = makeScheme(source, variant, dark, contrastValue(contrast));
    if (neutralSurfaces) {
        constexpr double kNeutralChroma = 2.0;
        constexpr double kNeutralVariantChroma = 3.0;
        const double hue = source.get_hue();
        scheme->neutral_palette = mcu::TonalPalette(hue, std::min(kNeutralChroma, scheme->neutral_palette.get_chroma()));
        scheme->neutral_variant_palette =
            mcu::TonalPalette(hue, std::min(kNeutralVariantChroma, scheme->neutral_variant_palette.get_chroma()));
    }
    ColorScheme result;
    for (const RoleBinding &binding : kBindings) {
        const mcu::Argb argb = binding.color().GetArgb(*scheme);
        result[binding.role] = QColor::fromRgba(argb);
    }
    return result;
}

std::optional<QColor> seedFromImage(const QImage &image)
{
    if (image.isNull()) {
        return std::nullopt;
    }
    // A small copy is enough for the dominant color and keeps the quantizer fast (MCU recommends ~112x112).
    const QImage small = image.scaled(112, 112, Qt::KeepAspectRatio, Qt::SmoothTransformation)
                             .convertToFormat(QImage::Format_ARGB32);
    std::vector<mcu::Argb> pixels;
    pixels.reserve(static_cast<size_t>(small.width()) * static_cast<size_t>(small.height()));
    for (int y = 0; y < small.height(); ++y) {
        const auto *line = reinterpret_cast<const QRgb *>(small.constScanLine(y));
        for (int x = 0; x < small.width(); ++x) {
            if (qAlpha(line[x]) == 255) {
                pixels.push_back(line[x]);
            }
        }
    }
    if (pixels.empty()) {
        return std::nullopt;
    }
    const mcu::QuantizerResult quantized = mcu::QuantizeCelebi(pixels, 128);
    const std::vector<mcu::Argb> ranked = mcu::RankedSuggestions(quantized.color_to_count);
    if (ranked.empty()) {
        return std::nullopt;
    }
    return QColor::fromRgba(ranked.front());
}

} // namespace vedit::theme
