// SPDX-License-Identifier: GPL-3.0-or-later
#include "Color.h"

#include <algorithm>
#include <cmath>

namespace velacut::fx {

namespace {

double clamp01(double v)
{
    return std::clamp(v, 0.0, 1.0);
}

double luma(const std::array<double, 3> &c)
{
    return 0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2]; // Rec.709
}

// Monotone piecewise-linear curve through the points (0,0) and (1,1) added when missing.
double curveAt(const std::vector<std::pair<double, double>> &points, double x)
{
    if (points.empty()) {
        return x;
    }
    double px = 0, py = 0;
    if (points.front().first > 0) {
        px = 0;
        py = 0;
    } else {
        px = points.front().first;
        py = points.front().second;
    }
    for (const auto &[qx, qy] : points) {
        if (x <= qx) {
            return qx > px ? py + (qy - py) * (x - px) / (qx - px) : qy;
        }
        px = qx;
        py = qy;
    }
    return px < 1 ? py + (1 - py) * (x - px) / (1 - px) : py;
}

inline std::uint8_t toByte(double v)
{
    return static_cast<std::uint8_t>(std::lround(clamp01(v) * 255.0));
}

} // namespace

bool ColorAdjust::isIdentity() const
{
    return *this == ColorAdjust{};
}

std::array<double, 3> ColorLut::evaluate(const ColorAdjust &a, std::array<double, 3> c)
{
    // Exposure in (approximately) linear light.
    if (a.exposure != 0) {
        const double gain = std::pow(2.0, a.exposure);
        for (double &v : c) {
            v = std::pow(std::pow(clamp01(v), 2.2) * gain, 1.0 / 2.2);
        }
    }
    // White balance as channel gains.
    if (a.temperature != 0 || a.tint != 0) {
        c[0] *= 1.0 + 0.18 * a.temperature;
        c[2] *= 1.0 - 0.18 * a.temperature;
        c[1] *= 1.0 - 0.12 * a.tint;
    }
    // Black and white points.
    if (a.whites != 0 || a.blacks != 0) {
        const double black = -0.08 * a.blacks;
        const double white = 1.0 - 0.08 * a.whites;
        for (double &v : c) {
            v = (v - black) / (white - black);
        }
    }
    // Midtones: a gamma that keeps black and white.
    if (a.brightness != 0) {
        const double gamma = std::pow(2.0, -a.brightness);
        for (double &v : c) {
            v = std::pow(clamp01(v), gamma);
        }
    }
    // Contrast around middle grey.
    if (a.contrast != 0) {
        const double k = a.contrast >= 0 ? 1.0 + a.contrast : 1.0 + 0.7 * a.contrast;
        for (double &v : c) {
            v = 0.5 + (v - 0.5) * k;
        }
    }
    // Shadows and highlights: the same offset on every channel (hue kept), weighted by luminance.
    if (a.shadows != 0 || a.highlights != 0) {
        const double y = clamp01(luma(c));
        const double shadowWeight = (1.0 - y) * (1.0 - y);
        const double highlightWeight = y * y;
        const double delta = 0.25 * (a.shadows * shadowWeight + a.highlights * highlightWeight);
        for (double &v : c) {
            v += delta;
        }
    }
    // Saturation and vibrance around the luminance.
    if (a.saturation != 0 || a.vibrance != 0) {
        const double y = luma(c);
        const double maxC = std::max({c[0], c[1], c[2]});
        const double minC = std::min({c[0], c[1], c[2]});
        const double currentSaturation = maxC > 1e-6 ? (maxC - minC) / maxC : 0.0;
        const double factor = 1.0 + a.saturation + a.vibrance * (1.0 - clamp01(currentSaturation));
        for (double &v : c) {
            v = y + (v - y) * std::max(0.0, factor);
        }
    }
    // Split toning.
    if (a.splitAmount != 0) {
        const double y = clamp01(luma(c));
        for (int i = 0; i < 3; ++i) {
            c[i] += a.splitAmount * 0.2 * (a.shadowTone[i] * (1.0 - y) + a.highlightTone[i] * y);
        }
    }
    // Tone curve.
    if (!a.curve.empty()) {
        for (double &v : c) {
            v = curveAt(a.curve, clamp01(v));
        }
    }
    // Fade: lifted blacks.
    if (a.fade != 0) {
        const double lift = 0.18 * clamp01(a.fade);
        for (double &v : c) {
            v = lift + clamp01(v) * (1.0 - lift);
        }
    }
    for (double &v : c) {
        v = clamp01(v);
    }
    return c;
}

