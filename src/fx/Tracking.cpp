// SPDX-License-Identifier: GPL-3.0-or-later
#include "Tracking.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace vedit::fx {

PointTracker::PointTracker(const std::vector<std::uint8_t> &first, int width, int height, double x, double y, int radius)
    : m_width(width)
    , m_height(height)
    , m_radius(std::max(3, radius))
    , m_point{x, y, false}
{
    const int side = 2 * m_radius + 1;
    m_patch.resize(static_cast<size_t>(side * side));
    const int cx = static_cast<int>(std::lround(x));
    const int cy = static_cast<int>(std::lround(y));
    for (int dy = -m_radius; dy <= m_radius; ++dy) {
        for (int dx = -m_radius; dx <= m_radius; ++dx) {
            const int px = std::clamp(cx + dx, 0, width - 1);
            const int py = std::clamp(cy + dy, 0, height - 1);
            m_patch[static_cast<size_t>((dy + m_radius) * side + dx + m_radius)] = first[static_cast<size_t>(py * width + px)];
        }
    }
    m_reference = 4.0;
}

double PointTracker::cost(const std::vector<std::uint8_t> &frame, int cx, int cy, double give) const
{
    const int side = 2 * m_radius + 1;
    double sum = 0.0;
    int count = 0;
    for (int dy = -m_radius; dy <= m_radius; dy += 1) {
        const int py = cy + dy;
        if (py < 0 || py >= m_height) {
            continue;
        }
        for (int dx = -m_radius; dx <= m_radius; dx += 1) {
            const int px = cx + dx;
            if (px < 0 || px >= m_width) {
                continue;
            }
            sum += std::abs(frame[static_cast<size_t>(py * m_width + px)] -
                            m_patch[static_cast<size_t>((dy + m_radius) * side + dx + m_radius)]);
            ++count;
        }
    }
    // Half the patch outside the picture is no match.
    if (count < side * side / 2) {
        return std::numeric_limits<double>::max();
    }
    return sum / count + give;
}

PointTracker::Point PointTracker::update(const std::vector<std::uint8_t> &frame)
{
    if (frame.size() != static_cast<size_t>(m_width) * static_cast<size_t>(m_height)) {
        return m_point;
    }
    const int px = static_cast<int>(std::lround(m_point.x));
    const int py = static_cast<int>(std::lround(m_point.y));
    // Wider search when lost (it may have moved on meanwhile).
    const int range = m_point.lost ? m_radius * 4 : std::max(4, m_radius * 2);
    // A small cost for distance: of two equal matches, the nearer one (flat areas, repeated patterns).
    const auto give = [](int dx, int dy) { return 0.02 * std::sqrt(double(dx * dx + dy * dy)); };
    int bestX = px;
    int bestY = py;
    double best = std::numeric_limits<double>::max();
    for (int dy = -range; dy <= range; dy += 2) {
        for (int dx = -range; dx <= range; dx += 2) {
            const double c = cost(frame, px + dx, py + dy, give(dx, dy));
            if (c < best) {
                best = c;
                bestX = px + dx;
                bestY = py + dy;
            }
        }
    }
    const int cx = bestX;
    const int cy = bestY;
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            const double c = cost(frame, cx + dx, cy + dy, give(cx + dx - px, cy + dy - py));
            if (c < best) {
                best = c;
                bestX = cx + dx;
                bestY = cy + dy;
            }
        }
    }
    // Sub-pixel: the cost grows like |shift| around the best place.
    const auto refine = [&](int ox, int oy) {
        const double minus = cost(frame, bestX - ox, bestY - oy, 0.0);
        const double plus = cost(frame, bestX + ox, bestY + oy, 0.0);
        const double centre = cost(frame, bestX, bestY, 0.0);
        const double denominator = 2.0 * (std::max(minus, plus) - centre);
        return denominator > 1e-9 && minus < 1e300 && plus < 1e300 ? std::clamp((minus - plus) / denominator, -0.5, 0.5) : 0.0;
    };
    const double match = cost(frame, bestX, bestY, 0.0);
    // Lost: much worse than the matches so far.
    if (match > std::max(18.0, m_reference * 3.5)) {
        m_point.lost = true;
        return m_point;
    }
    m_point = Point{bestX + refine(1, 0), bestY + refine(0, 1), false};
    m_reference = 0.9 * m_reference + 0.1 * match;
    // The patch follows slowly the look of what it tracks.
    const int side = 2 * m_radius + 1;
    for (int dy = -m_radius; dy <= m_radius; ++dy) {
        for (int dx = -m_radius; dx <= m_radius; ++dx) {
            const int x = std::clamp(bestX + dx, 0, m_width - 1);
            const int y = std::clamp(bestY + dy, 0, m_height - 1);
            float &value = m_patch[static_cast<size_t>((dy + m_radius) * side + dx + m_radius)];
            value = 0.9f * value + 0.1f * frame[static_cast<size_t>(y * m_width + x)];
        }
    }
    return m_point;
}

} // namespace vedit::fx
