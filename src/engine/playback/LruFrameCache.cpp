// SPDX-License-Identifier: GPL-3.0-or-later
#include "LruFrameCache.h"

#include <algorithm>

namespace vedit::engine {

namespace {

inline uint8_t clamp8(int val)
{
    return static_cast<uint8_t>(std::clamp(val, 0, 255));
}

QImage convertNv12ToRgb32(const VideoFrame &frame)
{
    const int w = frame.width();
    const int h = frame.height();
    if (w <= 0 || h <= 0) {
        return {};
    }

    QImage result(w, h, QImage::Format_RGB32);
    const auto &planeY = frame.planeY();
    const auto &planeUV = frame.planeUV();
    const uint8_t *srcY = planeY.constData();
    const uint8_t *srcUV = planeUV.constData();
    const int strideY = planeY.stride > 0 ? planeY.stride : w;
    const int strideUV = planeUV.stride > 0 ? planeUV.stride : w;

    for (int y = 0; y < h; ++y) {
        const uint8_t *rowY = srcY + y * strideY;
        const uint8_t *rowUV = srcUV + (y / 2) * strideUV;
        auto *dst = reinterpret_cast<QRgb *>(result.scanLine(y));

        for (int x = 0; x < w; ++x) {
            const int Y = static_cast<int>(rowY[x]) - 16;
            const int U = static_cast<int>(rowUV[(x / 2) * 2]) - 128;
            const int V = static_cast<int>(rowUV[(x / 2) * 2 + 1]) - 128;

            const int yVal = 298 * std::max(0, Y);
            const int r = clamp8((yVal + 409 * V + 128) >> 8);
            const int g = clamp8((yVal - 100 * U - 208 * V + 128) >> 8);
            const int b = clamp8((yVal + 516 * U + 128) >> 8);

            dst[x] = qRgb(r, g, b);
        }
    }
    return result;
}

QImage convertP010ToRgb32(const VideoFrame &frame)
{
    const int w = frame.width();
    const int h = frame.height();
    if (w <= 0 || h <= 0) {
        return {};
    }

    QImage result(w, h, QImage::Format_RGB32);
    const auto &planeY = frame.planeY();
    const auto &planeUV = frame.planeUV();
    const auto *srcY = reinterpret_cast<const uint16_t *>(planeY.constData());
    const auto *srcUV = reinterpret_cast<const uint16_t *>(planeUV.constData());
    const int strideY = (planeY.stride > 0 ? planeY.stride : w * 2) / 2;
    const int strideUV = (planeUV.stride > 0 ? planeUV.stride : w * 2) / 2;

    for (int y = 0; y < h; ++y) {
        const uint16_t *rowY = srcY + y * strideY;
        const uint16_t *rowUV = srcUV + (y / 2) * strideUV;
        auto *dst = reinterpret_cast<QRgb *>(result.scanLine(y));

        for (int x = 0; x < w; ++x) {
            // P010 stores 10-bit data in MSBs of 16-bit uint (shift right by 6)
            const int Y = (rowY[x] >> 6) - 64;
            const int U = (rowUV[(x / 2) * 2] >> 6) - 512;
            const int V = (rowUV[(x / 2) * 2 + 1] >> 6) - 512;

            const int yVal = 1192 * std::max(0, Y);
            const int r = clamp8((yVal + 1634 * V + 512) >> 10);
            const int g = clamp8((yVal - 400 * U - 833 * V + 512) >> 10);
            const int b = clamp8((yVal + 2066 * U + 512) >> 10);

            dst[x] = qRgb(r, g, b);
        }
    }
    return result;
}

} // namespace

QImage VideoFrame::toImage() const
{
    if (m_format == VideoPixelFormat::Rgba8888) {
        return m_image;
    }
    if (m_format == VideoPixelFormat::Nv12) {
        return convertNv12ToRgb32(*this);
    }
    if (m_format == VideoPixelFormat::P010) {
        return convertP010ToRgb32(*this);
    }
    return {};
}

} // namespace vedit::engine
