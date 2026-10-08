// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QRectF>
#include <QSize>
#include <QSizeF>

namespace velacut::ui {

// Aspect-fit ("contain") of a frame inside a target: the fraction of the target covered on each axis.
inline QSizeF fitScale(const QSize &frame, const QSize &target)
{
    if (frame.isEmpty() || target.isEmpty()) {
        return {1.0, 1.0};
    }
    const double frameAspect = double(frame.width()) / frame.height();
    const double targetAspect = double(target.width()) / target.height();
    return frameAspect > targetAspect ? QSizeF(1.0, targetAspect / frameAspect) : QSizeF(frameAspect / targetAspect, 1.0);
}

// The same fit as a rectangle centred in `target`.
inline QRectF fitRect(const QSize &frame, const QSizeF &target)
{
    const QSizeF scale = fitScale(frame, target.toSize());
    const QSizeF size(target.width() * scale.width(), target.height() * scale.height());
    return QRectF(QPointF((target.width() - size.width()) / 2, (target.height() - size.height()) / 2), size);
}

} // namespace velacut::ui
