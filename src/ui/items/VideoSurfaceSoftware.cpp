// SPDX-License-Identifier: GPL-3.0-or-later
#include "VideoSurfaceSoftware.h"

#include "items/VideoFit.h"

#include <QQuickWindow>
#include <QSGImageNode>
#include <QSGRectangleNode>

namespace vedit::ui {

namespace {
// Node layout: root -> [background rectangle, image].
struct Nodes
{
    QSGRectangleNode *background;
    QSGImageNode *image;
};
} // namespace

VideoSurfaceSoftware::VideoSurfaceSoftware(QQuickItem *parent)
    : QQuickItem(parent)
{
    setFlag(ItemHasContents, true);
}

void VideoSurfaceSoftware::setSink(engine::FrameSink *sink)
{
    if (sink == m_sink) {
        return;
    }
    if (m_sink) {
        disconnect(m_sink, nullptr, this, nullptr);
    }
    m_sink = sink;
    if (m_sink) {
        connect(m_sink, &engine::FrameSink::frameReady, this, &QQuickItem::update);
    }
    emit sinkChanged();
    update();
}

void VideoSurfaceSoftware::setBackgroundColor(const QColor &color)
{
    if (color != m_backgroundColor) {
        m_backgroundColor = color;
        emit backgroundColorChanged();
        update();
    }
}

QSGNode *VideoSurfaceSoftware::updatePaintNode(QSGNode *old, UpdatePaintNodeData *)
{
    QSGNode *root = old;
    if (!root) {
        root = new QSGNode;
        QSGRectangleNode *background = window()->createRectangleNode();
        QSGImageNode *image = window()->createImageNode();
        image->setOwnsTexture(true);
        image->setFiltering(QSGTexture::Linear);
        // Placeholder until the first frame: an image node must always have a texture.
        QImage placeholder(1, 1, QImage::Format_RGBA8888);
        placeholder.fill(Qt::transparent);
        image->setTexture(window()->createTextureFromImage(placeholder));
        root->appendChildNode(background);
        root->appendChildNode(image);
    }
    auto *background = static_cast<QSGRectangleNode *>(root->firstChild());
    auto *image = static_cast<QSGImageNode *>(root->lastChild());
    background->setRect(boundingRect());
    background->setColor(m_backgroundColor);

    if (m_sink) {
        quint64 serial = 0;
        const QImage frame = m_sink->latest(&serial);
        if (serial != m_serial && !frame.isNull()) {
            m_serial = serial;
            m_frameSize = frame.size();
            image->setTexture(window()->createTextureFromImage(frame));
            m_sink->markConsumed(serial);
        }
    }
    if (!m_frameSize.isEmpty()) {
        image->setRect(fitRect(m_frameSize, size()));
        image->setSourceRect(QRectF(QPointF(0, 0), m_frameSize));
    } else {
        image->setRect(QRectF());
    }
    return root;
}

} // namespace vedit::ui
