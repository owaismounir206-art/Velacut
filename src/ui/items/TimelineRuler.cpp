// SPDX-License-Identifier: GPL-3.0-or-later
#include "TimelineRuler.h"

#include "core/time/Timecode.h"

#include <QPainter>
#include <QQuickWindow>

#include <array>
#include <cmath>

namespace velacut::ui {

namespace {

constexpr int kChunkWidth = 512;

template<typename T>
bool assign(T &member, const T &value)
{
    if (member == value) {
        return false;
    }
    member = value;
    return true;
}

} // namespace

TimelineRuler::TimelineRuler(QQuickItem *parent)
    : QQuickItem(parent)
{
    setFlag(ItemHasContents);
}

#define VELACUT_RULER_SETTER(Name, Type, member)                                                                       \
    void TimelineRuler::set##Name(Type value)                                                                          \
    {                                                                                                                  \
        if (assign(member, value)) {                                                                                   \
            emit changed();                                                                                            \
            invalidate();                                                                                              \
        }                                                                                                              \
    }
VELACUT_RULER_SETTER(Zoom, double, m_zoom)
VELACUT_RULER_SETTER(FrameRateNum, int, m_num)
VELACUT_RULER_SETTER(FrameRateDen, int, m_den)
VELACUT_RULER_SETTER(LabelSpacing, double, m_labelSpacing)
VELACUT_RULER_SETTER(MajorTick, double, m_majorTick)
VELACUT_RULER_SETTER(MinorTick, double, m_minorTick)
VELACUT_RULER_SETTER(Hairline, double, m_hairline)
VELACUT_RULER_SETTER(LabelInset, double, m_labelInset)
VELACUT_RULER_SETTER(MajorColor, const QColor &, m_majorColor)
VELACUT_RULER_SETTER(MinorColor, const QColor &, m_minorColor)
VELACUT_RULER_SETTER(LabelColor, const QColor &, m_labelColor)
VELACUT_RULER_SETTER(Font, const QFont &, m_font)
#undef VELACUT_RULER_SETTER

void TimelineRuler::setContentX(double x)
{
    if (assign(m_contentX, x)) {
        emit changed();
        layoutTiles(); // scrolling: the same chunks, elsewhere
    }
}

void TimelineRuler::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.height() != oldGeometry.height()) {
        invalidate();
    } else if (newGeometry.width() != oldGeometry.width()) {
        layoutTiles();
    }
}

int TimelineRuler::step() const
{
    const int fps = std::max(1, static_cast<int>(std::lround(static_cast<double>(m_num) / std::max(1, m_den))));
    const std::array<int, 17> steps{1, 2, 5, 10, 15, fps, 2 * fps, 5 * fps, 10 * fps, 15 * fps, 30 * fps, 60 * fps,
                                    120 * fps, 300 * fps, 600 * fps, 1800 * fps, 3600 * fps};
    for (const int candidate : steps) {
        if (candidate * m_zoom >= m_labelSpacing) {
            return candidate;
        }
    }
    return steps.back();
}

int TimelineRuler::minorDivisions() const
{
    const int fps = std::max(1, static_cast<int>(std::lround(static_cast<double>(m_num) / std::max(1, m_den))));
    const int s = step();
    if (s < fps) {
        return s == 15 ? 3 : s >= 10 ? 5 : s;
    }
    const int seconds = s / fps;
    return seconds <= 2 ? 4 : seconds % 5 == 0 ? 5 : seconds % 3 == 0 ? 3 : 2;
}

