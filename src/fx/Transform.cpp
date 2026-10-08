// SPDX-License-Identifier: GPL-3.0-or-later
#include "Transform.h"

#include <algorithm>
#include <cmath>

namespace velacut::fx {

Affine Affine::rotation(double degrees)
{
    const double r = degrees * M_PI / 180.0;
    const double c = std::cos(r);
    const double s = std::sin(r);
    // y down: a positive angle turns clockwise on screen.
    return {c, -s, s, c, 0, 0};
}

Affine Affine::operator*(const Affine &first) const
{
    return {m11 * first.m11 + m12 * first.m21, m11 * first.m12 + m12 * first.m22,
            m21 * first.m11 + m22 * first.m21, m21 * first.m12 + m22 * first.m22,
            m11 * first.dx + m12 * first.dy + dx, m21 * first.dx + m22 * first.dy + dy};
}

Affine Affine::inverted() const
{
    const double det = m11 * m22 - m12 * m21;
    if (std::abs(det) < 1e-12) {
        return {0, 0, 0, 0, -1e9, -1e9}; // degenerate: maps everything outside the source
    }
    const double i11 = m22 / det;
    const double i12 = -m12 / det;
    const double i21 = -m21 / det;
    const double i22 = m11 / det;
    return {i11, i12, i21, i22, -(i11 * dx + i12 * dy), -(i21 * dx + i22 * dy)};
}

namespace {

struct Premultiplied
{
    float r = 0, g = 0, b = 0, a = 0;
};

inline Premultiplied fetch(ConstImageView image, const SourceWindow &window, int x, int y)
{
    // Pixel centres at x + 0.5: a pixel is inside the window if its centre is.
    if (x < 0 || y < 0 || x >= image.width || y >= image.height || x + 0.5 < window.left || x + 0.5 > window.right ||
        y + 0.5 < window.top || y + 0.5 > window.bottom) {
        return {};
    }
    const std::uint8_t *p = image.row(y) + x * 4;
    const float a = p[3] / 255.0f;
    return {p[0] * a, p[1] * a, p[2] * a, a};
}

inline Premultiplied sampleBilinear(ConstImageView image, const SourceWindow &window, double sx, double sy)
{
    const double fx = sx - 0.5;
    const double fy = sy - 0.5;
    const int x0 = static_cast<int>(std::floor(fx));
    const int y0 = static_cast<int>(std::floor(fy));
    const float tx = static_cast<float>(fx - x0);
    const float ty = static_cast<float>(fy - y0);
    const Premultiplied p00 = fetch(image, window, x0, y0);
    const Premultiplied p10 = fetch(image, window, x0 + 1, y0);
    const Premultiplied p01 = fetch(image, window, x0, y0 + 1);
    const Premultiplied p11 = fetch(image, window, x0 + 1, y0 + 1);
    const auto lerp = [](float a, float b, float t) { return a + (b - a) * t; };
    const auto mix = [&](float Premultiplied::*c) {
        return lerp(lerp(p00.*c, p10.*c, tx), lerp(p01.*c, p11.*c, tx), ty);
    };
    return {mix(&Premultiplied::r), mix(&Premultiplied::g), mix(&Premultiplied::b), mix(&Premultiplied::a)};
}

inline void blendOver(std::uint8_t *d, Premultiplied s)
{
    if (s.a <= 0.0f) {
        return;
    }
    const float da = d[3] / 255.0f;
    const float inverse = 1.0f - s.a;
    const float outA = s.a + da * inverse;
    const float r = s.r + d[0] * da * inverse;
    const float g = s.g + d[1] * da * inverse;
    const float b = s.b + d[2] * da * inverse;
    const auto clampByte = [](float v) { return static_cast<std::uint8_t>(std::clamp(std::lround(v), 0L, 255L)); };
    d[0] = clampByte(r / outA);
    d[1] = clampByte(g / outA);
    d[2] = clampByte(b / outA);
    d[3] = clampByte(outA * 255.0f);
}

bool isIntegerTranslation(const Affine &m)
{
    return m.m11 == 1 && m.m22 == 1 && m.m12 == 0 && m.m21 == 0 && m.dx == std::floor(m.dx) && m.dy == std::floor(m.dy);
}

} // namespace

void drawAffine(ImageView destination, ConstImageView source, const Affine &destinationToSource,
                const SourceWindow &window, double opacity, int rowBegin, int rowEnd)
{
    const float op = static_cast<float>(std::clamp(opacity, 0.0, 1.0));
    if (op <= 0.0f || source.width <= 0 || source.height <= 0) {
        return;
    }
    SourceWindow clipped = window;
    clipped.left = std::max(0.0, clipped.left);
    clipped.top = std::max(0.0, clipped.top);
    clipped.right = std::min<double>(source.width, clipped.right);
    clipped.bottom = std::min<double>(source.height, clipped.bottom);
    if (clipped.right <= clipped.left || clipped.bottom <= clipped.top) {
        return;
    }

    // Destination bounding box of the window (plus one pixel for the soft edge).
    const Affine forward = destinationToSource.inverted();
    double minX = 1e18, minY = 1e18, maxX = -1e18, maxY = -1e18;
    for (const auto &[cx, cy] : {std::pair{clipped.left, clipped.top}, std::pair{clipped.right, clipped.top},
                                 std::pair{clipped.left, clipped.bottom}, std::pair{clipped.right, clipped.bottom}}) {
        double x = 0, y = 0;
        forward.map(cx, cy, x, y);
        minX = std::min(minX, x);
        minY = std::min(minY, y);
        maxX = std::max(maxX, x);
        maxY = std::max(maxY, y);
    }
    const int x0 = std::max(0, static_cast<int>(std::floor(minX)) - 1);
    const int x1 = std::min(destination.width, static_cast<int>(std::ceil(maxX)) + 1);
    const int y0 = std::max({0, rowBegin, static_cast<int>(std::floor(minY)) - 1});
    const int y1 = std::min({destination.height, rowEnd, static_cast<int>(std::ceil(maxY)) + 1});

    if (isIntegerTranslation(destinationToSource)) {
        // Exact copy: destination pixel (x, y) is source pixel (x + dx, y + dy).
        const int ox = static_cast<int>(destinationToSource.dx);
        const int oy = static_cast<int>(destinationToSource.dy);
        for (int y = y0; y < y1; ++y) {
            for (int x = x0; x < x1; ++x) {
                Premultiplied p = fetch(source, clipped, x + ox, y + oy);
                if (p.a > 0.0f) {
                    p.r *= op;
                    p.g *= op;
                    p.b *= op;
                    p.a *= op;
                    blendOver(destination.row(y) + x * 4, p);
                }
            }
        }
        return;
    }
    for (int y = y0; y < y1; ++y) {
        std::uint8_t *d = destination.row(y) + x0 * 4;
        for (int x = x0; x < x1; ++x, d += 4) {
            double sx = 0, sy = 0;
            destinationToSource.map(x + 0.5, y + 0.5, sx, sy);
            if (sx < clipped.left - 1 || sy < clipped.top - 1 || sx > clipped.right + 1 || sy > clipped.bottom + 1) {
                continue;
            }
            Premultiplied p = sampleBilinear(source, clipped, sx, sy);
            p.r *= op;
            p.g *= op;
            p.b *= op;
            p.a *= op;
            blendOver(d, p);
        }
    }
}

void resizeBilinear(ImageView destination, ConstImageView source, int rowBegin, int rowEnd)
{
    if (destination.width <= 0 || destination.height <= 0 || source.width <= 0 || source.height <= 0) {
        return;
    }
    const SourceWindow all{0, 0, double(source.width), double(source.height)};
    const double sx = double(source.width) / destination.width;
    const double sy = double(source.height) / destination.height;
    // Downscaling by more than 2 averages boxes first, so that fine detail does not alias.
    for (int y = std::max(0, rowBegin); y < std::min(rowEnd, destination.height); ++y) {
        std::uint8_t *d = destination.row(y);
        for (int x = 0; x < destination.width; ++x, d += 4) {
            Premultiplied p;
            if (sx > 2.0 || sy > 2.0) {
                const int bx0 = static_cast<int>(x * sx);
                const int by0 = static_cast<int>(y * sy);
                const int bx1 = std::max(bx0 + 1, static_cast<int>((x + 1) * sx));
                const int by1 = std::max(by0 + 1, static_cast<int>((y + 1) * sy));
                int count = 0;
                for (int yy = by0; yy < std::min(by1, source.height); ++yy) {
                    for (int xx = bx0; xx < std::min(bx1, source.width); ++xx) {
                        const Premultiplied q = fetch(source, all, xx, yy);
                        p.r += q.r;
                        p.g += q.g;
                        p.b += q.b;
                        p.a += q.a;
                        ++count;
                    }
                }
                if (count > 0) {
                    p.r /= count;
                    p.g /= count;
                    p.b /= count;
                    p.a /= count;
                }
            } else {
                // Clamp to the edges instead of fading out.
                const double cx = std::clamp((x + 0.5) * sx, 0.5, source.width - 0.5);
                const double cy = std::clamp((y + 0.5) * sy, 0.5, source.height - 0.5);
                p = sampleBilinear(source, all, cx, cy);
            }
            const auto clampByte = [](float v) { return static_cast<std::uint8_t>(std::clamp(std::lround(v), 0L, 255L)); };
            if (p.a <= 0.0f) {
                d[0] = d[1] = d[2] = d[3] = 0;
            } else {
                d[0] = clampByte(p.r / p.a);
                d[1] = clampByte(p.g / p.a);
                d[2] = clampByte(p.b / p.a);
                d[3] = clampByte(p.a * 255.0f);
            }
        }
    }
}

} // namespace velacut::fx
