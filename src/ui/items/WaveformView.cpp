// SPDX-License-Identifier: GPL-3.0-or-later
#include "WaveformView.h"

#include "controllers/EditorController.h"
#include "engine/analysis/MediaAnalysis.h"

#include <QPainter>

#include <cmath>

namespace velacut::ui {

namespace {
constexpr int kChunkWidth = 512;
}

WaveformView::WaveformView(QQuickItem *parent)
    : QQuickItem(parent)
{
    setFlag(ItemHasContents);
}

WaveformView::~WaveformView() = default;

void WaveformView::setEditor(EditorController *editor)
{
    if (editor == m_editor) {
        return;
    }
    disconnect(m_readyConnection);
    m_editor = editor;
    if (m_editor) {
        m_readyConnection = connect(&m_editor->analysis(), &engine::MediaAnalysis::waveformReady, this,
                                    [this](const MediaId &id) {
                                        if (id.toString() == m_mediaId) {
                                            reload();
                                        }
                                    });
    }
    emit editorChanged();
    reload();
}

void WaveformView::setMediaId(const QString &mediaId)
{
    if (mediaId != m_mediaId) {
        m_mediaId = mediaId;
        emit mediaIdChanged();
        reload();
    }
}

void WaveformView::setSourceIn(int frames)
{
    if (frames != m_sourceIn) {
        m_sourceIn = frames;
        emit sourceChanged();
        invalidate();
    }
}

void WaveformView::setSourceDuration(int frames)
{
    if (frames != m_sourceDuration) {
        m_sourceDuration = frames;
        emit sourceChanged();
        invalidate();
    }
}

void WaveformView::setColor(const QColor &color)
{
    if (color != m_color) {
        m_color = color;
        emit colorChanged();
        invalidate();
    }
}

void WaveformView::setFullWidth(qreal width)
{
    if (width != m_fullWidth) {
        m_fullWidth = width;
        emit spanChanged();
        invalidate();
    }
}

void WaveformView::setOffset(qreal offset)
{
    if (offset != m_offset) {
        m_offset = offset;
        emit spanChanged();
        layoutTiles();
    }
}

void WaveformView::reload()
{
    const Media *media = m_editor ? m_editor->findMedia(m_mediaId) : nullptr;
    m_waveform = media ? m_editor->analysis().waveform(*media) : nullptr;
    invalidate();
}

void WaveformView::invalidate()
{
    m_chunks.clear();
    layoutTiles();
}

void WaveformView::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.height() != oldGeometry.height()) {
        invalidate();
    } else if (newGeometry.width() != oldGeometry.width()) {
        layoutTiles();
    }
}

void WaveformView::layoutTiles()
{
    m_tiles.clear();
    m_images.clear();
    if (m_waveform && m_sourceDuration > 0 && width() > 0 && height() > 0 && m_editor) {
        const int first = static_cast<int>(std::floor(m_offset / kChunkWidth));
        const int last = static_cast<int>(std::floor((m_offset + width() - 1) / kChunkWidth));
        QHash<int, QImage> kept;
        for (int chunk = first; chunk <= last; ++chunk) {
            QImage image = m_chunks.value(chunk);
            if (image.isNull()) {
                image = paintChunk(chunk);
                ++m_paintedChunks;
            }
            kept.insert(chunk, image);
            m_images.insert(image.cacheKey(), image);
            ImageTile tile{image.cacheKey(), QRectF(chunk * kChunkWidth - m_offset, 0, kChunkWidth, height()),
                           QRectF(0, 0, image.width(), image.height())};
            if (clipTile(tile, boundingRect())) {
                m_tiles.push_back(tile);
            }
        }
        m_chunks = std::move(kept); // only the chunks around the view stay in memory
    }
    update();
}

QSGNode *WaveformView::updatePaintNode(QSGNode *old, UpdatePaintNodeData *)
{
    return updateImageTiles(window(), old, m_tiles, m_images);
}

QImage WaveformView::paintChunk(int chunk) const
{
    QImage image(kChunkWidth, std::max(1, static_cast<int>(std::ceil(height()))), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painterOnImage(&image);
    QPainter *painter = &painterOnImage;
    const QByteArray &peaks = m_waveform->peaks;
    const int buckets = m_waveform->bucketCount();
    const double bucketsPerFrame = m_waveform->bucketsPerSecond / m_editor->frameRate();
    const double middle = height() / 2.0;
    const double full = m_fullWidth > 0 ? m_fullWidth : width();
    const int columns = kChunkWidth;
    // One rectangle per pixel column, filled one by one: a single path of thousands of rectangles costs the raster
    // engine far more than the rectangles themselves (a four-minute clip used to freeze the interface for minutes).
    painter->setPen(Qt::NoPen);
    painter->setBrush(m_color);
    for (int x = 0; x < columns; ++x) {
        const double column = static_cast<double>(chunk) * kChunkWidth + x;
        const double from = (m_sourceIn + m_sourceDuration * column / full) * bucketsPerFrame;
        const double to = (m_sourceIn + m_sourceDuration * (column + 1) / full) * bucketsPerFrame;
        int low = 0;
        int high = 0;
        for (int b = static_cast<int>(from); b < std::max(static_cast<int>(from) + 1, static_cast<int>(to)) && b < buckets; ++b) {
            if (b >= 0) {
                low = std::min<int>(low, static_cast<signed char>(peaks[2 * b]));
                high = std::max<int>(high, static_cast<signed char>(peaks[2 * b + 1]));
            }
        }
        // At least one pixel, so silence still shows as a line.
        const double top = middle - std::max(0.5, high / 127.0 * middle);
        const double bottom = middle + std::max(0.5, -low / 127.0 * middle);
        painter->drawRect(QRectF(x, top, 1.0, bottom - top));
    }
    return image;
}

} // namespace velacut::ui
