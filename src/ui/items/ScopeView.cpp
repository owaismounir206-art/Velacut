// SPDX-License-Identifier: GPL-3.0-or-later
#include "ScopeView.h"

#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>
#include <vector>

namespace vedit::ui {

ScopeView::ScopeView(QQuickItem *parent)
    : QQuickPaintedItem(parent)
{
    setAntialiasing(true);
}

void ScopeView::setSink(engine::FrameSink *sink)
{
    if (m_sink == sink) {
        return;
    }
    if (m_sink) {
        disconnect(m_frameConnection);
    }
    m_sink = sink;
    if (m_sink) {
        m_frameConnection = connect(m_sink, &engine::FrameSink::frameReady, this, [this] {
            update();
        });
    }
    emit sinkChanged();
    update();
}

void ScopeView::setMode(int mode)
{
    if (m_mode == mode) {
        return;
    }
    m_mode = mode;
    emit modeChanged();
    update();
}

void ScopeView::paint(QPainter *painter)
{
    const QRectF rect(0, 0, width(), height());
    painter->fillRect(rect, QColor(24, 24, 28));

    QImage frame;
    if (m_sink) {
        frame = m_sink->latest();
    }

    if (m_mode == Histogram) {
        drawHistogram(painter, frame, rect);
    } else if (m_mode == Waveform) {
        drawWaveform(painter, frame, rect);
    } else if (m_mode == Vectorscope) {
        drawVectorscope(painter, frame, rect);
    }
}

void ScopeView::drawHistogram(QPainter *painter, const QImage &frame, const QRectF &rect)
{
    // Graticule
    painter->setPen(QPen(QColor(60, 60, 70), 1, Qt::DotLine));
    for (int i = 1; i <= 3; ++i) {
        const double x = rect.left() + rect.width() * i / 4.0;
        painter->drawLine(QPointF(x, rect.top()), QPointF(x, rect.bottom()));
        const double y = rect.top() + rect.height() * i / 4.0;
        painter->drawLine(QPointF(rect.left(), y), QPointF(rect.right(), y));
    }

    if (frame.isNull()) {
        painter->setPen(QColor(140, 140, 150));
        painter->drawText(rect, Qt::AlignCenter, tr("No frame"));
        return;
    }

    const QImage rgb = frame.format() == QImage::Format_RGBA8888 || frame.format() == QImage::Format_RGB32
                           ? frame
                           : frame.convertToFormat(QImage::Format_RGBA8888);

    int rBins[256] = {0};
    int gBins[256] = {0};
    int bBins[256] = {0};
    int maxCount = 1;

    const int stepX = std::max(1, rgb.width() / 160);
    const int stepY = std::max(1, rgb.height() / 90);

    for (int y = 0; y < rgb.height(); y += stepY) {
        const auto *scan = reinterpret_cast<const QRgb *>(rgb.constScanLine(y));
        for (int x = 0; x < rgb.width(); x += stepX) {
            const QRgb pixel = scan[x];
            const int r = qRed(pixel);
            const int g = qGreen(pixel);
            const int b = qBlue(pixel);
            maxCount = std::max({maxCount, ++rBins[r], ++gBins[g], ++bBins[b]});
        }
    }

    const auto drawChannel = [&](const int bins[256], const QColor &color) {
        QPainterPath path;
        path.moveTo(rect.left(), rect.bottom());
        for (int i = 0; i < 256; ++i) {
            const double x = rect.left() + rect.width() * i / 255.0;
            const double h = (static_cast<double>(bins[i]) / maxCount) * (rect.height() * 0.9);
            path.lineTo(x, rect.bottom() - h);
        }
        path.lineTo(rect.right(), rect.bottom());
        path.closeSubpath();
        painter->fillPath(path, color);
        painter->setPen(QPen(QColor(color.red(), color.green(), color.blue(), 220), 1.5));
        painter->drawPath(path);
    };

    painter->setCompositionMode(QPainter::CompositionMode_Plus);
    drawChannel(rBins, QColor(220, 40, 40, 90));
    drawChannel(gBins, QColor(40, 220, 40, 90));
    drawChannel(bBins, QColor(40, 100, 240, 90));
    painter->setCompositionMode(QPainter::CompositionMode_SourceOver);
}

void ScopeView::drawWaveform(QPainter *painter, const QImage &frame, const QRectF &rect)
{
    // Graticule (IRE 0, 25, 50, 75, 100)
    painter->setPen(QPen(QColor(60, 60, 70), 1, Qt::DotLine));
    for (int i = 0; i <= 4; ++i) {
        const double y = rect.top() + rect.height() * (1.0 - i / 4.0);
        painter->drawLine(QPointF(rect.left(), y), QPointF(rect.right(), y));
    }

    if (frame.isNull()) {
        painter->setPen(QColor(140, 140, 150));
        painter->drawText(rect, Qt::AlignCenter, tr("No frame"));
        return;
    }

    const QImage rgb = frame.format() == QImage::Format_RGBA8888 || frame.format() == QImage::Format_RGB32
                           ? frame
                           : frame.convertToFormat(QImage::Format_RGBA8888);

    const int cols = std::min(256, static_cast<int>(rect.width()));
    const int stepY = std::max(1, rgb.height() / 120);

    painter->setPen(QColor(50, 220, 140, 40));
    for (int c = 0; c < cols; ++c) {
        const int srcX = c * rgb.width() / cols;
        const double dstX = rect.left() + rect.width() * c / cols;
        for (int y = 0; y < rgb.height(); y += stepY) {
            const QRgb pixel = rgb.pixel(srcX, y);
            const double luma = 0.2126 * qRed(pixel) + 0.7152 * qGreen(pixel) + 0.0722 * qBlue(pixel);
            const double dstY = rect.bottom() - (luma / 255.0) * rect.height();
            painter->drawPoint(QPointF(dstX, dstY));
        }
    }
}

void ScopeView::drawVectorscope(QPainter *painter, const QImage &frame, const QRectF &rect)
{
    const QPointF center = rect.center();
    const double radius = std::min(rect.width(), rect.height()) * 0.44;

    // Graticule circles: 75% and 100% saturation
    painter->setPen(QPen(QColor(70, 70, 85), 1, Qt::SolidLine));
    painter->drawEllipse(center, radius * 0.75, radius * 0.75);
    painter->setPen(QPen(QColor(50, 50, 65), 1, Qt::DashLine));
    painter->drawEllipse(center, radius, radius);

    // Crosshairs
    painter->setPen(QPen(QColor(60, 60, 75), 1, Qt::DotLine));
    painter->drawLine(QPointF(center.x() - radius, center.y()), QPointF(center.x() + radius, center.y()));
    painter->drawLine(QPointF(center.x(), center.y() - radius), QPointF(center.x(), center.y() + radius));

    // Skin tone line (I axis, approx 117 degrees)
    const double skinRad = (117.0 - 90.0) * (std::numbers::pi / 180.0);
    const QPointF skinDir(std::cos(skinRad) * radius * 0.85, -std::sin(skinRad) * radius * 0.85);
    painter->setPen(QPen(QColor(180, 120, 70, 160), 1, Qt::DashLine));
    painter->drawLine(center, center + skinDir);

    // Target boxes for 75% primaries (R, Mg, B, Cy, G, Yl)
    struct Target { const char *name; double r, g, b; };
    const Target targets[] = {
        {"R",  0.75, 0.0,  0.0},
        {"Mg", 0.75, 0.0,  0.75},
        {"B",  0.0,  0.0,  0.75},
        {"Cy", 0.0,  0.75, 0.75},
        {"G",  0.0,  0.75, 0.0},
        {"Yl", 0.75, 0.75, 0.0}
    };
    painter->setPen(QPen(QColor(120, 140, 180), 1));
    for (const auto &t : targets) {
        const double cb = (-0.168736 * t.r - 0.331264 * t.g + 0.5 * t.b) / 0.5;
        const double cr = (0.5 * t.r - 0.418688 * t.g - 0.081312 * t.b) / 0.5;
        const QPointF pt(center.x() + cb * radius, center.y() - cr * radius);
        painter->drawRect(QRectF(pt.x() - 4, pt.y() - 4, 8, 8));
        painter->drawText(QRectF(pt.x() - 10, pt.y() - 16, 20, 12), Qt::AlignCenter, QLatin1StringView(t.name));
    }

    if (frame.isNull()) {
        painter->setPen(QColor(140, 140, 150));
        painter->drawText(rect, Qt::AlignCenter, tr("No frame"));
        return;
    }

    const QImage rgb = frame.format() == QImage::Format_RGBA8888 || frame.format() == QImage::Format_RGB32
                           ? frame
                           : frame.convertToFormat(QImage::Format_RGBA8888);

    const int step = std::max(2, static_cast<int>(std::sqrt(rgb.width() * rgb.height() / 10000.0)));
    painter->setPen(QColor(60, 220, 100, 50));

    for (int y = 0; y < rgb.height(); y += step) {
        const auto *scan = reinterpret_cast<const QRgb *>(rgb.constScanLine(y));
        for (int x = 0; x < rgb.width(); x += step) {
            const QRgb pixel = scan[x];
            const double r = qRed(pixel) / 255.0;
            const double g = qGreen(pixel) / 255.0;
            const double b = qBlue(pixel) / 255.0;
            const double cb = (-0.168736 * r - 0.331264 * g + 0.5 * b) / 0.5;
            const double cr = (0.5 * r - 0.418688 * g - 0.081312 * b) / 0.5;
            painter->drawPoint(QPointF(center.x() + cb * radius, center.y() - cr * radius));
        }
    }
}

} // namespace vedit::ui