ColorLut::ColorLut()
    : ColorLut(ColorAdjust{})
{
}

ColorLut::ColorLut(const ColorAdjust &adjust)
    : ColorLut([&adjust](const std::array<double, 3> &rgb) { return evaluate(adjust, rgb); })
{
}

ColorLut::ColorLut(const std::function<std::array<double, 3>(const std::array<double, 3> &)> &transform)
    : m_table(static_cast<size_t>(kSize * kSize * kSize))
{
    for (int r = 0; r < kSize; ++r) {
        for (int g = 0; g < kSize; ++g) {
            for (int b = 0; b < kSize; ++b) {
                const auto out = transform({r / double(kSize - 1), g / double(kSize - 1), b / double(kSize - 1)});
                m_table[static_cast<size_t>((r * kSize + g) * kSize + b)] = {float(out[0]), float(out[1]), float(out[2])};
            }
        }
    }
}

void ColorLut::apply(ImageView image, double intensity, int rowBegin, int rowEnd) const
{
    const float mix = static_cast<float>(std::clamp(intensity, 0.0, 1.0));
    if (mix <= 0.0f) {
        return;
    }
    constexpr float scale = (kSize - 1) / 255.0f;
    const auto at = [this](int r, int g, int b) -> const std::array<float, 3> & {
        return m_table[static_cast<size_t>((r * kSize + g) * kSize + b)];
    };
    for (int y = std::max(0, rowBegin); y < std::min(rowEnd, image.height); ++y) {
        std::uint8_t *p = image.row(y);
        for (int x = 0; x < image.width; ++x, p += 4) {
            if (p[3] == 0) {
                continue;
            }
            const float fr = p[0] * scale, fg = p[1] * scale, fb = p[2] * scale;
            const int r0 = std::min(static_cast<int>(fr), kSize - 2);
            const int g0 = std::min(static_cast<int>(fg), kSize - 2);
            const int b0 = std::min(static_cast<int>(fb), kSize - 2);
            const float tr = fr - r0, tg = fg - g0, tb = fb - b0;
            for (int c = 0; c < 3; ++c) {
                const float c00 = at(r0, g0, b0)[c] * (1 - tb) + at(r0, g0, b0 + 1)[c] * tb;
                const float c01 = at(r0, g0 + 1, b0)[c] * (1 - tb) + at(r0, g0 + 1, b0 + 1)[c] * tb;
                const float c10 = at(r0 + 1, g0, b0)[c] * (1 - tb) + at(r0 + 1, g0, b0 + 1)[c] * tb;
                const float c11 = at(r0 + 1, g0 + 1, b0)[c] * (1 - tb) + at(r0 + 1, g0 + 1, b0 + 1)[c] * tb;
                const float v = (c00 * (1 - tg) + c01 * tg) * (1 - tr) + (c10 * (1 - tg) + c11 * tg) * tr;
                const float original = p[c] / 255.0f;
                p[c] = toByte(original + (v - original) * mix);
            }
        }
    }
}

