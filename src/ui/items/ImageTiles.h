// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QHash>
#include <QImage>
#include <QRectF>

#include <vector>

class QQuickWindow;
class QSGNode;

namespace velacut::ui {

// Pictures made of tiles cut from a few images (thumbnail strips, waveform chunks), drawn by the scene graph: each
// image becomes a texture once and stays one while it is used; a tile is an image node (every backend, software
// included). Moving or resizing the tiles changes coordinates only.
struct ImageTile
{
    qint64 image = 0; // QImage::cacheKey() of an image in the map given with the tiles
    QRectF target;    // in item coordinates
    QRectF source;    // in the image's pixels
};

// Cuts `tile` to `bounds` (the part outside is not drawn; the source shrinks in proportion). False if nothing is left.
bool clipTile(ImageTile &tile, const QRectF &bounds);

// For QQuickItem::updatePaintNode(): the node of `tiles` (null when there are none), reusing `old`.
QSGNode *updateImageTiles(QQuickWindow *window, QSGNode *old, const std::vector<ImageTile> &tiles,
                          const QHash<qint64, QImage> &images);

} // namespace velacut::ui
