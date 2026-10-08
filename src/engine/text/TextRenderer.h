// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/project/Clip.h"

#include <QImage>
#include <QRectF>
#include <QSize>

namespace velacut::engine {

// Draws a text clip (SPEC §5.7 base text) as a transparent canvas-sized layer, the text block centred on the canvas;
// the clip's transform (vedit.transform) then places it. Font size, stroke, shadow and background are relative to the
// canvas, so the text looks the same at every resolution. CPU (QPainter): works in every process with a
// QGuiApplication (velacut-render runs one on the "offscreen" platform).
class TextRenderer
{
public:
    // Straight-alpha RGBA8888 image of `canvas` size at final/static state.
    static QImage render(const TextClipData &text, QSize canvas);
    // Straight-alpha RGBA8888 image of `canvas` size at content time `timeSeconds` with clip duration `durationSeconds`.
    static QImage render(const TextClipData &text, QSize canvas, double timeSeconds, double durationSeconds);
    // Straight-alpha RGBA8888 image taking normalized animation progress (0.0 to 1.0).
    static QImage render(const TextClipData &text, QSize canvas, double progress);
    // The rectangle of the text block (with its background box/tail) in canvas pixels, before the transform: for the
    // handles on the preview.
    static QRectF bounds(const TextClipData &text, QSize canvas);
};

} // namespace velacut::engine
