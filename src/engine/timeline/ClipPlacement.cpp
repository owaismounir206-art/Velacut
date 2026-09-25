// SPDX-License-Identifier: GPL-3.0-or-later
#include "ClipPlacement.h"

#include "engine/text/TextRenderer.h"

#include <cmath>
#include <numbers>

namespace vedit::engine {

namespace {

double numberOf(const Param &param, double fallback)
{
    const double *value = std::get_if<double>(&param.staticValue());
    return value ? *value : fallback;
}

Vec2 vectorOf(const Param &param, Vec2 fallback)
{
    const Vec2 *value = std::get_if<Vec2>(&param.staticValue());
    return value ? *value : fallback;
}

} // namespace

QSizeF displaySize(const Media &media)
{
    if (!media.info.video || media.info.video->width <= 0 || media.info.video->height <= 0) {
        return {1920, 1080};
    }
    const VideoStreamInfo &video = *media.info.video;
    QSizeF size(video.width * video.sampleAspectRatio.toDouble(), video.height);
    if (video.rotation == 90 || video.rotation == 270) {
        size.transpose();
    }
    return size;
}

QSizeF fittedSize(QSizeF source, FitMode fit, QSize canvas)
{
    const double sourceAspect = source.width() / std::max(1.0, source.height());
    const double canvasAspect = double(canvas.width()) / std::max(1, canvas.height());
    switch (fit) {
    case FitMode::Contain:
    case FitMode::Cover: {
        const bool wider = sourceAspect > canvasAspect;
        const bool byWidth = fit == FitMode::Contain ? wider : !wider;
        return byWidth ? QSizeF(canvas.width(), canvas.width() / sourceAspect)
                       : QSizeF(canvas.height() * sourceAspect, canvas.height());
    }
    case FitMode::Stretch:
        return QSizeF(canvas);
    case FitMode::None:
        // Pixels 1:1 relative to a 1080p canvas.
        return source * (canvas.height() / 1080.0);
    }
    return QSizeF(canvas);
}

CanvasBox canvasBox(const Clip &clip, const Media *media, QSize canvas)
{
    const Vec2 position = vectorOf(clip.transform.position, {0, 0});
    const Vec2 scale = vectorOf(clip.transform.scale, {1, 1});
    const double scaleX = std::abs(scale.x);
    const double scaleY = std::abs(clip.transform.uniformScale ? scale.x : scale.y);
    CanvasBox box;
    box.rotation = numberOf(clip.transform.rotation, 0.0);
    // The layer's centre on the canvas.
    const QPointF anchor(canvas.width() / 2.0 + position.x * canvas.width(), canvas.height() / 2.0 + position.y * canvas.height());
    if (const TextClipData *text = clip.text()) {
        // A canvas-sized layer: the text block, scaled and rotated around the canvas centre, then moved.
        const QRectF bounds = TextRenderer::bounds(*text, canvas);
        const QPointF offset = (bounds.center() - QPointF(canvas.width() / 2.0, canvas.height() / 2.0));
        const double radians = box.rotation * std::numbers::pi / 180.0;
        const QPointF scaled(offset.x() * scaleX, offset.y() * scaleY);
        box.centre = anchor + QPointF(scaled.x() * std::cos(radians) - scaled.y() * std::sin(radians),
                                      scaled.x() * std::sin(radians) + scaled.y() * std::cos(radians));
        box.size = QSizeF(bounds.width() * scaleX, bounds.height() * scaleY);
        return box;
    }
    const QSizeF fitted = media ? fittedSize(displaySize(*media), clip.transform.fit, canvas) : QSizeF(canvas);
    box.centre = anchor;
    box.size = QSizeF(fitted.width() * scaleX, fitted.height() * scaleY);
    return box;
}

} // namespace vedit::engine
