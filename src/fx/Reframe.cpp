// SPDX-License-Identifier: GPL-3.0-or-later
#include "Reframe.h"

#include <algorithm>
#include <cmath>

namespace velacut::fx {

namespace {

// The centroid of the parts of `profile` above its mean (where the subject is, not the spread-out background).
double peakCentre(const std::vector<double> &profile, double *strength)
{
    double mean = 0.0;
    for (double value : profile) {
        mean += value;
    }
    mean /= std::max<size_t>(1, profile.size());
    double sum = 0.0;
    double weighted = 0.0;
    double total = 0.0;
    for (size_t i = 0; i < profile.size(); ++i) {
        total += profile[i];
        const double above = profile[i] - mean;
        if (above > 0.0) {
            sum += above;
            weighted += above * (static_cast<double>(i) + 0.5);
        }
    }
    if (strength) {
        *strength = total > 0.0 ? sum / total : 0.0; // how concentrated: 0 = flat, near 1 = one sharp peak
    }
    return sum > 0.0 ? weighted / sum / static_cast<double>(profile.size()) : 0.5;
}

} // namespace

SubjectPoint findSubject(const std::vector<std::uint8_t> &previous, const std::vector<std::uint8_t> &current, int width,
                         int height)
{
    SubjectPoint point;
    if (width < 8 || height < 8 || current.size() != static_cast<size_t>(width) * static_cast<size_t>(height)) {
        return point;
    }
    const bool hasPrevious = previous.size() == current.size();
    std::vector<double> columns(static_cast<size_t>(width), 0.0);
    std::vector<double> rows(static_cast<size_t>(height), 0.0);
    const auto at = [&](const std::vector<std::uint8_t> &image, int x, int y) {
        return static_cast<int>(image[static_cast<size_t>(y * width + x)]);
    };
    for (int y = 1; y < height - 1; ++y) {
        for (int x = 1; x < width - 1; ++x) {
            const int edges = std::abs(at(current, x + 1, y) - at(current, x - 1, y)) +
                              std::abs(at(current, x, y + 1) - at(current, x, y - 1));
            int motion = hasPrevious ? std::abs(at(current, x, y) - at(previous, x, y)) : 0;
            motion = motion < 8 ? 0 : motion; // noise and compression
            // What moves counts most; a mild preference for the middle (subjects are rarely at the very edge).
            const double centre = 1.0 - 0.35 * std::abs((x + 0.5) / width - 0.5) * 2.0;
            const double weight = (3.0 * motion + 0.5 * edges) * centre;
            columns[static_cast<size_t>(x)] += weight;
            rows[static_cast<size_t>(y)] += weight;
        }
    }
    double strengthX = 0.0;
    double strengthY = 0.0;
    point.x = static_cast<float>(peakCentre(columns, &strengthX));
    point.y = static_cast<float>(peakCentre(rows, &strengthY));
    point.weight = static_cast<float>(std::clamp((strengthX + strengthY) / 2.0, 0.0, 1.0));
    return point;
}

std::vector<SubjectPoint> smoothSubjectPath(const std::vector<SubjectPoint> &points, double sigma)
{
    const size_t count = points.size();
    std::vector<SubjectPoint> smooth(count);
    const int radius = static_cast<int>(std::ceil(std::max(0.0, sigma) * 2.5));
    for (size_t i = 0; i < count; ++i) {
        double x = 0.0;
        double y = 0.0;
        double total = 0.0;
        for (int k = -radius; k <= radius; ++k) {
            const long j = static_cast<long>(i) + k;
            if (j < 0 || j >= static_cast<long>(count)) {
                continue;
            }
            const SubjectPoint &p = points[static_cast<size_t>(j)];
            // Sure points weigh more; a little weight for everyone keeps the path defined when nothing stands out.
            const double w = (sigma > 0.0 ? std::exp(-0.5 * k * k / (sigma * sigma)) : (k == 0 ? 1.0 : 0.0)) * (0.05 + p.weight);
            x += w * p.x;
            y += w * p.y;
            total += w;
        }
        smooth[i] = SubjectPoint{static_cast<float>(total > 0 ? x / total : 0.5), static_cast<float>(total > 0 ? y / total : 0.5),
                                 points[i].weight};
    }
    return smooth;
}

} // namespace velacut::fx