void vignette(ImageView image, double amount, double size, int rowBegin, int rowEnd)
{
    if (amount == 0) {
        return;
    }
    const double cx = image.width / 2.0;
    const double cy = image.height / 2.0;
    const double inner = std::clamp(1.0 - size, 0.0, 0.95); // radius (0 centre … 1 corner) where it starts
    for (int y = std::max(0, rowBegin); y < std::min(rowEnd, image.height); ++y) {
        std::uint8_t *p = image.row(y);
        const double dy = (y + 0.5 - cy) / cy;
        for (int x = 0; x < image.width; ++x, p += 4) {
            const double dx = (x + 0.5 - cx) / cx;
            const double r = std::sqrt((dx * dx + dy * dy) / 2.0); // 1 at the corners
            double t = std::clamp((r - inner) / (1.0 - inner), 0.0, 1.0);
            t = t * t * (3 - 2 * t); // smoothstep
            for (int c = 0; c < 3; ++c) {
                const double v = p[c] / 255.0;
                p[c] = toByte(amount < 0 ? v * (1.0 + amount * t) : v + (1.0 - v) * amount * t);
            }
        }
    }
}

void grain(ImageView image, double amount, std::uint32_t seed, int rowBegin, int rowEnd)
{
    if (amount <= 0) {
        return;
    }
    for (int y = std::max(0, rowBegin); y < std::min(rowEnd, image.height); ++y) {
        std::uint8_t *p = image.row(y);
        for (int x = 0; x < image.width; ++x, p += 4) {
            // Integer hash of (x, y, seed): the same frame always gets the same grain.
            std::uint32_t h = static_cast<std::uint32_t>(x) * 374761393u + static_cast<std::uint32_t>(y) * 668265263u + seed * 2246822519u;
            h = (h ^ (h >> 13)) * 1274126177u;
            h ^= h >> 16;
            const double noise = (h & 0xffff) / 65535.0 - 0.5; // −0.5…0.5
            const double delta = noise * amount * 0.25;
            for (int c = 0; c < 3; ++c) {
                p[c] = toByte(p[c] / 255.0 + delta);
            }
        }
    }
}

void sharpen(ImageView destination, ConstImageView source, double amount, int rowBegin, int rowEnd)
{
    const int w = std::min(destination.width, source.width);
    const int h = std::min(destination.height, source.height);
    for (int y = std::max(0, rowBegin); y < std::min(rowEnd, h); ++y) {
        const std::uint8_t *above = source.row(std::max(0, y - 1));
        const std::uint8_t *here = source.row(y);
        const std::uint8_t *below = source.row(std::min(h - 1, y + 1));
        std::uint8_t *d = destination.row(y);
        for (int x = 0; x < w; ++x) {
            const int l = std::max(0, x - 1) * 4;
            const int r = std::min(w - 1, x + 1) * 4;
            const int m = x * 4;
            for (int c = 0; c < 4; ++c) {
                if (c == 3) {
                    d[m + 3] = here[m + 3];
                    continue;
                }
                const double blur = (above[m + c] + below[m + c] + here[l + c] + here[r + c] + 4.0 * here[m + c]) / 8.0;
                d[m + c] = toByte((here[m + c] + amount * (here[m + c] - blur)) / 255.0);
            }
        }
    }
}

namespace {

// One pixel of the noise reduction from its neighbours `at(k)`, k = −radius…radius.
template<typename Neighbour>
inline void denoisePixel(const std::uint8_t *centre, std::uint8_t *out, int radius, const std::array<int, 256> &weightOf, Neighbour at)
{
    int sum0 = centre[0] * 256, sum1 = centre[1] * 256, sum2 = centre[2] * 256;
    int weights = 256;
    for (int k = -radius; k <= radius; ++k) {
        if (k == 0) {
            continue;
        }
        const std::uint8_t *p = at(k);
        const int difference = std::max({std::abs(p[0] - centre[0]), std::abs(p[1] - centre[1]), std::abs(p[2] - centre[2])});
        const int weight = weightOf[static_cast<size_t>(difference)];
        sum0 += weight * p[0];
        sum1 += weight * p[1];
        sum2 += weight * p[2];
        weights += weight;
    }
    const int half = weights / 2;
    out[0] = static_cast<std::uint8_t>((sum0 + half) / weights);
    out[1] = static_cast<std::uint8_t>((sum1 + half) / weights);
    out[2] = static_cast<std::uint8_t>((sum2 + half) / weights);
    out[3] = centre[3];
}

} // namespace

