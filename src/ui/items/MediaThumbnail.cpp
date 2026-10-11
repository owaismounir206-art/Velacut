// SPDX-License-Identifier: GPL-3.0-or-later
#include "MediaThumbnail.h"

#include "controllers/EditorController.h"
#include "engine/analysis/MediaAnalysis.h"

#include <cmath>

namespace velacut::ui {

MediaThumbnail::MediaThumbnail(QQuickItem *parent)
    : QQuickItem(parent)
{
    setFlag(ItemHasContents);
}

void MediaThumbnail::setEditor(EditorController *editor)
{
    if (editor == m_editor) {
        return;
    }
    disconnect(m_readyConnection);
    m_editor = editor;
    if (m_editor) {
        m_readyConnection = connect(&m_editor->analysis(), &engine::MediaAnalysis::thumbnailsReady, this,
                                    [this](const MediaId &id) {
                                        if (id.toString() == m_mediaId) {
                                            reload();
                                        }
                                    });
    }
    emit editorChanged();
    reload();
}

void MediaThumbnail::setMediaId(const QString &mediaId)
{
    if (mediaId != m_mediaId) {
        m_mediaId = mediaId;
        emit mediaIdChanged();
        reload();
    }
}

void MediaThumbnail::setTiled(bool tiled)
{
    if (tiled != m_tiled) {
        m_tiled = tiled;
        emit tiledChanged();
        layoutTiles();
    }
}

void MediaThumbnail::setSourceIn(int frames)
{
    if (frames != m_sourceIn) {
        m_sourceIn = frames;
        emit sourceChanged();
        layoutTiles();
    }
}

void MediaThumbnail::setSourceDuration(int frames)
{
    if (frames != m_sourceDuration) {
        m_sourceDuration = frames;
        emit sourceChanged();
        layoutTiles();
    }
}

void MediaThumbnail::setPosition(double position)
{
    position = std::clamp(position, 0.0, 1.0);
    if (position != m_position) {
        m_position = position;
        emit positionChanged();
        layoutTiles();
    }
}

void MediaThumbnail::setFullWidth(qreal width)
{
    if (width != m_fullWidth) {
        m_fullWidth = width;
        emit spanChanged();
        layoutTiles();
    }
}

void MediaThumbnail::setOffset(qreal offset)
{
    if (offset != m_offset) {
        m_offset = offset;
        emit spanChanged();
        layoutTiles();
    }
}

void MediaThumbnail::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() != oldGeometry.size()) {
        layoutTiles();
    }
}

void MediaThumbnail::reload()
{
    const bool wasReady = ready();
    m_strip = QImage();
    const Media *media = m_editor ? m_editor->findMedia(m_mediaId) : nullptr;
    if (media) {
        m_strip = m_editor->analysis().thumbnails(*media); // null: requested, thumbnailsReady() follows
        m_count = engine::MediaAnalysis::thumbnailCount(*media);
        m_mediaSeconds = media->info.duration ? media->info.duration->toSecondsDouble() : 0.0;
    }
    if (wasReady != ready()) {
        emit readyChanged();
    }
    layoutTiles();
}

void MediaThumbnail::layoutTiles()
{
    m_tiles.clear();
    m_images.clear();
    int dense = 0;
    if (!m_strip.isNull() && m_count >= 1 && width() > 0 && height() > 0) {
        const int frameWidth = m_strip.width() / m_count;
        const qreal frameHeight = m_strip.height();
        m_images.insert(m_strip.cacheKey(), m_strip);
        const auto coarse = [&](int index) {
            return QRectF(std::clamp(index, 0, m_count - 1) * frameWidth, 0, frameWidth, frameHeight);
        };
        if (!m_tiled) {
            // One frame covering the item, centred: the part of the frame that fits the item's shape.
            const int index = static_cast<int>(std::floor(m_position * m_count - 1e-9));
            const double scale = std::max(width() / frameWidth, height() / frameHeight);
            const double visibleWidth = width() / scale;
            const double visibleHeight = height() / scale;
            QRectF source = coarse(index);
            source = QRectF(source.x() + (frameWidth - visibleWidth) / 2, (frameHeight - visibleHeight) / 2, visibleWidth,
                            visibleHeight);
            m_tiles.push_back({m_strip.cacheKey(), boundingRect(), source});
        } else {
            // Timeline: tiles as high as the item, each with the frame of the source time at its centre, laid out from
            // the clip's start (x in clip coordinates) so that they stay put while the view scrolls.
            const double tileWidth = frameWidth * height() / frameHeight;
            const double rate = m_editor ? m_editor->frameRate() : 30.0;
            const double full = m_fullWidth > 0 ? m_fullWidth : width();
            const Media *media = m_editor ? m_editor->findMedia(m_mediaId) : nullptr;
            // Source seconds per pixel; the dense strips when the coarse one has less than a frame per tile.
            const double secondsPerPixel = full > 0 && rate > 0 ? m_sourceDuration / rate / full : 0.0;
            const double coarsePerSecond = m_mediaSeconds > 0 ? m_count / m_mediaSeconds : 0.0;
            const bool zoomedIn = media && media->kind == MediaKind::Video && secondsPerPixel > 0 &&
                                  1.0 / (secondsPerPixel * tileWidth) > 1.2 * coarsePerSecond;
            constexpr int kChunk = engine::MediaAnalysis::kDenseChunkSeconds;
            constexpr int kDenseCount = engine::MediaAnalysis::kDensePerSecond * kChunk;
            QHash<int, QImage> chunks;
            for (double x = std::floor(m_offset / tileWidth) * tileWidth; x < m_offset + width(); x += tileWidth) {
                const QRectF target(x - m_offset, 0, tileWidth, height());
                const auto add = [&](ImageTile tile) {
                    if (clipTile(tile, boundingRect())) {
                        m_tiles.push_back(tile);
                    }
                };
                double seconds = 0.0;
                if (m_mediaSeconds > 0 && m_sourceDuration > 0) {
                    seconds = (m_sourceIn + (x + tileWidth / 2) / full * m_sourceDuration) / rate;
                }
                if (zoomedIn) {
                    const int chunk = std::max(0, static_cast<int>(std::floor(seconds / kChunk)));
                    auto found = chunks.constFind(chunk);
                    if (found == chunks.constEnd()) {
                        found = chunks.insert(chunk, m_editor->analysis().denseThumbnails(*media, chunk)); // null: asked
                    }
                    const QImage &strip = *found;
                    if (!strip.isNull()) {
                        // Frames spread over the chunk (the last one, shorter, over what the media has).
                        const double length = std::min<double>(kChunk, m_mediaSeconds - chunk * kChunk);
                        const int index = std::clamp(static_cast<int>(std::floor((seconds - chunk * kChunk) / length * kDenseCount)),
                                                     0, kDenseCount - 1);
                        const int width = strip.width() / kDenseCount;
                        m_images.insert(strip.cacheKey(), strip);
                        add({strip.cacheKey(), target, QRectF(index * width, 0, width, strip.height())});
                        ++dense;
                        continue;
                    }
                }
                const int index = m_mediaSeconds > 0 ? static_cast<int>(std::floor(seconds / m_mediaSeconds * m_count)) : 0;
                add({m_strip.cacheKey(), target, coarse(index)});
            }
        }
    }
    if (dense != m_denseTiles) {
        m_denseTiles = dense;
        emit tilesChanged();
    }
    update();
}

QSGNode *MediaThumbnail::updatePaintNode(QSGNode *old, UpdatePaintNodeData *)
{
    return updateImageTiles(window(), old, m_tiles, m_images);
}

} // namespace velacut::ui
