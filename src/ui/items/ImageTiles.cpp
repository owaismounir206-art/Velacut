// SPDX-License-Identifier: GPL-3.0-or-later
#include "ImageTiles.h"

#include <QQuickWindow>
#include <QSGImageNode>
#include <QSGTexture>

#include <map>
#include <memory>

namespace velacut::ui {

namespace {

class TilesNode : public QSGNode
{
public:
    std::map<qint64, std::unique_ptr<QSGTexture>> textures; // by QImage::cacheKey()
};

} // namespace

bool clipTile(ImageTile &tile, const QRectF &bounds)
{
    const QRectF visible = tile.target.intersected(bounds);
    if (visible.isEmpty() || tile.target.width() <= 0 || tile.target.height() <= 0) {
        return false;
    }
    const double sx = tile.source.width() / tile.target.width();
    const double sy = tile.source.height() / tile.target.height();
    tile.source = QRectF(tile.source.x() + (visible.x() - tile.target.x()) * sx, tile.source.y() + (visible.y() - tile.target.y()) * sy,
                         visible.width() * sx, visible.height() * sy);
    tile.target = visible;
    return true;
}

QSGNode *updateImageTiles(QQuickWindow *window, QSGNode *old, const std::vector<ImageTile> &tiles,
                          const QHash<qint64, QImage> &images)
{
    auto *node = static_cast<TilesNode *>(old);
    if (tiles.empty() || !window) {
        delete node;
        return nullptr;
    }
    if (!node) {
        node = new TilesNode;
    }
    for (auto it = images.constBegin(); it != images.constEnd(); ++it) {
        if (!node->textures.contains(it.key())) {
            node->textures.emplace(it.key(), window->createTextureFromImage(it.value()));
        }
    }
    QSGNode *child = node->firstChild();
    for (const ImageTile &tile : tiles) {
        const auto texture = node->textures.find(tile.image);
        if (texture == node->textures.end()) {
            continue;
        }
        if (auto *image = static_cast<QSGImageNode *>(child)) {
            child = child->nextSibling();
            image->setTexture(texture->second.get());
            image->setRect(tile.target);
            image->setSourceRect(tile.source);
            continue;
        }
        // A new node gets its texture before anything else and joins the tree complete (the software renderer
        // updates a node as soon as it changes, and has nothing to draw without a texture).
        QSGImageNode *image = window->createImageNode();
        image->setOwnsTexture(false);
        image->setTexture(texture->second.get());
        image->setRect(tile.target);
        image->setSourceRect(tile.source);
        image->setFiltering(QSGTexture::Linear);
        node->appendChildNode(image);
    }
    while (child) {
        QSGNode *next = child->nextSibling();
        node->removeChildNode(child);
        delete child;
        child = next;
    }
    // Textures no longer used, last: until here image nodes could still point at them.
    for (auto it = node->textures.begin(); it != node->textures.end();) {
        it = images.contains(it->first) ? std::next(it) : node->textures.erase(it);
    }
    return node;
}

} // namespace velacut::ui