void denoisePass(ImageView destination, ConstImageView source, double amount, int radius, bool vertical, int rowBegin, int rowEnd)
{
    const int w = std::min(destination.width, source.width);
    const int h = std::min(destination.height, source.height);
    radius = std::clamp(radius, 1, 8);
    // Weight of a neighbour by its difference from the pixel, in 1/256: 1 − difference / threshold.
    const double threshold = 6.0 + 34.0 * std::clamp(amount, 0.0, 1.0);
    std::array<int, 256> weightOf{};
    for (int d = 0; d < 256; ++d) {
        weightOf[static_cast<size_t>(d)] = std::max(0, static_cast<int>(std::lround(256.0 * (1.0 - d / threshold))));
    }
    std::array<const std::uint8_t *, 17> rows{};
    for (int y = std::max(0, rowBegin); y < std::min(rowEnd, h); ++y) {
        const std::uint8_t *here = source.row(y);
        std::uint8_t *d = destination.row(y);
        if (vertical) {
            for (int k = -radius; k <= radius; ++k) {
                rows[static_cast<size_t>(k + radius)] = source.row(std::clamp(y + k, 0, h - 1));
            }
            for (int x = 0; x < w; ++x) {
                denoisePixel(here + x * 4, d + x * 4, radius, weightOf,
                             [&rows, radius, x](int k) { return rows[static_cast<size_t>(k + radius)] + x * 4; });
            }
            continue;
        }
        for (int x = 0; x < w; ++x) {
            if (x >= radius && x + radius < w) {
                denoisePixel(here + x * 4, d + x * 4, radius, weightOf, [here, x](int k) { return here + (x + k) * 4; });
            } else {
                denoisePixel(here + x * 4, d + x * 4, radius, weightOf,
                             [here, x, w](int k) { return here + std::clamp(x + k, 0, w - 1) * 4; });
            }
        }
    }
}

int denoiseRadius(int height, double amount)
{
    return std::clamp(static_cast<int>(std::lround(height / 540.0 * (1.0 + std::clamp(amount, 0.0, 1.0)))), 1, 8);
}

void boxBlur(ImageView image, int radius)
{
    if (radius <= 0 || image.width <= 0 || image.height <= 0) {
        return;
    }
    std::vector<std::uint32_t> line(static_cast<size_t>(std::max(image.width, image.height)) * 4);
    const auto pass = [&](int count, int length, auto pixel) {
        for (int i = 0; i < count; ++i) {
            for (int j = 0; j < length; ++j) {
                const std::uint8_t *p = pixel(i, j);
                for (int c = 0; c < 4; ++c) {
                    line[static_cast<size_t>(j * 4 + c)] = p[c];
                }
            }
            // Sliding window with clamped edges.
            for (int c = 0; c < 4; ++c) {
                std::uint32_t sum = 0;
                for (int k = -radius; k <= radius; ++k) {
                    sum += line[static_cast<size_t>(std::clamp(k, 0, length - 1) * 4 + c)];
                }
                const std::uint32_t window = 2 * radius + 1;
                for (int j = 0; j < length; ++j) {
                    pixel(i, j)[c] = static_cast<std::uint8_t>((sum + window / 2) / window);
                    const int out = std::clamp(j - radius, 0, length - 1);
                    const int in = std::clamp(j + radius + 1, 0, length - 1);
                    sum += line[static_cast<size_t>(in * 4 + c)];
                    sum -= line[static_cast<size_t>(out * 4 + c)];
                }
            }
        }
    };
    for (int iteration = 0; iteration < 3; ++iteration) {
        pass(image.height, image.width, [&](int y, int x) { return image.row(y) + x * 4; });
        pass(image.width, image.height, [&](int x, int y) { return image.row(y) + x * 4; });
    }
}

} // namespace velacut::fx
