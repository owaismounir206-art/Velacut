// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QColor>
#include <QHash>
#include <QImage>
#include <QSize>
#include <QString>

namespace velacut::fx {

// Animated graphic elements (SPEC §5.7) drawn on a transparent canvas-sized layer. CPU, QPainter without fonts (safe
// in MLT's threads): the characters of numbers and clocks come pre-rendered in GraphicGlyphs.
enum class GraphicType
{
    Counter,
    Countdown,
    Timer,
    ProgressBar,
    Arrow,
    Circle,
    Underline,
    Highlighter,
    Check,
    Cross,
};

struct GraphicParams
{
    GraphicType type = GraphicType::Counter;
    double from = 0.0;
    double to = 100.0;
    int decimals = 0;
    QColor color{255, 255, 255};
    QColor color2{255, 255, 255, 80};
    double thickness = 0.5;   // 0–1
    double drawSeconds = 0.6; // hand-drawn marks
};

// The pictures of the characters (digits, separators) and of the prefix and suffix, all `height` pixels high.
struct GraphicGlyphs
{
    QHash<QChar, QImage> glyphs;
    QImage prefix;
    QImage suffix;
    int height = 0;
};

// Whether the type shows text (then GraphicGlyphs are needed), and the characters it may use.
bool graphicHasText(GraphicType type);
QString graphicCharacters();
// The height of the text for a canvas `canvasHeight` pixels high.
int graphicTextHeight(const GraphicParams &params, int canvasHeight);
// The text shown `elapsed` seconds into a clip `total` seconds long (the number without prefix and suffix).
QString graphicText(const GraphicParams &params, double elapsed, double total);

QImage renderGraphic(const GraphicParams &params, double elapsed, double total, const QSize &canvas,
                     const GraphicGlyphs &glyphs);

} // namespace velacut::fx
