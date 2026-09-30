// SPDX-License-Identifier: GPL-3.0-or-later
#include "Graphic.h"

#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>

#include <algorithm>
#include <cmath>

namespace vedit::fx {

namespace {

constexpr double kPi = 3.14159265358979323846;

double easeOut(double t)
{
    t = std::clamp(t, 0.0, 1.0);
    return 1.0 - std::pow(1.0 - t, 3.0);
}

// The first `fraction` of `path` as a polyline (hand-drawn marks appear as if drawn).
QPolygonF partial(const QPainterPath &path, double fraction)
{
    QPolygonF points;
    const int steps = 120;
    const int last = static_cast<int>(std::ceil(std::clamp(fraction, 0.0, 1.0) * steps));
    for (int i = 0; i <= last; ++i) {
        points << path.pointAtPercent(std::min(1.0, static_cast<double>(i) / steps));
    }
    return points;
}

QString clock(double seconds)
{
    const auto whole = static_cast<long long>(std::max(0.0, seconds));
    return QStringLiteral("%1:%2").arg(whole / 60).arg(whole % 60, 2, 10, QLatin1Char('0'));
}

void drawText(QPainter &painter, const QString &text, const GraphicGlyphs &glyphs, const QSize &canvas)
{
    int width = glyphs.prefix.width() + glyphs.suffix.width();
    for (const QChar c : text) {
        width += glyphs.glyphs.value(c).width();
    }
    int x = (canvas.width() - width) / 2;
    const int y = (canvas.height() - glyphs.height) / 2;
    const auto put = [&](const QImage &image) {
        if (!image.isNull()) {
            painter.drawImage(x, y, image);
            x += image.width();
        }
    };
    put(glyphs.prefix);
    for (const QChar c : text) {
        put(glyphs.glyphs.value(c));
    }
    put(glyphs.suffix);
}

} // namespace

bool graphicHasText(GraphicType type)
{
    return type == GraphicType::Counter || type == GraphicType::Countdown || type == GraphicType::Timer;
}

QString graphicCharacters()
{
    return QStringLiteral("0123456789.,:-");
}

int graphicTextHeight(const GraphicParams &params, int canvasHeight)
{
    return std::max(8, static_cast<int>(std::lround(canvasHeight * (0.08 + std::clamp(params.thickness, 0.0, 1.0) * 0.22))));
}

QString graphicText(const GraphicParams &params, double elapsed, double total)
{
    switch (params.type) {
    case GraphicType::Counter: {
        // Counts during the first 80 % of the clip, slowing down at the end, then holds the final number.
        const double progress = total > 0 ? easeOut(elapsed / (total * 0.8)) : 1.0;
        const double value = params.from + (params.to - params.from) * progress;
        return QString::number(value, 'f', std::clamp(params.decimals, 0, 4));
    }
    case GraphicType::Countdown:
        return clock(std::ceil(std::max(0.0, total - elapsed) - 1e-9));
    case GraphicType::Timer:
        return clock(elapsed);
    default:
        return {};
    }
}

QImage renderGraphic(const GraphicParams &params, double elapsed, double total, const QSize &canvas,
                     const GraphicGlyphs &glyphs)
{
    QImage image(canvas, QImage::Format_RGBA8888);
    image.fill(Qt::transparent);
    if (canvas.isEmpty()) {
        return image;
    }
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    const double w = canvas.width();
    const double h = canvas.height();
    if (graphicHasText(params.type)) {
        drawText(painter, graphicText(params, elapsed, total), glyphs, canvas);
        return image;
    }
    if (params.type == GraphicType::ProgressBar) {
        const double barHeight = h * (0.008 + std::clamp(params.thickness, 0.0, 1.0) * 0.03);
        const QRectF track(w * 0.04, h * 0.93 - barHeight / 2, w * 0.92, barHeight);
        const double progress = total > 0 ? std::clamp(elapsed / total, 0.0, 1.0) : 1.0;
        painter.setPen(Qt::NoPen);
        painter.setBrush(params.color2);
        painter.drawRoundedRect(track, barHeight / 2, barHeight / 2);
        painter.setBrush(params.color);
        painter.drawRoundedRect(QRectF(track.left(), track.top(), std::max(barHeight, track.width() * progress), barHeight),
                                barHeight / 2, barHeight / 2);
        return image;
    }

    // Marks drawn by hand, in the middle third of the canvas.
    const double draw = params.drawSeconds > 0 ? std::clamp(elapsed / params.drawSeconds, 0.0, 1.0) : 1.0;
    const double s = std::min(w, h);
    const QPointF c(w / 2, h / 2);
    const double stroke = s * (0.006 + std::clamp(params.thickness, 0.0, 1.0) * 0.03);
    QPen pen(params.color, stroke, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    switch (params.type) {
    case GraphicType::Arrow: {
        QPainterPath shaft(c + QPointF(-s * 0.3, s * 0.18));
        shaft.cubicTo(c + QPointF(-s * 0.15, -s * 0.12), c + QPointF(s * 0.1, s * 0.08), c + QPointF(s * 0.28, -s * 0.16));
        const double shaftPart = std::min(1.0, draw / 0.75);
        painter.drawPolyline(partial(shaft, shaftPart));
        if (draw > 0.75) {
            // The head: two short strokes from the tip, back along the direction of the shaft.
            const double head = (draw - 0.75) / 0.25;
            const QPointF tip = shaft.pointAtPercent(1.0);
            const double angle = std::atan2(shaft.pointAtPercent(1.0).y() - shaft.pointAtPercent(0.95).y(),
                                            shaft.pointAtPercent(1.0).x() - shaft.pointAtPercent(0.95).x());
            const double length = s * 0.09 * head;
            for (const double side : {-1.0, 1.0}) {
                const double a = angle + kPi + side * 0.5;
                painter.drawLine(tip, tip + QPointF(std::cos(a) * length, std::sin(a) * length));
            }
        }
        break;
    }
    case GraphicType::Circle: {
        // A loop by hand: a little more than one turn, the radius wobbling.
        QPainterPath loop;
        const int steps = 90;
        for (int i = 0; i <= steps; ++i) {
            const double a = -kPi * 0.6 + 2.15 * kPi * i / steps;
            const double wobble = 1.0 + 0.04 * std::sin(a * 3.0 + 0.7) + 0.03 * (static_cast<double>(i) / steps);
            const QPointF p = c + QPointF(std::cos(a) * s * 0.34 * wobble, std::sin(a) * s * 0.22 * wobble);
            if (i == 0) {
                loop.moveTo(p);
            } else {
                loop.lineTo(p);
            }
        }
        painter.drawPolyline(partial(loop, draw));
        break;
    }
    case GraphicType::Underline: {
        QPainterPath line(c + QPointF(-s * 0.35, s * 0.02));
        line.cubicTo(c + QPointF(-s * 0.1, -s * 0.02), c + QPointF(s * 0.1, s * 0.05), c + QPointF(s * 0.35, -s * 0.01));
        painter.drawPolyline(partial(line, draw));
        break;
    }
    case GraphicType::Highlighter: {
        QColor marker = params.color;
        marker.setAlpha(std::min(marker.alpha(), 120));
        pen.setColor(marker);
        pen.setWidthF(s * (0.05 + std::clamp(params.thickness, 0.0, 1.0) * 0.12));
        pen.setCapStyle(Qt::FlatCap);
        painter.setPen(pen);
        const QPointF from = c + QPointF(-s * 0.36, 0);
        const QPointF to = c + QPointF(s * 0.36, -s * 0.01);
        painter.drawLine(from, from + (to - from) * draw);
        break;
    }
    case GraphicType::Check: {
        QPainterPath check(c + QPointF(-s * 0.16, 0));
        check.lineTo(c + QPointF(-s * 0.04, s * 0.13));
        check.lineTo(c + QPointF(s * 0.2, -s * 0.16));
        painter.drawPolyline(partial(check, draw));
        break;
    }
    case GraphicType::Cross: {
        const double first = std::min(1.0, draw * 2);
        const double second = std::clamp(draw * 2 - 1, 0.0, 1.0);
        const QPointF a(c + QPointF(-s * 0.15, -s * 0.15));
        const QPointF b(c + QPointF(s * 0.15, s * 0.15));
        const QPointF d(c + QPointF(s * 0.15, -s * 0.15));
        const QPointF e(c + QPointF(-s * 0.15, s * 0.15));
        painter.drawLine(a, a + (b - a) * first);
        if (second > 0) {
            painter.drawLine(d, d + (e - d) * second);
        }
        break;
    }
    default:
        break;
    }
    return image;
}

} // namespace vedit::fx
