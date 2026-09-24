// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "engine/playback/FrameSink.h"

#include <QColor>
#include <QPointer>
#include <QQuickRhiItem>
#include <QtQml/qqmlregistration.h>

namespace vedit::ui {

// Video preview for the hardware-accelerated scene graph (Vulkan, OpenGL, OpenGL ES): keeps one GPU
// texture and uploads each new frame into it, drawn aspect-fit with a tiny shader (compiled with
// qt6-shadertools for GLSL 100 es/120/150, SPIR-V…). docs/ARCHITECTURE.md §5.3.
class VideoSurfaceRhi : public QQuickRhiItem
{
    Q_OBJECT
    QML_NAMED_ELEMENT(VideoSurfaceRhi)
    Q_PROPERTY(vedit::engine::FrameSink *sink READ sink WRITE setSink NOTIFY sinkChanged FINAL)
    Q_PROPERTY(QColor backgroundColor READ backgroundColor WRITE setBackgroundColor NOTIFY backgroundColorChanged FINAL)

public:
    explicit VideoSurfaceRhi(QQuickItem *parent = nullptr);

    engine::FrameSink *sink() const { return m_sink; }
    void setSink(engine::FrameSink *sink);
    QColor backgroundColor() const { return m_backgroundColor; }
    void setBackgroundColor(const QColor &color);

signals:
    void sinkChanged();
    void backgroundColorChanged();

protected:
    QQuickRhiItemRenderer *createRenderer() override;

private:
    QPointer<engine::FrameSink> m_sink;
    QColor m_backgroundColor = Qt::black;
};

} // namespace vedit::ui
