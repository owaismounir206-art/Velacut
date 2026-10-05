// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <vector>

namespace vedit::fx {

// Follows a point of a video from frame to frame (motion tracking, SPEC §5.7: a text or a sticker that stays on what it
// points at): the patch around the point in the first grey frame is looked for near its last place in each next frame
// (mean absolute difference, coarse then fine, sub-pixel), and the patch slowly takes the new look of what it follows
// (light, turning). When nothing matches well enough the point stays where it was ("lost") until it is found again.
class PointTracker
{
public:
    // `x`, `y` in pixels of `width` × `height` frames; `radius`: half the side of the patch, in pixels.
    PointTracker(const std::vector<std::uint8_t> &first, int width, int height, double x, double y, int radius);

    struct Point
    {
        double x = 0.0;
        double y = 0.0;
        bool lost = false;
    };
    Point update(const std::vector<std::uint8_t> &frame);
    Point point() const { return m_point; }

private:
    double cost(const std::vector<std::uint8_t> &frame, int cx, int cy, double give) const;

    int m_width = 0;
    int m_height = 0;
    int m_radius = 8;
    std::vector<float> m_patch; // (2r+1)² grey values
    double m_reference = 0.0;   // the cost of a good match (noise of the video)
    Point m_point;
};

} // namespace vedit::fx
