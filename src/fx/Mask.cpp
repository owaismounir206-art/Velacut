// SPDX-License-Identifier: GPL-3.0-or-later
#include "fx/Mask.h"

#include <algorithm>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace vedit::fx {

namespace {

double distanceToSegment(double px, double py, double x1, double y1, double x2, double y2)
{
    const double dx = x2 - x1;
    const double dy = y2 - y1;
    const double lenSq = dx * dx + dy * dy;
    if (lenSq < 1e-12) {
        return std::hypot(px - x1, py - y1);
    }
    const double t = std::clamp(((px - x1) * dx + (py - y1) * dy) / lenSq, 0.0, 1.0);
    const double projX = x1 + t * dx;
    const double projY = y1 + t * dy;
    return std::hypot(px - projX, py - projY);
}

} // namespace

double evaluateMask(const MaskParams &mask, double u, double v, int width, int height)
{
    const double du = u - mask.centerX;
    const double dv = v - mask.centerY;

    const double rad = -mask.rotation * (M_PI / 180.0);
    const double cosR = std::cos(rad);
    const double sinR = std::sin(rad);

    const double ru = du * cosR - dv * sinR;
    const double rv = du * sinR + dv * cosR;

    const double hx = std::max(mask.sizeX * 0.5, 1e-6);
    const double hy = std::max(mask.sizeY * 0.5, 1e-6);

    double dist = 0.0;

    switch (mask.shape) {
    case MaskShape::Rectangle: {
        const double absRu = std::abs(ru);
        const double absRv = std::abs(rv);
        const double r = std::min(hx, hy) * std::clamp(mask.roundness, 0.0, 1.0);
        const double qx = absRu - hx + r;
        const double qy = absRv - hy + r;
        dist = std::hypot(std::max(qx, 0.0), std::max(qy, 0.0)) + std::min(std::max(qx, qy), 0.0) - r;
        break;
    }
    case MaskShape::Circle: {
        const double d = std::hypot(ru / hx, rv / hy);
        dist = (d - 1.0) * std::min(hx, hy);
        break;
    }
    case MaskShape::Linear: {
        dist = rv;
        break;
    }
    case MaskShape::Mirror: {
        dist = std::abs(rv) - hy;
        break;
    }
    case MaskShape::Heart: {
        const double nx = ru / hx;
        const double ny = -rv / hy + 0.25;
        const double a = nx * nx + ny * ny - 1.0;
        const double val = a * a * a - nx * nx * ny * ny * ny;
        dist = val * std::min(hx, hy) * 0.4;
        break;
    }
    case MaskShape::Star: {
        const double theta = std::atan2(rv, ru);
        const double rho = std::hypot(ru / hx, rv / hy);
        const double a = std::fmod(theta + M_PI * 0.5 + 4.0 * M_PI, 2.0 * M_PI / 5.0) - M_PI / 5.0;
        constexpr double m = 0.45;
        const double sinPi5 = std::sin(M_PI / 5.0);
        const double cosPi5 = std::cos(M_PI / 5.0);
        const double absA = std::abs(a);
        const double denom = std::sin(absA) * (1.0 - m * cosPi5) + std::cos(absA) * (m * sinPi5);
        const double r_edge = (denom > 1e-6) ? ((m * sinPi5) / denom) : 1.0;
        dist = (rho - r_edge) * std::min(hx, hy);
        break;
    }
    case MaskShape::Path: {
        if (mask.points.size() < 3) {
            dist = 1.0;
            break;
        }
        bool inside = false;
        double minDist = 1e9;
        const size_t n = mask.points.size();
        for (size_t i = 0, j = n - 1; i < n; j = i++) {
            const double xi = mask.points[i].x;
            const double yi = mask.points[i].y;
            const double xj = mask.points[j].x;
            const double yj = mask.points[j].y;

            if (((yi > v) != (yj > v)) && (u < (xj - xi) * (v - yi) / (yj - yi) + xi)) {
                inside = !inside;
            }
            minDist = std::min(minDist, distanceToSegment(u, v, xi, yi, xj, yj));
        }
        dist = inside ? -minDist : minDist;
        break;
    }
    }

    double alpha = 0.0;
    if (mask.feather <= 1e-4) {
        const double pixelSize = 1.0 / std::max({width, height, 1});
        alpha = std::clamp(0.5 - dist / pixelSize, 0.0, 1.0);
    } else {
        const double t = std::clamp(0.5 - 0.5 * dist / mask.feather, 0.0, 1.0);
        alpha = t * t * (3.0 - 2.0 * t);
    }

    if (mask.invert) {
        alpha = 1.0 - alpha;
    }

    return alpha;
}

void applyMasks(ImageView image, const std::vector<MaskParams> &masks, int rowBegin, int rowEnd)
{
    if (masks.empty() || !image.data || image.width <= 0 || image.height <= 0) {
        return;
    }

    const int w = image.width;
    const int h = image.height;
    const double invW = 1.0 / static_cast<double>(w);
    const double invH = 1.0 / static_cast<double>(h);

    const int yStart = std::clamp(rowBegin, 0, h);
    const int yEnd = std::clamp(rowEnd, yStart, h);

    for (int y = yStart; y < yEnd; ++y) {
        const double v = (static_cast<double>(y) + 0.5) * invH - 0.5;
        uint8_t *row = image.row(y);

        for (int x = 0; x < w; ++x) {
            const double u = (static_cast<double>(x) + 0.5) * invW - 0.5;

            double alphaCombined = 0.0;
            for (size_t m = 0; m < masks.size(); ++m) {
                const double a = evaluateMask(masks[m], u, v, w, h);
                alphaCombined = (m == 0) ? a : std::max(alphaCombined, a);
            }

            uint8_t *pixel = row + x * 4;
            pixel[3] = static_cast<uint8_t>(std::clamp<long>(std::lround(static_cast<double>(pixel[3]) * alphaCombined), 0L, 255L));
        }
    }
}

} // namespace vedit::fx
