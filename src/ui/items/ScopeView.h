// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "engine/playback/FrameSink.h"

#include <QColor>
#include <QPointer>
#include <QQuickPaintedItem>
#include <QtQml/qqmlregistration.h>

namespace velacut::ui {

// Live video scopes (SPEC §5.10): histogram, waveform and vectorscope rendered from preview frames.
class ScopeView : public QQuickPaintedItem
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(velacut::engine::FrameSink *sink READ sink WRITE setSink NOTIFY sinkChanged FINAL)
    Q_PROPERTY(int mode READ mode WRITE setMode NOTIFY modeChanged FINAL)

public:
    enum Mode
    {
        Histogram = 0,
        Waveform = 1,
        Vectorscope = 2,
    };
    Q_ENUM(Mode)

    explicit ScopeView(QQuickItem *parent = nullptr);
    ~ScopeView() override = default;

    engine::FrameSink *sink() const { return m_sink; }
    void setSink(engine::FrameSink *sink);

    int mode() const { return m_mode; }
    void setMode(int mode);

    void paint(QPainter *painter) override;

signals:
    void sinkChanged();
    void modeChanged();

private:
    void drawHistogram(QPainter *painter, const QImage &frame, const QRectF &rect);
    void drawWaveform(QPainter *painter, const QImage &frame, const QRectF &rect);
    void drawVectorscope(QPainter *painter, const QImage &frame, const QRectF &rect);

    QPointer<engine::FrameSink> m_sink;
    int m_mode = Histogram;
    QMetaObject::Connection m_frameConnection;
};

} // namespace velacut::ui
