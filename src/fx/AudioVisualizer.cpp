// SPDX-License-Identifier: GPL-3.0-or-later
#include "AudioVisualizer.h"

#include <QColor>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>

#include <algorithm>
#include <cmath>

namespace velacut::fx {

namespace {

QColor withAlpha(QColor color, int alpha)
{
    color.setAlpha(alpha);
    return color;
}

} // namespace

VisualizerFrameData visualizerFrame(const std::vector<float> &spectrumBands, int barCount, double sensitivity)
{
    VisualizerFrameData data;
    const int count = std::clamp(barCount, 4, 128);
    data.bands.assign(static_cast<size_t>(count), 0.0);
    const int bands = static_cast<int>(spectrumBands.size());
    if (bands == 0) {
        return data;
    }
    const double gain = std::clamp(sensitivity, 0.1, 10.0);
    double sum = 0.0;
    double bass = 0.0;
    const int bassBars = std::max(1, count / 4);
    for (int bar = 0; bar < count; ++bar) {
        // The bar covers [from, to) of the bands (at least one band: more bars than bands repeat them).
        const double from = static_cast<double>(bar) * bands / count;
        const double to = std::max(from + 1.0, static_cast<double>(bar + 1) * bands / count);
        double level = 0.0;
        int taken = 0;
        for (int b = static_cast<int>(from); b < static_cast<int>(std::ceil(to)) && b < bands; ++b) {
            level += spectrumBands[static_cast<size_t>(b)];
            ++taken;
        }
        level = taken > 0 ? std::clamp(level / taken * gain, 0.0, 1.0) : 0.0;
        data.bands[static_cast<size_t>(bar)] = level;
        sum += level;
        if (bar < bassBars) {
            bass += level;
        }
    }
    data.overallLevel = sum / count;
    data.bassLevel = bass / bassBars;
    return data;
}

VisualizerFrameData exampleVisualizerFrame(double seconds, int barCount)
{
    constexpr double kPi = 3.14159265358979323846;
    std::vector<float> levels(static_cast<size_t>(std::clamp(barCount, 4, 128)));
    const double kick = std::exp(-8.0 * std::fmod(seconds, 0.5));
    for (size_t i = 0; i < levels.size(); ++i) {
        const double x = static_cast<double>(i) / static_cast<double>(levels.size());
        const double wave = 0.5 + 0.5 * std::sin(2.0 * kPi * (seconds * 1.5 + x * 3.0));
        levels[i] = static_cast<float>(std::clamp(kick * 0.6 * (1.0 - x) + wave * 0.35 + 0.05, 0.0, 1.0));
    }
    return visualizerFrame(levels, barCount, 1.0);
}

QImage renderAudioVisualizer(const VisualizerSettings &settings, const VisualizerFrameData &data, const QSize &canvasSize)
{
    if (canvasSize.width() <= 0 || canvasSize.height() <= 0) {
        return {};
    }
    if (data.bands.size() < 4) {
        QImage empty(canvasSize, QImage::Format_RGBA8888);
        empty.fill(Qt::transparent);
        return empty;
    }

    QImage image(canvasSize, QImage::Format_RGBA8888);
    image.fill(Qt::transparent);

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const int w = canvasSize.width();
    const int h = canvasSize.height();
    const int barCount = std::min(static_cast<int>(data.bands.size()), 128);
    const double unit = h / 1080.0; // line widths are given at 1080p

    const QColor col1 = settings.primary;
    const QColor col2 = settings.secondary;

    switch (settings.style) {
    case VisualizerStyle::Bars: {
        const double areaW = w * 0.88;
        const double areaH = h * 0.45;
        const double startX = (w - areaW) * 0.5;
        const double baselineY = settings.mirror ? (h * 0.5) : (h * 0.85);

        const double gap = std::clamp(areaW / (barCount * 4.0), 2.0, 8.0);
        const double barW = (areaW - (barCount - 1) * gap) / barCount;
        const double cornerRadius = (settings.roundness * barW * 0.5);

        for (int i = 0; i < barCount; ++i) {
            const double bx = startX + i * (barW + gap);
            const double bHeight = std::max(4.0, data.bands[static_cast<size_t>(i)] * areaH);

            QLinearGradient grad(bx, baselineY - bHeight, bx, baselineY);
            grad.setColorAt(0.0, col2);
            grad.setColorAt(1.0, col1);

            painter.setPen(Qt::NoPen);
            painter.setBrush(grad);

            if (!settings.mirror) {
                painter.drawRoundedRect(QRectF(bx, baselineY - bHeight, barW, bHeight), cornerRadius, cornerRadius);
            } else {
                // Top half
                painter.drawRoundedRect(QRectF(bx, baselineY - bHeight * 0.5, barW, bHeight * 0.5), cornerRadius, cornerRadius);
                // Mirrored bottom half
                painter.drawRoundedRect(QRectF(bx, baselineY, barW, bHeight * 0.5), cornerRadius, cornerRadius);
            }
        }
        break;
    }
    case VisualizerStyle::Spectrum: {
        const double areaW = w * 0.90;
        const double areaH = h * 0.50;
        const double startX = (w - areaW) * 0.5;
        const double baselineY = settings.mirror ? (h * 0.5) : (h * 0.85);

        QPainterPath path;
        path.moveTo(startX, baselineY);

        const double step = areaW / (barCount - 1);
        for (int i = 0; i < barCount; ++i) {
            const double px = startX + i * step;
            const double py = baselineY - (data.bands[static_cast<size_t>(i)] * areaH * (settings.mirror ? 0.5 : 1.0));
            path.lineTo(px, py);
        }
        path.lineTo(startX + areaW, baselineY);

        QLinearGradient grad(0, baselineY - areaH, 0, baselineY);
        grad.setColorAt(0.0, withAlpha(settings.secondary, 200));
        grad.setColorAt(1.0, withAlpha(settings.primary, 40));

        painter.setPen(Qt::NoPen);
        painter.setBrush(grad);
        painter.drawPath(path);

        // Crisp contour stroke on top
        QPainterPath linePath;
        linePath.moveTo(startX, baselineY - (data.bands[0] * areaH * (settings.mirror ? 0.5 : 1.0)));
        for (int i = 1; i < barCount; ++i) {
            const double px = startX + i * step;
            const double py = baselineY - (data.bands[static_cast<size_t>(i)] * areaH * (settings.mirror ? 0.5 : 1.0));
            linePath.lineTo(px, py);
        }
        QPen pen(col1, std::clamp((settings.thickness * unit), 1.0, 10.0));
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(linePath);

        if (settings.mirror) {
            QPainterPath mirrorPath;
            mirrorPath.moveTo(startX, baselineY);
            for (int i = 0; i < barCount; ++i) {
                const double px = startX + i * step;
                const double py = baselineY + (data.bands[static_cast<size_t>(i)] * areaH * 0.5);
                mirrorPath.lineTo(px, py);
            }
            mirrorPath.lineTo(startX + areaW, baselineY);

            QLinearGradient mGrad(0, baselineY, 0, baselineY + areaH * 0.5);
            mGrad.setColorAt(0.0, withAlpha(settings.primary, 40));
            mGrad.setColorAt(1.0, withAlpha(settings.secondary, 200));
            painter.setPen(Qt::NoPen);
            painter.setBrush(mGrad);
            painter.drawPath(mirrorPath);

            QPainterPath mLinePath;
            mLinePath.moveTo(startX, baselineY + (data.bands[0] * areaH * 0.5));
            for (int i = 1; i < barCount; ++i) {
                const double px = startX + i * step;
                const double py = baselineY + (data.bands[static_cast<size_t>(i)] * areaH * 0.5);
                mLinePath.lineTo(px, py);
            }
            painter.setPen(pen);
            painter.setBrush(Qt::NoBrush);
            painter.drawPath(mLinePath);
        }
        break;
    }
    case VisualizerStyle::Waveform: {
        const double areaW = w * 0.92;
        const double areaH = h * 0.35;
        const double startX = (w - areaW) * 0.5;
        const double centerY = h * 0.5;
        const double step = areaW / (barCount - 1);

        QPainterPath wavePath;
        wavePath.moveTo(startX, centerY);

        for (int i = 0; i < barCount; ++i) {
            const double px = startX + i * step;
            const double sign = (i % 2 == 0) ? 1.0 : -1.0;
            const double py = centerY + sign * data.bands[static_cast<size_t>(i)] * areaH * 0.5;
            wavePath.lineTo(px, py);
        }

        // Draw soft glow
        const double thick = std::clamp((settings.thickness * unit), 1.0, 15.0);
        QPen glowPen(withAlpha(settings.secondary, 60), thick * 3.0);
        glowPen.setCapStyle(Qt::RoundCap);
        glowPen.setJoinStyle(Qt::RoundJoin);
        painter.setPen(glowPen);
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(wavePath);

        // Draw crisp core line
        QPen corePen(col1, thick);
        corePen.setCapStyle(Qt::RoundCap);
        corePen.setJoinStyle(Qt::RoundJoin);
        painter.setPen(corePen);
        painter.drawPath(wavePath);
        break;
    }
    case VisualizerStyle::PulsingCircle: {
        const double cx = w * 0.5;
        const double cy = h * 0.5;
        const double minDim = std::min(w, h);
        const double baseRadius = minDim * 0.18;
        const double pulseRadius = baseRadius * (1.0 + data.bassLevel * 0.35 * std::clamp(settings.sensitivity, 0.5, 3.0));
        const double maxBarLen = minDim * 0.18;

        // Central pulsing disk with radial gradient
        QRadialGradient centerGrad(cx, cy, pulseRadius);
        centerGrad.setColorAt(0.0, withAlpha(settings.primary, 180));
        centerGrad.setColorAt(0.7, withAlpha(settings.secondary, 120));
        centerGrad.setColorAt(1.0, withAlpha(settings.primary, 30));

        painter.setPen(Qt::NoPen);
        painter.setBrush(centerGrad);
        painter.drawEllipse(QPointF(cx, cy), pulseRadius, pulseRadius);

        // Radial bars
        constexpr double kTwoPi = 2.0 * 3.14159265358979323846;
        const double barWidth = std::clamp((kTwoPi * pulseRadius) / (barCount * 2.2), 2.0, 10.0);

        for (int i = 0; i < barCount; ++i) {
            const double angle = (static_cast<double>(i) / barCount) * kTwoPi;
            const double cosA = std::cos(angle);
            const double sinA = std::sin(angle);
            const double len = std::max(4.0, data.bands[static_cast<size_t>(i)] * maxBarLen);

            const double x0 = cx + pulseRadius * cosA;
            const double y0 = cy + pulseRadius * sinA;
            const double x1 = cx + (pulseRadius + len) * cosA;
            const double y1 = cy + (pulseRadius + len) * sinA;

            QPen radialPen(col2, barWidth);
            radialPen.setCapStyle(Qt::RoundCap);
            painter.setPen(radialPen);
            painter.drawLine(QPointF(x0, y0), QPointF(x1, y1));
        }
        break;
    }
    }

    painter.end();
    return image;
}

} // namespace velacut::fx
