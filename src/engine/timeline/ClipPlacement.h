// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/project/Clip.h"
#include "core/project/Media.h"

#include <QPointF>
#include <QSize>
#include <QSizeF>

namespace velacut::engine {

// Where a clip appears on the canvas: the geometry vedit.transform renders, shared with the handles on the preview.

// Size of a media item as displayed (rotation and pixel aspect applied), in its pixels.
QSizeF displaySize(const Media &media);

// Size of a source at scale 1 on a canvas, per fit mode.
QSizeF fittedSize(QSizeF source, FitMode fit, QSize canvas);

// The visible box of a clip on the canvas, in canvas pixels: its centre, its size (scale applied) and its rotation
// (degrees clockwise). For a media clip the whole picture (crop not applied); for a text the text block with its
// background box.
struct CanvasBox
{
    QPointF centre;
    QSizeF size;
    double rotation = 0;
};
CanvasBox canvasBox(const Clip &clip, const Media *media, QSize canvas);

} // namespace velacut::engine