std::vector<TimelineRuler::Tick> TimelineRuler::ticks(std::int64_t firstFrame, std::int64_t lastFrame) const
{
    std::vector<Tick> result;
    const Rational rate(std::max(1, m_num), std::max(1, m_den));
    const int fps = std::max(1, static_cast<int>(std::lround(rate.toDouble())));
    const int s = step();
    firstFrame = std::max<std::int64_t>(0, firstFrame);
    if (s < fps) {
        // Below a second: every `s` frames, with the frames in the label.
        for (std::int64_t k = firstFrame / s; k * s <= lastFrame; ++k) {
            result.push_back({k * s, formatTimecode(k * s, rate)});
        }
        return result;
    }
    // Whole seconds: on real seconds (the first frame that starts in each), whatever the frame rate.
    const std::int64_t seconds = s / fps;
    const std::int64_t firstSecond = firstFrame * rate.den() / rate.num();
    for (std::int64_t k = firstSecond / seconds;; ++k) {
        const std::int64_t frame = firstFrameOfSecond(k * seconds, rate);
        if (frame > lastFrame) {
            break;
        }
        if (frame >= firstFrame) {
            result.push_back({frame, formatSeconds(frame, rate)});
        }
    }
    return result;
}

void TimelineRuler::invalidate()
{
    m_chunks.clear();
    layoutTiles();
}

void TimelineRuler::layoutTiles()
{
    m_tiles.clear();
    m_images.clear();
    if (width() > 0 && height() > 0 && m_zoom > 0) {
        const int first = static_cast<int>(std::floor(std::max(0.0, m_contentX) / kChunkWidth));
        const int last = static_cast<int>(std::floor((m_contentX + width()) / kChunkWidth));
        QHash<int, QImage> kept;
        for (int chunk = first; chunk <= last; ++chunk) {
            QImage image = m_chunks.value(chunk);
            if (image.isNull()) {
                image = paintChunk(chunk);
            }
            kept.insert(chunk, image);
            m_images.insert(image.cacheKey(), image);
            ImageTile tile{image.cacheKey(), QRectF(chunk * kChunkWidth - m_contentX, 0, kChunkWidth, height()),
                           QRectF(0, 0, image.width(), image.height())};
            if (clipTile(tile, boundingRect())) {
                m_tiles.push_back(tile);
            }
        }
        m_chunks = std::move(kept);
    }
    update();
}

QImage TimelineRuler::paintChunk(int chunk) const
{
    const qreal ratio = window() ? window()->effectiveDevicePixelRatio() : 1.0;
    QImage image(static_cast<int>(std::ceil(kChunkWidth * ratio)), static_cast<int>(std::ceil(height() * ratio)),
                 QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(ratio);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    QFont font = m_font;
    // In pixels as on screen: an image counts points at 72 dpi, which would make the labels smaller.
    font.setPixelSize(QFontInfo(m_font).pixelSize());
    font.setFeature(QFont::Tag("tnum"), 1); // figures of one width: labels do not wobble
    painter.setFont(font);
    const double x0 = static_cast<double>(chunk) * kChunkWidth;
    // Ticks of this chunk, and labels from a little before it (they run to the right of their tick).
    const auto firstFrame = static_cast<std::int64_t>(std::floor((x0 - m_labelSpacing) / m_zoom));
    const auto lastFrame = static_cast<std::int64_t>(std::ceil((x0 + kChunkWidth) / m_zoom)) + 1;
    const std::vector<Tick> major = ticks(firstFrame, lastFrame + step());
    const int divisions = minorDivisions();
    for (size_t i = 0; i < major.size(); ++i) {
        const double x = major[i].frame * m_zoom - x0;
        painter.fillRect(QRectF(x, height() - m_majorTick, m_hairline, m_majorTick), m_majorColor);
        if (i + 1 < major.size()) {
            const double next = major[i + 1].frame * m_zoom - x0;
            for (int d = 1; d < divisions; ++d) {
                const double minor = x + (next - x) * d / divisions;
                painter.fillRect(QRectF(minor, height() - m_minorTick, m_hairline, m_minorTick), m_minorColor);
            }
        }
        painter.setPen(m_labelColor);
        painter.drawText(QPointF(x + m_labelInset, m_labelInset + QFontMetricsF(font).ascent()), major[i].label);
    }
    // The line along the bottom.
    painter.fillRect(QRectF(0, height() - m_hairline, kChunkWidth, m_hairline), m_minorColor);
    return image;
}

QSGNode *TimelineRuler::updatePaintNode(QSGNode *old, UpdatePaintNodeData *)
{
    return updateImageTiles(window(), old, m_tiles, m_images);
}

} // namespace velacut::ui
