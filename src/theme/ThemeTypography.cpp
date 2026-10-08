// SPDX-License-Identifier: GPL-3.0-or-later
#include "ThemeTypography.h"

namespace velacut::theme {

namespace {
struct TypeStyle
{
    qreal size;       // sp
    qreal lineHeight; // sp
    qreal tracking;   // sp
    int weight;
};

// Material 3 baseline type scale, with Apple-style optical tracking: display and headline
// sizes compact slightly (the larger the text, the tighter, like SF Pro), titles stay neutral,
// labels stay slightly open (they are small and dense). Only the tracking was tuned (SF feel),
// sizes, line heights and weights are the M3 baseline.
constexpr TypeStyle kScale[] = {
    {57, 64, -0.5, 400}, // displayLarge
    {45, 52, -0.25, 400}, // displayMedium
    {36, 44, -0.2, 400}, // displaySmall
    {32, 40, -0.5, 400}, // headlineLarge
    {28, 36, -0.25, 400}, // headlineMedium
    {24, 32, -0.2, 400}, // headlineSmall
    {22, 28, -0.15, 400}, // titleLarge
    {16, 24, 0.15, 500}, // titleMedium
    {14, 20, 0.1, 500}, // titleSmall
    {16, 24, 0.5, 400}, // bodyLarge
    {14, 20, 0.25, 400}, // bodyMedium
    {12, 16, 0.4, 400}, // bodySmall
    {14, 20, 0.1, 500}, // labelLarge
    {12, 16, 0.5, 500}, // labelMedium
    {11, 16, 0.5, 500}, // labelSmall
};
static_assert(std::size(kScale) == ThemeTypography::kStyleCount);
} // namespace

void ThemeTypography::configure(const QString &family, qreal scale)
{
    m_family = family;
    m_scale = scale;
    emit changed();
}

QFont ThemeTypography::font(int style) const
{
    const TypeStyle &s = kScale[style];
    QFont font(m_family);
    font.setPixelSize(qRound(s.size * m_scale));
    font.setWeight(static_cast<QFont::Weight>(s.weight));
    font.setLetterSpacing(QFont::AbsoluteSpacing, s.tracking * m_scale);
    font.setHintingPreference(QFont::PreferNoHinting);
    return font;
}

qreal ThemeTypography::lineHeight(int style) const
{
    return kScale[style].lineHeight * m_scale;
}

} // namespace velacut::theme
