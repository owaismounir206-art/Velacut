// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "engine/playback/FrameSink.h"

#include <QColor>
#include <QPointer>
#include <QQuickItem>
#include <QtQml/qqmlregistration.h>

namespace vedit::ui {

// Video preview for the software scene graph (QT_QUICK_BACKEND=software, no GPU): plain image nodes,
// no shaders (SPEC 1bis).
class VideoSurfaceSoftware : public QQuickItem
{
    Q_OBJECT
    QML_NAMED_ELEMENT(VideoSurfaceSoftware)
    Q_PROPERTY(vedit::engine::FrameSink *sink READ sink WRITE setSink NOTIFY sinkChanged FINAL)
    Q_PROPERTY(QColor backgroundColor READ backgroundColor WRITE setBackgroundColor NOTIFY backgroundColorChanged FINAL)

public:
    explicit VideoSurfaceSoftware(QQuickItem *parent = nullptr);

    engine::FrameSink *sink() const { return m_sink; }
    void setSink(engine::FrameSink *sink);
    QColor backgroundColor() const { return m_backgroundColor; }
    void setBackgroundColor(const QColor &color);

signals:
    void sinkChanged();
    void backgroundColorChanged();

protected:
    QSGNode *updatePaintNode(QSGNode *old, UpdatePaintNodeData *) override;

private:
    QPointer<engine::FrameSink> m_sink;
    QColor m_backgroundColor = Qt::black;
    quint64 m_serial = 0;
    QSize m_frameSize;
};

} // namespace vedit::ui
