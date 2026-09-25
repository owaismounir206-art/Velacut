// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/project/Clip.h"

#include <QImage>
#include <QRectF>
#include <QSize>

namespace vedit::engine {

// Draws a text clip (SPEC §5.7 base text) as a transparent canvas-sized layer, the text block centred on the canvas;
// the clip's transform (vedit.transform) then places it. Font size, stroke, shadow and background are relative to the
// canvas, so the text looks the same at every resolution. CPU (QPainter): works in every process with a
// QGuiApplication (vedit-render runs one on the "offscreen" platform).
class TextRenderer
{
public:
    // Straight-alpha RGBA8888 image of `canvas` size.
    static QImage render(const TextClipData &text, QSize canvas);
    // The rectangle of the text block (with its background box) in canvas pixels, before the transform: for the
    // handles on the preview.
    static QRectF bounds(const TextClipData &text, QSize canvas);
};

} // namespace vedit::engine
