// SPDX-License-Identifier: GPL-3.0-or-later
#include "Stabilization.h"

#include "fx/Transform.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace vedit::fx {

namespace {

constexpr double kPi = 3.14159265358979323846;
// Grey levels per pixel of shift added to a match's cost (mean absolute difference, 0…255).
constexpr double kShiftPenalty = 0.02;

// Mean absolute difference between a block of `current` at (x, y) and the block of `previous` moved by (sx, sy).
double blockCost(const std::vector<std::uint8_t> &previous, const std::vector<std::uint8_t> &current, int width, int height,
                 int x0, int y0, int blockWidth, int blockHeight, int sx, int sy, int step)
{
    double sum = 0.0;
    int count = 0;
    for (int y = y0; y < y0 + blockHeight; y += step) {
        const int py = y - sy;
        if (py < 0 || py >= height) {
            continue;
        }
        for (int x = x0; x < x0 + blockWidth; x += step) {
            const int px = x - sx;
            if (px < 0 || px >= width) {
                continue;
            }
            sum += std::abs(int(current[static_cast<size_t>(y * width + x)]) - int(previous[static_cast<size_t>(py * width + px)]));
            ++count;
        }
    }
    return count > 0 ? sum / count : std::numeric_limits<double>::max();
}

// The translation (pixels) of one block: the best integer shift in a window, then sub-pixel from the neighbours.
std::array<double, 2> blockShift(const std::vector<std::uint8_t> &previous, const std::vector<std::uint8_t> &current, int width,
                                 int height, int x0, int y0, int blockWidth, int blockHeight)
{
    const int range = std::max(2, width / 12);
    // Coarse: every other pixel and shift.
    int bestX = 0;
    int bestY = 0;
    double best = std::numeric_limits<double>::max();
    for (int sy = -range; sy <= range; sy += 2) {
        for (int sx = -range; sx <= range; sx += 2) {
            // A flat region matches everywhere: the smallest shift wins the ties.
            const double cost = blockCost(previous, current, width, height, x0, y0, blockWidth, blockHeight, sx, sy, 2) +
                                kShiftPenalty * (std::abs(sx) + std::abs(sy));
            if (cost < best) {
                best = cost;
                bestX = sx;
                bestY = sy;
            }
        }
    }
    // Fine: every pixel, around the coarse best.
    const int cx = bestX;
    const int cy = bestY;
    best = std::numeric_limits<double>::max();
    for (int sy = cy - 2; sy <= cy + 2; ++sy) {
        for (int sx = cx - 2; sx <= cx + 2; ++sx) {
            const double cost = blockCost(previous, current, width, height, x0, y0, blockWidth, blockHeight, sx, sy, 1) +
                                kShiftPenalty * (std::abs(sx) + std::abs(sy));
            if (cost < best) {
                best = cost;
                bestX = sx;
                bestY = sy;
            }
        }
    }
    best = blockCost(previous, current, width, height, x0, y0, blockWidth, blockHeight, bestX, bestY, 1);
    const auto refine = [&](int dx, int dy) {
        const double minus = blockCost(previous, current, width, height, x0, y0, blockWidth, blockHeight, bestX - dx, bestY - dy, 1);
        const double plus = blockCost(previous, current, width, height, x0, y0, blockWidth, blockHeight, bestX + dx, bestY + dy, 1);
        // Equiangular line fit: the cost of a shift grows like |shift| around the best one (absolute differences).
        const double denominator = 2.0 * (std::max(minus, plus) - best);
        return denominator > 1e-9 ? std::clamp((minus - plus) / denominator, -0.5, 0.5) : 0.0;
    };
    return {bestX + refine(1, 0), bestY + refine(0, 1)};
}

} // namespace

CameraStep estimateCameraStep(const std::vector<std::uint8_t> &previous, const std::vector<std::uint8_t> &current, int width,
                              int height)
{
    if (width < 16 || height < 16 || previous.size() != current.size() ||
        current.size() != static_cast<size_t>(width) * static_cast<size_t>(height)) {
        return {};
    }
    // Four regions around the centre, away from the borders (what comes in or leaves the picture).
    const int blockWidth = width * 3 / 10;
    const int blockHeight = height * 3 / 10;
    const std::array<std::array<int, 2>, 4> origins{{{width / 8, height / 8},
                                                     {width - width / 8 - blockWidth, height / 8},
                                                     {width / 8, height - height / 8 - blockHeight},
                                                     {width - width / 8 - blockWidth, height - height / 8 - blockHeight}}};
    std::array<std::array<double, 2>, 4> shifts{};
    std::array<std::array<double, 2>, 4> centres{};
    for (size_t i = 0; i < origins.size(); ++i) {
        shifts[i] = blockShift(previous, current, width, height, origins[i][0], origins[i][1], blockWidth, blockHeight);
        centres[i] = {origins[i][0] + blockWidth / 2.0 - width / 2.0, origins[i][1] + blockHeight / 2.0 - height / 2.0};
    }
    // The median of the four (the mean of the two middle ones): one region with something moving in it, or with
    // nothing to match, does not pull the result.
    const auto median = [&shifts](size_t axis) {
        std::array<double, 4> values{shifts[0][axis], shifts[1][axis], shifts[2][axis], shifts[3][axis]};
        std::sort(values.begin(), values.end());
        return (values[1] + values[2]) / 2.0;
    };
    const double meanX = median(0);
    const double meanY = median(1);
    // Small rotation: how the regions move across their direction from the centre.
    double turn = 0.0;
    double norm = 0.0;
    for (size_t i = 0; i < origins.size(); ++i) {
        const double rx = centres[i][0];
        const double ry = centres[i][1];
        turn += rx * (shifts[i][1] - meanY) - ry * (shifts[i][0] - meanX);
        norm += rx * rx + ry * ry;
    }
    const double radians = norm > 0.0 ? turn / norm : 0.0;
    return CameraStep{static_cast<float>(meanX / width), static_cast<float>(meanY / height),
                      static_cast<float>(radians * 180.0 / kPi)};
}

