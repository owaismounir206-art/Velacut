// SPDX-License-Identifier: GPL-3.0-or-later
#include "MediaThumbnail.h"

#include "controllers/EditorController.h"
#include "engine/analysis/MediaAnalysis.h"

#include <QPainter>

#include <cmath>

namespace vedit::ui {

MediaThumbnail::MediaThumbnail(QQuickItem *parent)
    : QQuickPaintedItem(parent)
{
    setOpaquePainting(false);
    setAntialiasing(false);
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
        update();
    }
}

void MediaThumbnail::setSourceIn(int frames)
{
    if (frames != m_sourceIn) {
        m_sourceIn = frames;
        emit sourceChanged();
        update();
    }
}

void MediaThumbnail::setSourceDuration(int frames)
{
    if (frames != m_sourceDuration) {
        m_sourceDuration = frames;
        emit sourceChanged();
        update();
    }
}

void MediaThumbnail::setPosition(double position)
{
    position = std::clamp(position, 0.0, 1.0);
    if (position != m_position) {
        m_position = position;
        emit positionChanged();
        update();
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
    update();
}

void MediaThumbnail::paint(QPainter *painter)
{
    if (m_strip.isNull() || m_count < 1 || width() <= 0 || height() <= 0) {
        return;
    }
    painter->setRenderHint(QPainter::SmoothPixmapTransform);
    const int frameWidth = m_strip.width() / m_count;
    const QSizeF frame(frameWidth, m_strip.height());
    const auto source = [&](int index) {
        return QRectF(std::clamp(index, 0, m_count - 1) * frameWidth, 0, frameWidth, m_strip.height());
    };
    if (!m_tiled) {
        // One frame, scaled to cover the item and centred.
        const int index = static_cast<int>(std::floor(m_position * m_count - 1e-9));
        const double scale = std::max(width() / frame.width(), height() / frame.height());
        const QSizeF size = frame * scale;
        const QRectF target((width() - size.width()) / 2, (height() - size.height()) / 2, size.width(), size.height());
        painter->setClipRect(boundingRect());
        painter->drawImage(target, m_strip, source(index));
        return;
    }
    // Timeline: tiles as high as the item, each with the frame of the source time at its centre.
    const double tileWidth = frame.width() * height() / frame.height();
    const double rate = m_editor ? m_editor->frameRate() : 30.0;
    for (double x = 0; x < width(); x += tileWidth) {
        int index = 0;
        if (m_mediaSeconds > 0 && m_sourceDuration > 0) {
            const double frames = m_sourceIn + (x + tileWidth / 2) / width() * m_sourceDuration;
            index = static_cast<int>(std::floor(frames / rate / m_mediaSeconds * m_count));
        }
        painter->drawImage(QRectF(x, 0, tileWidth, height()), m_strip, source(index));
    }
}

} // namespace vedit::ui