QByteArray encodeCameraSteps(const std::vector<CameraStep> &steps)
{
    QByteArray bytes;
    bytes.reserve(static_cast<qsizetype>(steps.size() * 6));
    const auto put = [&bytes](double value, double scale) {
        const auto v = static_cast<std::int16_t>(std::clamp(std::lround(value * scale), -32767L, 32767L));
        const auto u = static_cast<std::uint16_t>(v);
        bytes.append(static_cast<char>(u & 0xff));
        bytes.append(static_cast<char>(u >> 8));
    };
    for (const CameraStep &step : steps) {
        put(step.dx, 10000.0);
        put(step.dy, 10000.0);
        put(step.angle, 1000.0);
    }
    return bytes.toBase64();
}

std::vector<CameraStep> decodeCameraSteps(const QByteArray &base64)
{
    const QByteArray bytes = QByteArray::fromBase64(base64);
    std::vector<CameraStep> steps;
    const auto get = [&bytes](qsizetype at, double scale) {
        const auto u = static_cast<std::uint16_t>(static_cast<std::uint8_t>(bytes[at]) |
                                                  (static_cast<std::uint8_t>(bytes[at + 1]) << 8));
        return static_cast<float>(static_cast<std::int16_t>(u) / scale);
    };
    for (qsizetype at = 0; at + 6 <= bytes.size(); at += 6) {
        steps.push_back(CameraStep{get(at, 10000.0), get(at + 2, 10000.0), get(at + 4, 1000.0)});
    }
    return steps;
}

Stabilization stabilize(const std::vector<CameraStep> &steps, double framesPerSecond, double strength)
{
    Stabilization result;
    const size_t count = steps.size();
    if (count == 0) {
        return result;
    }
    // The camera's path, frame by frame.
    std::vector<std::array<double, 3>> path(count);
    std::array<double, 3> position{};
    for (size_t i = 0; i < count; ++i) {
        if (i > 0) {
            position[0] += steps[i].dx;
            position[1] += steps[i].dy;
            position[2] += steps[i].angle;
        }
        path[i] = position;
    }
    // Smoothed with a Gaussian of up to half a second each side (the ends are held, not pulled to zero).
    const double sigma = std::max(0.0, std::clamp(strength, 0.0, 1.0) * 0.5 * framesPerSecond);
    const int radius = static_cast<int>(std::ceil(sigma * 2.5));
    result.corrections.resize(count);
    double largest = 0.0;
    for (size_t i = 0; i < count; ++i) {
        std::array<double, 3> smooth{};
        double weight = 0.0;
        for (int k = -radius; k <= radius; ++k) {
            const auto j = static_cast<size_t>(std::clamp<long>(static_cast<long>(i) + k, 0, static_cast<long>(count) - 1));
            const double w = sigma > 0.0 ? std::exp(-0.5 * (k * k) / (sigma * sigma)) : (k == 0 ? 1.0 : 0.0);
            for (int c = 0; c < 3; ++c) {
                smooth[static_cast<size_t>(c)] += w * path[j][static_cast<size_t>(c)];
            }
            weight += w;
        }
        CameraStep &correction = result.corrections[i];
        correction.dx = static_cast<float>(smooth[0] / weight - path[i][0]);
        correction.dy = static_cast<float>(smooth[1] / weight - path[i][1]);
        // Rotation is measured but not corrected: on small frames its estimate is too noisy (about 0.4° per frame), and
        // its errors add up into a visible tilt.
        correction.angle = 0.0f;
        largest = std::max({largest, std::abs(double(correction.dx)), std::abs(double(correction.dy))});
    }
    // Enough enlargement to hide the borders the corrections uncover, at most 25 %: beyond, the corrections are
    // limited to what it can hide (a very shaky shot keeps a little shake rather than black edges).
    result.zoom = std::clamp(1.0 + 2.0 * largest, 1.0, 1.25);
    const double limit = (result.zoom - 1.0) / 2.0;
    for (CameraStep &correction : result.corrections) {
        correction.dx = static_cast<float>(std::clamp(double(correction.dx), -limit, limit));
        correction.dy = static_cast<float>(std::clamp(double(correction.dy), -limit, limit));
    }
    return result;
}

void applyStabilization(ImageView destination, ConstImageView source, const CameraStep &correction, double zoom)
{
    for (int y = 0; y < destination.height; ++y) {
        std::memset(destination.row(y), 0, static_cast<size_t>(destination.width) * 4);
    }
    const double cx = source.width / 2.0;
    const double cy = source.height / 2.0;
    // Source → destination: move by the correction, turn and enlarge around the centre.
    const Affine forward = Affine::translation(cx + correction.dx * source.width, cy + correction.dy * source.height) *
                           Affine::rotation(correction.angle) * Affine::scaling(zoom, zoom) * Affine::translation(-cx, -cy);
    drawAffine(destination, source, forward.inverted(), SourceWindow{0, 0, double(source.width), double(source.height)});
}

} // namespace vedit::fx
