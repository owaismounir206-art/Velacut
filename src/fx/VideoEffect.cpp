// SPDX-License-Identifier: GPL-3.0-or-later
#include "VideoEffect.h"

#include "fx/MotionBlur.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <vector>

namespace vedit::fx {

namespace {

constexpr double kPi = 3.14159265358979323846;

struct Name
{
    EffectKernel kernel;
    const char *name;
};

constexpr std::array kNames{
    Name{EffectKernel::Blur, "blur"},
    Name{EffectKernel::ZoomBlur, "zoomBlur"},
    Name{EffectKernel::SpinBlur, "spinBlur"},
    Name{EffectKernel::DirectionalBlur, "directionalBlur"},
    Name{EffectKernel::Glow, "glow"},
    Name{EffectKernel::Dreamy, "dreamy"},
    Name{EffectKernel::RgbSplit, "rgbSplit"},
    Name{EffectKernel::Glitch, "glitch"},
    Name{EffectKernel::BlockGlitch, "blockGlitch"},
    Name{EffectKernel::Scanlines, "scanlines"},
    Name{EffectKernel::Vhs, "vhs"},
    Name{EffectKernel::Noise, "noise"},
    Name{EffectKernel::Pixelate, "pixelate"},
    Name{EffectKernel::Mirror, "mirror"},
    Name{EffectKernel::Kaleidoscope, "kaleidoscope"},
    Name{EffectKernel::Shake, "shake"},
    Name{EffectKernel::ZoomPulse, "zoomPulse"},
    Name{EffectKernel::Strobe, "strobe"},
    Name{EffectKernel::Invert, "invert"},
    Name{EffectKernel::Posterize, "posterize"},
    Name{EffectKernel::Edges, "edges"},
    Name{EffectKernel::Sketch, "sketch"},
    Name{EffectKernel::Emboss, "emboss"},
    Name{EffectKernel::PulseVignette, "pulseVignette"},
    Name{EffectKernel::HueCycle, "hueCycle"},
    Name{EffectKernel::Duotone, "duotone"},
    Name{EffectKernel::Thermal, "thermal"},
    Name{EffectKernel::NightVision, "nightVision"},
    Name{EffectKernel::OldFilm, "oldFilm"},
    Name{EffectKernel::LightLeak, "lightLeak"},
    Name{EffectKernel::Rain, "rain"},
    Name{EffectKernel::Snow, "snow"},
    Name{EffectKernel::Sparkles, "sparkles"},
    Name{EffectKernel::Bokeh, "bokeh"},
    Name{EffectKernel::Wave, "wave"},
    Name{EffectKernel::Swirl, "swirl"},
    Name{EffectKernel::Bulge, "bulge"},
    Name{EffectKernel::Grid, "grid"},
    Name{EffectKernel::Letterbox, "letterbox"},
    Name{EffectKernel::Halftone, "halftone"},
    Name{EffectKernel::Dither, "dither"},
    Name{EffectKernel::NeonEdges, "neonEdges"},
    Name{EffectKernel::TiltShift, "tiltShift"},
    Name{EffectKernel::Prism, "prism"},
    Name{EffectKernel::LensAberration, "lensAberration"},
    Name{EffectKernel::Spotlight, "spotlight"},
    Name{EffectKernel::Flicker, "flicker"},
    Name{EffectKernel::ColorShift, "colorShift"},
};

// ---- helpers -------------------------------------------------------------------------------------------------------

std::uint32_t hash(std::uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

// Uniform 0–1 from integers (deterministic).
double random01(std::uint32_t a, std::uint32_t b = 0, std::uint32_t c = 0)
{
    return hash(a * 0x9e3779b9U ^ hash(b + 0x632be5abU) ^ hash(c * 0x85ebca6bU + 1U)) / 4294967295.0;
}

std::uint8_t toByte(double value)
{
    return static_cast<std::uint8_t>(std::clamp(std::lround(value), 0L, 255L));
}

double luma(const std::uint8_t *p)
{
    return (0.2126 * p[0] + 0.7152 * p[1] + 0.0722 * p[2]) / 255.0;
}

// A packed copy of an image (stride = width × 4), the source of the kernels that move pixels.
struct Copy
{
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> pixels;

    explicit Copy(const ImageView &image)
        : width(image.width)
        , height(image.height)
        , pixels(static_cast<size_t>(image.width) * image.height * 4)
    {
        for (int y = 0; y < height; ++y) {
            std::memcpy(pixels.data() + static_cast<size_t>(y) * width * 4, image.row(y), static_cast<size_t>(width) * 4);
        }
    }
    const std::uint8_t *at(int x, int y) const
    {
        x = std::clamp(x, 0, width - 1);
        y = std::clamp(y, 0, height - 1);
        return pixels.data() + (static_cast<size_t>(y) * width + x) * 4;
    }
    // Bilinear, clamped at the borders.
    std::array<double, 4> sample(double x, double y) const
    {
        x = std::clamp(x, 0.0, width - 1.0);
        y = std::clamp(y, 0.0, height - 1.0);
        const int x0 = static_cast<int>(x);
        const int y0 = static_cast<int>(y);
        const double fx = x - x0;
        const double fy = y - y0;
        const std::uint8_t *a = at(x0, y0);
        const std::uint8_t *b = at(x0 + 1, y0);
        const std::uint8_t *c = at(x0, y0 + 1);
        const std::uint8_t *d = at(x0 + 1, y0 + 1);
        std::array<double, 4> out{};
        for (int i = 0; i < 4; ++i) {
            out[static_cast<size_t>(i)] = (a[i] * (1 - fx) + b[i] * fx) * (1 - fy) + (c[i] * (1 - fx) + d[i] * fx) * fy;
        }
        return out;
    }
    ImageView view() { return {pixels.data(), width, height, width * 4}; }
};

void put(std::uint8_t *p, const std::array<double, 4> &value)
{
    for (int i = 0; i < 4; ++i) {
        p[i] = toByte(value[static_cast<size_t>(i)]);
    }
}

// Every pixel from a position in the source: `map(x, y) → (sx, sy)`.
template<typename Map>
void remap(const ImageView &image, Map map)
{
    const Copy source(image);
    for (int y = 0; y < image.height; ++y) {
        std::uint8_t *row = image.row(y);
        for (int x = 0; x < image.width; ++x) {
            double sx = x;
            double sy = y;
            map(static_cast<double>(x), static_cast<double>(y), sx, sy);
            put(row + x * 4, source.sample(sx, sy));
        }
    }
}

// Box blur of radius `radius` (pixels), three passes each way: close to a Gaussian.
void blur(const ImageView &image, int radius)
{
    if (radius < 1 || image.width < 2 || image.height < 2) {
        return;
    }
    const int w = image.width;
    const int h = image.height;
    std::vector<int> line(static_cast<size_t>(std::max(w, h)) * 4);
    const auto pass = [&](int count, int step, auto pixel) {
        for (int i = 0; i < count * 4; ++i) {
            line[static_cast<size_t>(i)] = pixel(i / 4)[i % 4];
        }
        const int window = 2 * radius + 1;
        for (int c = 0; c < 4; ++c) {
            long sum = 0;
            for (int k = -radius; k <= radius; ++k) {
                sum += line[static_cast<size_t>(std::clamp(k, 0, count - 1)) * 4 + c];
            }
            for (int i = 0; i < count; ++i) {
                pixel(i)[c] = static_cast<std::uint8_t>(sum / window);
                sum += line[static_cast<size_t>(std::min(i + radius + 1, count - 1)) * 4 + c] -
                       line[static_cast<size_t>(std::max(i - radius, 0)) * 4 + c];
            }
        }
        (void)step;
    };
    for (int iteration = 0; iteration < 3; ++iteration) {
        for (int y = 0; y < h; ++y) {
            std::uint8_t *row = image.row(y);
            pass(w, 4, [row](int i) { return row + i * 4; });
        }
        for (int x = 0; x < w; ++x) {
            pass(h, image.stride, [&image, x](int i) { return image.row(i) + x * 4; });
        }
    }
}

// Mixes `effect` over `original` (same size): 0 = original, 1 = effect.
void mixInto(const ImageView &effect, const Copy &original, double amount)
{
    amount = std::clamp(amount, 0.0, 1.0);
    for (int y = 0; y < effect.height; ++y) {
        std::uint8_t *row = effect.row(y);
        for (int x = 0; x < effect.width; ++x) {
            const std::uint8_t *o = original.at(x, y);
            for (int c = 0; c < 3; ++c) {
                row[x * 4 + c] = toByte(o[c] + (row[x * 4 + c] - o[c]) * amount);
            }
        }
    }
}

template<typename Pixel>
void forEachPixel(const ImageView &image, Pixel pixel)
{
    for (int y = 0; y < image.height; ++y) {
        std::uint8_t *row = image.row(y);
        for (int x = 0; x < image.width; ++x) {
            pixel(x, y, row + x * 4);
        }
    }
}

// Adds a soft round spot of `color` (additive, `strength` 0–1).
void addSpot(const ImageView &image, double cx, double cy, double radius, const EffectRgb &color, double strength,
             bool hard = false)
{
    const int x0 = std::max(0, static_cast<int>(cx - radius));
    const int x1 = std::min(image.width - 1, static_cast<int>(cx + radius));
    const int y0 = std::max(0, static_cast<int>(cy - radius));
    const int y1 = std::min(image.height - 1, static_cast<int>(cy + radius));
    for (int y = y0; y <= y1; ++y) {
        std::uint8_t *row = image.row(y);
        for (int x = x0; x <= x1; ++x) {
            const double d = std::hypot(x - cx, y - cy) / std::max(radius, 0.5);
            if (d >= 1.0) {
                continue;
            }
            const double a = strength * (hard ? std::clamp((1.0 - d) * 4.0, 0.0, 1.0) : (1.0 - d) * (1.0 - d));
            std::uint8_t *p = row + x * 4;
            p[0] = toByte(p[0] + color.r * 255.0 * a);
            p[1] = toByte(p[1] + color.g * 255.0 * a);
            p[2] = toByte(p[2] + color.b * 255.0 * a);
        }
    }
}

void edgeMagnitude(const ImageView &image, std::vector<double> &out)
{
    const Copy source(image);
    out.assign(static_cast<size_t>(image.width) * image.height, 0.0);
    for (int y = 0; y < image.height; ++y) {
        for (int x = 0; x < image.width; ++x) {
            const auto l = [&](int dx, int dy) { return luma(source.at(x + dx, y + dy)); };
            const double gx = -l(-1, -1) - 2 * l(-1, 0) - l(-1, 1) + l(1, -1) + 2 * l(1, 0) + l(1, 1);
            const double gy = -l(-1, -1) - 2 * l(0, -1) - l(1, -1) + l(-1, 1) + 2 * l(0, 1) + l(1, 1);
            out[static_cast<size_t>(y) * image.width + x] = std::min(1.0, std::hypot(gx, gy));
        }
    }
}

void vignette(const ImageView &image, double strength, const EffectRgb &color = {0, 0, 0})
{
    const double cx = image.width / 2.0;
    const double cy = image.height / 2.0;
    const double reach = std::hypot(cx, cy);
    forEachPixel(image, [&](int x, int y, std::uint8_t *p) {
        const double d = std::hypot(x - cx, y - cy) / reach;
        const double a = std::clamp(strength * std::pow(std::max(0.0, d - 0.35) / 0.65, 1.5), 0.0, 1.0);
        p[0] = toByte(p[0] * (1 - a) + color.r * 255 * a);
        p[1] = toByte(p[1] * (1 - a) + color.g * 255 * a);
        p[2] = toByte(p[2] * (1 - a) + color.b * 255 * a);
    });
}

// Smooth pseudo-random value in -1…1 changing with `t` (for shakes and wobbles).
double smoothNoise(double t, std::uint32_t seed)
{
    const double i = std::floor(t);
    const double f = t - i;
    const double a = random01(static_cast<std::uint32_t>(static_cast<long long>(i)), seed) * 2 - 1;
    const double b = random01(static_cast<std::uint32_t>(static_cast<long long>(i) + 1), seed) * 2 - 1;
    const double s = f * f * (3 - 2 * f);
    return a + (b - a) * s;
}

// ---- kernels -------------------------------------------------------------------------------------------------------

void radialBlur(const ImageView &image, double zoom, double spin)
{
    const Copy source(image);
    const double cx = image.width / 2.0;
    const double cy = image.height / 2.0;
    constexpr int kTaps = 12;
    for (int y = 0; y < image.height; ++y) {
        std::uint8_t *row = image.row(y);
        for (int x = 0; x < image.width; ++x) {
            std::array<double, 4> sum{};
            for (int k = 0; k < kTaps; ++k) {
                const double t = static_cast<double>(k) / (kTaps - 1) - 0.5;
                const double scale = 1.0 - zoom * t;
                const double angle = spin * t;
                const double dx = (x - cx) * scale;
                const double dy = (y - cy) * scale;
                const auto s = source.sample(cx + dx * std::cos(angle) - dy * std::sin(angle),
                                             cy + dx * std::sin(angle) + dy * std::cos(angle));
                for (size_t c = 0; c < 4; ++c) {
                    sum[c] += s[c] / kTaps;
                }
            }
            put(row + x * 4, sum);
        }
    }
}

void glow(const ImageView &image, double amount, double threshold)
{
    Copy bright(image);
    ImageView view = bright.view();
    forEachPixel(view, [&](int, int, std::uint8_t *p) {
        const double keep = std::clamp((luma(p) - threshold) / std::max(0.05, 1.0 - threshold), 0.0, 1.0);
        p[0] = toByte(p[0] * keep);
        p[1] = toByte(p[1] * keep);
        p[2] = toByte(p[2] * keep);
    });
    blur(view, std::max(2, image.height / 40));
    forEachPixel(image, [&](int x, int y, std::uint8_t *p) {
        const std::uint8_t *g = bright.at(x, y);
        for (int c = 0; c < 3; ++c) {
            // Screen blend: bright parts bloom, nothing goes beyond white.
            const double a = p[c] / 255.0;
            const double b = g[c] / 255.0 * amount * 1.5;
            p[c] = toByte((1 - (1 - a) * (1 - std::min(1.0, b))) * 255);
        }
    });
}

void rgbSplit(const ImageView &image, double offset, double angle)
{
    const Copy source(image);
    const double dx = offset * std::cos(angle * kPi / 180);
    const double dy = offset * std::sin(angle * kPi / 180);
    forEachPixel(image, [&](int x, int y, std::uint8_t *p) {
        p[0] = toByte(source.sample(x - dx, y - dy)[0]);
        p[2] = toByte(source.sample(x + dx, y + dy)[2]);
    });
}

void glitch(const ImageView &image, const VideoEffectParams &p, bool blocks)
{
    const Copy source(image);
    const auto tick = static_cast<std::uint32_t>(std::floor(p.time * std::max(0.1, p.speed) * 10));
    const int w = image.width;
    const int h = image.height;
    if (!blocks) {
        // Horizontal slices shifted sideways, some with their colours split.
        int y = 0;
        std::uint32_t slice = 0;
        while (y < h) {
            const int height = std::max(1, static_cast<int>(h * (0.01 + random01(tick, slice, 1) * 0.08)));
            const bool active = random01(tick, slice, 2) < p.amount * 0.6;
            const double shift = active ? (random01(tick, slice, 3) - 0.5) * w * 0.2 * p.amount : 0.0;
            const double split = active && random01(tick, slice, 4) < 0.5 ? h * 0.01 * p.amount : 0.0;
            for (int yy = y; yy < std::min(h, y + height); ++yy) {
                std::uint8_t *row = image.row(yy);
                for (int x = 0; x < w; ++x) {
                    const auto c = source.sample(x - shift, yy);
                    row[x * 4] = toByte(source.sample(x - shift - split, yy)[0]);
                    row[x * 4 + 1] = toByte(c[1]);
                    row[x * 4 + 2] = toByte(source.sample(x - shift + split, yy)[2]);
                }
            }
            y += height;
            ++slice;
        }
        return;
    }
    // Blocks copied from elsewhere, with channels swapped.
    const int blocksCount = static_cast<int>(4 + p.amount * 30);
    for (int b = 0; b < blocksCount; ++b) {
        const int bw = std::max(2, static_cast<int>(w * (0.03 + random01(tick, b, 1) * 0.2)));
        const int bh = std::max(2, static_cast<int>(h * (0.02 + random01(tick, b, 2) * 0.1)));
        const int x0 = static_cast<int>(random01(tick, b, 3) * (w - bw));
        const int y0 = static_cast<int>(random01(tick, b, 4) * (h - bh));
        const int sx = static_cast<int>(random01(tick, b, 5) * (w - bw));
        const int sy = static_cast<int>(random01(tick, b, 6) * (h - bh));
        const int swap = static_cast<int>(random01(tick, b, 7) * 3);
        for (int y = 0; y < bh; ++y) {
            std::uint8_t *row = image.row(y0 + y);
            for (int x = 0; x < bw; ++x) {
                const std::uint8_t *s = source.at(sx + x, sy + y);
                std::uint8_t *d = row + (x0 + x) * 4;
                d[0] = s[(0 + swap) % 3];
                d[1] = s[(1 + swap) % 3];
                d[2] = s[(2 + swap) % 3];
            }
        }
    }
}

void scanlines(const ImageView &image, double amount, double size)
{
    const double period = std::max(2.0, image.height * (0.004 + size * 0.02));
    forEachPixel(image, [&](int, int y, std::uint8_t *p) {
        const double phase = std::fmod(y, period) / period;
        const double dark = amount * 0.6 * (0.5 + 0.5 * std::cos(phase * 2 * kPi));
        for (int c = 0; c < 3; ++c) {
            p[c] = toByte(p[c] * (1 - dark));
        }
    });
}

void noise(const ImageView &image, double amount, double speed, double time)
{
    const auto tick = static_cast<std::uint32_t>(std::floor(time * std::max(1.0, speed * 24)));
    forEachPixel(image, [&](int x, int y, std::uint8_t *p) {
        const double n = (random01(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y), tick) - 0.5) * 255 * amount * 0.6;
        for (int c = 0; c < 3; ++c) {
            p[c] = toByte(p[c] + n);
        }
    });
}

void vhs(const ImageView &image, const VideoEffectParams &p)
{
    const Copy source(image);
    const int w = image.width;
    const int h = image.height;
    const double bleed = w * 0.006 * p.amount;
    const double band = std::fmod(p.time * 0.15 * std::max(0.1, p.speed), 1.2) - 0.1; // tracking band position
    for (int y = 0; y < h; ++y) {
        const double v = static_cast<double>(y) / h;
        const double wobble = std::sin(v * 40 + p.time * 6) * w * 0.002 * p.amount +
                              (std::abs(v - band) < 0.03 ? (random01(static_cast<std::uint32_t>(y), 7,
                                                                     static_cast<std::uint32_t>(p.time * 30)) - 0.5) * w * 0.05 * p.amount
                                                         : 0.0);
        std::uint8_t *row = image.row(y);
        for (int x = 0; x < w; ++x) {
            const double sx = x + wobble;
            row[x * 4] = toByte(source.sample(sx + bleed, y)[0]);
            row[x * 4 + 1] = toByte(source.sample(sx, y)[1]);
            row[x * 4 + 2] = toByte(source.sample(sx - bleed, y)[2]);
        }
    }
    scanlines(image, p.amount * 0.4, 0.1);
    noise(image, p.amount * 0.35, 1.0, p.time);
    // Washed-out colours of the tape.
    forEachPixel(image, [&](int, int, std::uint8_t *px) {
        const double l = luma(px) * 255;
        for (int c = 0; c < 3; ++c) {
            px[c] = toByte(px[c] + (l - px[c]) * 0.25 * p.amount + 8 * p.amount);
        }
    });
}

void pixelate(const ImageView &image, double size)
{
    const int block = std::max(2, static_cast<int>(std::lround(image.height * (0.005 + size * 0.06))));
    for (int by = 0; by < image.height; by += block) {
        for (int bx = 0; bx < image.width; bx += block) {
            std::array<long, 4> sum{};
            int n = 0;
            for (int y = by; y < std::min(image.height, by + block); ++y) {
                for (int x = bx; x < std::min(image.width, bx + block); ++x) {
                    const std::uint8_t *p = image.row(y) + x * 4;
                    for (size_t c = 0; c < 4; ++c) {
                        sum[c] += p[c];
                    }
                    ++n;
                }
            }
            for (int y = by; y < std::min(image.height, by + block); ++y) {
                for (int x = bx; x < std::min(image.width, bx + block); ++x) {
                    std::uint8_t *p = image.row(y) + x * 4;
                    for (size_t c = 0; c < 4; ++c) {
                        p[c] = static_cast<std::uint8_t>(sum[c] / n);
                    }
                }
            }
        }
    }
}

void thermalColor(double t, std::uint8_t *p)
{
    // black → blue → magenta → red → yellow → white
    static constexpr std::array<std::array<double, 3>, 6> stops{{{0, 0, 0}, {0, 0, 0.8}, {0.7, 0, 0.7}, {1, 0.1, 0}, {1, 0.9, 0}, {1, 1, 1}}};
    const double x = std::clamp(t, 0.0, 1.0) * (stops.size() - 1);
    const auto i = std::min(static_cast<size_t>(x), stops.size() - 2);
    const double f = x - static_cast<double>(i);
    for (size_t c = 0; c < 3; ++c) {
        p[c] = toByte((stops[i][c] * (1 - f) + stops[i + 1][c] * f) * 255);
    }
}

void particles(const ImageView &image, const VideoEffectParams &p, EffectKernel kernel)
{
    const double h = image.height;
    const double w = image.width;
    const int count = static_cast<int>(10 + p.amount * (kernel == EffectKernel::Rain ? 400 : kernel == EffectKernel::Bokeh ? 30 : 160));
    const double speed = std::max(0.05, p.speed);
    for (int i = 0; i < count; ++i) {
        const double r1 = random01(static_cast<std::uint32_t>(i), 11);
        const double r2 = random01(static_cast<std::uint32_t>(i), 12);
        const double r3 = random01(static_cast<std::uint32_t>(i), 13);
        switch (kernel) {
        case EffectKernel::Rain: {
            // Thin streaks falling fast, slightly slanted.
            const double y = std::fmod(r2 * h + p.time * speed * h * (1.2 + r3), h * 1.1) - h * 0.05;
            const double x = std::fmod(r1 * w + y * 0.15, w);
            const double length = h * (0.03 + r3 * 0.04);
            for (int k = 0; k < static_cast<int>(length); ++k) {
                const int px = static_cast<int>(x - k * 0.15);
                const int py = static_cast<int>(y - k);
                if (px >= 0 && px < image.width && py >= 0 && py < image.height) {
                    std::uint8_t *q = image.row(py) + px * 4;
                    for (int c = 0; c < 3; ++c) {
                        q[c] = toByte(q[c] * 0.6 + 200 * 0.4);
                    }
                }
            }
            break;
        }
        case EffectKernel::Snow: {
            const double y = std::fmod(r2 * h + p.time * speed * h * (0.08 + r3 * 0.12), h * 1.1) - h * 0.05;
            const double x = std::fmod(r1 * w + std::sin(p.time * (0.5 + r3) + i) * w * 0.02 + w, w);
            addSpot(image, x, y, h * (0.003 + r3 * 0.008), {1, 1, 1}, 0.9, true);
            break;
        }
        case EffectKernel::Sparkles: {
            const double phase = std::fmod(p.time * speed * (0.5 + r3) + r2, 1.0);
            const double twinkle = std::sin(phase * kPi);
            const double size = h * (0.004 + r3 * 0.01) * twinkle;
            const double x = r1 * w;
            const double y = r2 * h;
            addSpot(image, x, y, size * 2.5, p.color, 0.8 * twinkle);
            // A small cross of light.
            for (int k = -static_cast<int>(size * 4); k <= static_cast<int>(size * 4); ++k) {
                const double a = 0.7 * twinkle * (1.0 - std::abs(k) / (size * 4 + 1));
                for (const auto &[px, py] : {std::pair{static_cast<int>(x) + k, static_cast<int>(y)},
                                             std::pair{static_cast<int>(x), static_cast<int>(y) + k}}) {
                    if (px >= 0 && px < image.width && py >= 0 && py < image.height) {
                        std::uint8_t *q = image.row(py) + px * 4;
                        q[0] = toByte(q[0] + p.color.r * 255 * a);
                        q[1] = toByte(q[1] + p.color.g * 255 * a);
                        q[2] = toByte(q[2] + p.color.b * 255 * a);
                    }
                }
            }
            break;
        }
        default: { // Bokeh: large soft discs drifting upwards.
            const double y = std::fmod(r2 * h - p.time * speed * h * 0.05 * (0.5 + r3) + h * 10, h * 1.2) - h * 0.1;
            const double x = r1 * w + std::sin(p.time * 0.3 + i) * w * 0.03;
            addSpot(image, x, y, h * (0.04 + r3 * 0.08), p.color, 0.25 + 0.2 * r1);
            break;
        }
        }
    }
}

void halftone(const ImageView &image, double size, const EffectRgb &ink)
{
    const Copy source(image);
    const double cell = std::max(3.0, image.height * (0.008 + size * 0.03));
    forEachPixel(image, [&](int x, int y, std::uint8_t *p) {
        const double cx = (std::floor(x / cell) + 0.5) * cell;
        const double cy = (std::floor(y / cell) + 0.5) * cell;
        const double darkness = 1.0 - luma(source.at(static_cast<int>(cx), static_cast<int>(cy)));
        const double radius = std::sqrt(darkness) * cell * 0.7;
        const double inside = std::clamp(radius - std::hypot(x + 0.5 - cx, y + 0.5 - cy) + 0.5, 0.0, 1.0);
        p[0] = toByte(255 * (1 - inside) + ink.r * 255 * inside);
        p[1] = toByte(255 * (1 - inside) + ink.g * 255 * inside);
        p[2] = toByte(255 * (1 - inside) + ink.b * 255 * inside);
    });
}

void dither(const ImageView &image, double size, int levels)
{
    pixelate(image, size * 0.3);
    static constexpr std::array<int, 16> bayer{0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
    const int steps = std::clamp(levels, 2, 8) - 1;
    const int cell = std::max(1, static_cast<int>(std::lround(image.height * (0.005 + size * 0.06 * 0.3))));
    forEachPixel(image, [&](int x, int y, std::uint8_t *p) {
        const double threshold = (bayer[static_cast<size_t>((y / cell % 4) * 4 + (x / cell % 4))] + 0.5) / 16.0 - 0.5;
        for (int c = 0; c < 3; ++c) {
            const double v = p[c] / 255.0 * steps + threshold;
            p[c] = toByte(std::clamp(std::round(v), 0.0, static_cast<double>(steps)) / steps * 255);
        }
    });
}

} // namespace

std::optional<EffectKernel> effectKernel(const QString &name)
{
    for (const Name &entry : kNames) {
        if (name == QLatin1StringView(entry.name)) {
            return entry.kernel;
        }
    }
    return std::nullopt;
}

QString effectKernelName(EffectKernel kernel)
{
    for (const Name &entry : kNames) {
        if (entry.kernel == kernel) {
            return QString::fromLatin1(entry.name);
        }
    }
    return {};
}

bool isAnimatedKernel(EffectKernel kernel)
{
    switch (kernel) {
    case EffectKernel::Glitch:
    case EffectKernel::BlockGlitch:
    case EffectKernel::Vhs:
    case EffectKernel::Noise:
    case EffectKernel::Kaleidoscope:
    case EffectKernel::Shake:
    case EffectKernel::ZoomPulse:
    case EffectKernel::Strobe:
    case EffectKernel::PulseVignette:
    case EffectKernel::HueCycle:
    case EffectKernel::NightVision:
    case EffectKernel::OldFilm:
    case EffectKernel::LightLeak:
    case EffectKernel::Rain:
    case EffectKernel::Snow:
    case EffectKernel::Sparkles:
    case EffectKernel::Bokeh:
    case EffectKernel::Wave:
    case EffectKernel::Swirl:
    case EffectKernel::Spotlight:
    case EffectKernel::Flicker:
    case EffectKernel::RgbSplit:
        return true;
    default:
        return false;
    }
}

void renderVideoEffect(EffectKernel kernel, const ImageView &image, const VideoEffectParams &p)
{
    if (!image.data || image.width < 2 || image.height < 2) {
        return;
    }
    const int w = image.width;
    const int h = image.height;
    const double cx = w / 2.0;
    const double cy = h / 2.0;
    const double t = p.time;
    const double amount = p.amount;
    switch (kernel) {
    case EffectKernel::Blur:
        blur(image, static_cast<int>(std::lround(h * 0.03 * amount)));
        break;
    case EffectKernel::ZoomBlur:
        radialBlur(image, amount * 0.25, 0.0);
        break;
    case EffectKernel::SpinBlur:
        radialBlur(image, 0.0, amount * 0.35);
        break;
    case EffectKernel::DirectionalBlur:
        applyMotionBlur(image, MotionBlurSettings{std::clamp(amount, 0.0, 1.0), p.angle, 15});
        break;
    case EffectKernel::Glow:
        glow(image, amount, std::clamp(p.size, 0.0, 0.95));
        break;
    case EffectKernel::Dreamy: {
        Copy soft(image);
        ImageView view = soft.view();
        blur(view, std::max(2, h / 60));
        forEachPixel(image, [&](int x, int y, std::uint8_t *px) {
            const std::uint8_t *s = soft.at(x, y);
            for (int c = 0; c < 3; ++c) {
                const double screen = 255 - (255 - px[c]) * (255 - s[c]) / 255.0;
                px[c] = toByte(px[c] + (screen - px[c]) * amount);
            }
        });
        break;
    }
    case EffectKernel::RgbSplit: {
        const double pulse = p.speed > 0 ? 0.5 + 0.5 * std::sin(t * p.speed * 2 * kPi) : 1.0;
        rgbSplit(image, h * 0.02 * amount * pulse, p.angle);
        break;
    }
    case EffectKernel::Glitch:
        glitch(image, p, false);
        break;
    case EffectKernel::BlockGlitch:
        glitch(image, p, true);
        break;
    case EffectKernel::Scanlines:
        scanlines(image, amount, p.size);
        break;
    case EffectKernel::Vhs:
        vhs(image, p);
        break;
    case EffectKernel::Noise:
        noise(image, amount, p.speed, t);
        break;
    case EffectKernel::Pixelate:
        pixelate(image, p.size);
        break;
    case EffectKernel::Mirror:
        remap(image, [&](double x, double y, double &sx, double &sy) {
            switch (p.count) {
            case 1:
                sx = x > cx ? x : w - 1 - x;
                break;
            case 2:
                sy = y < cy ? y : h - 1 - y;
                break;
            case 3:
                sx = x < cx ? x : w - 1 - x;
                sy = y < cy ? y : h - 1 - y;
                break;
            default:
                sx = x < cx ? x : w - 1 - x;
                break;
            }
        });
        break;
    case EffectKernel::Kaleidoscope: {
        const int segments = std::max(2, p.count > 0 ? p.count : 6);
        const double wedge = 2 * kPi / segments;
        const double turn = t * p.speed * 0.3;
        remap(image, [&](double x, double y, double &sx, double &sy) {
            const double dx = x - cx;
            const double dy = y - cy;
            const double r = std::hypot(dx, dy);
            double a = std::atan2(dy, dx) + turn;
            a = std::fmod(a, wedge);
            if (a < 0) {
                a += wedge;
            }
            if (a > wedge / 2) {
                a = wedge - a; // mirrored every other half-wedge: no seams
            }
            sx = cx + r * std::cos(a);
            sy = cy + r * std::sin(a);
        });
        break;
    }
    case EffectKernel::Shake: {
        const double f = std::max(0.1, p.speed) * 8;
        const double dx = smoothNoise(t * f, 1) * w * 0.03 * amount;
        const double dy = smoothNoise(t * f, 2) * h * 0.03 * amount;
        const double rot = smoothNoise(t * f, 3) * 0.02 * amount;
        const double zoom = 1.0 + 0.08 * amount; // hides the borders that would come in
        remap(image, [&](double x, double y, double &sx, double &sy) {
            const double ux = (x - cx) / zoom;
            const double uy = (y - cy) / zoom;
            sx = cx + ux * std::cos(rot) - uy * std::sin(rot) - dx;
            sy = cy + ux * std::sin(rot) + uy * std::cos(rot) - dy;
        });
        break;
    }
    case EffectKernel::ZoomPulse: {
        const double phase = std::fmod(t * std::max(0.1, p.speed), 1.0);
        const double zoom = 1.0 + amount * 0.25 * std::exp(-5 * phase);
        remap(image, [&](double x, double y, double &sx, double &sy) {
            sx = cx + (x - cx) / zoom;
            sy = cy + (y - cy) / zoom;
        });
        break;
    }
    case EffectKernel::Strobe: {
        const double phase = std::fmod(t * std::max(0.1, p.speed) * 4, 1.0);
        if (phase < 0.3) {
            const double a = amount * (1.0 - phase / 0.3);
            forEachPixel(image, [&](int, int, std::uint8_t *px) {
                px[0] = toByte(px[0] + (p.color.r * 255 - px[0]) * a);
                px[1] = toByte(px[1] + (p.color.g * 255 - px[1]) * a);
                px[2] = toByte(px[2] + (p.color.b * 255 - px[2]) * a);
            });
        }
        break;
    }
    case EffectKernel::Invert:
        forEachPixel(image, [&](int, int, std::uint8_t *px) {
            for (int c = 0; c < 3; ++c) {
                px[c] = toByte(px[c] + (255 - 2 * px[c]) * amount);
            }
        });
        break;
    case EffectKernel::Posterize: {
        const double levels = 2 + std::round((1.0 - amount) * 8);
        forEachPixel(image, [&](int, int, std::uint8_t *px) {
            for (int c = 0; c < 3; ++c) {
                px[c] = toByte(std::round(px[c] / 255.0 * (levels - 1)) / (levels - 1) * 255);
            }
        });
        break;
    }
    case EffectKernel::Edges:
    case EffectKernel::Sketch:
    case EffectKernel::NeonEdges: {
        const Copy original(image);
        std::vector<double> edges;
        edgeMagnitude(image, edges);
        forEachPixel(image, [&](int x, int y, std::uint8_t *px) {
            const double e = std::clamp(edges[static_cast<size_t>(y) * w + x] * 2.0, 0.0, 1.0);
            if (kernel == EffectKernel::Edges) {
                px[0] = px[1] = px[2] = toByte(e * 255);
            } else if (kernel == EffectKernel::Sketch) {
                const double paper = 245 - (1.0 - luma(original.at(x, y))) * 40;
                px[0] = px[1] = px[2] = toByte(paper * (1.0 - e * 0.9));
            } else {
                const double dim = 0.25;
                px[0] = toByte(px[0] * dim + p.color.r * 255 * e * 1.5);
                px[1] = toByte(px[1] * dim + p.color.g * 255 * e * 1.5);
                px[2] = toByte(px[2] * dim + p.color.b * 255 * e * 1.5);
            }
        });
        if (kernel == EffectKernel::NeonEdges) {
            glow(image, 0.6, 0.3);
        }
        mixInto(image, original, amount);
        break;
    }
    case EffectKernel::Emboss: {
        const Copy source(image);
        forEachPixel(image, [&](int x, int y, std::uint8_t *px) {
            const double v = 0.5 + (luma(source.at(x + 1, y + 1)) - luma(source.at(x - 1, y - 1))) * 2;
            const auto g = static_cast<double>(toByte(v * 255));
            for (int c = 0; c < 3; ++c) {
                px[c] = toByte(px[c] + (g - px[c]) * amount);
            }
        });
        break;
    }
    case EffectKernel::PulseVignette:
        vignette(image, amount * (0.6 + 0.4 * std::sin(t * std::max(0.1, p.speed) * 2 * kPi)), p.color);
        break;
    case EffectKernel::HueCycle: {
        // Rotation of the chroma in YIQ, plus a saturation boost.
        const double a = t * p.speed * 2 * kPi;
        const double cosA = std::cos(a);
        const double sinA = std::sin(a);
        const double sat = 1.0 + amount;
        forEachPixel(image, [&](int, int, std::uint8_t *px) {
            const double r = px[0], g = px[1], b = px[2];
            const double y = 0.299 * r + 0.587 * g + 0.114 * b;
            const double i = (0.596 * r - 0.274 * g - 0.322 * b) * sat;
            const double q = (0.211 * r - 0.523 * g + 0.312 * b) * sat;
            const double i2 = i * cosA - q * sinA;
            const double q2 = i * sinA + q * cosA;
            px[0] = toByte(y + 0.956 * i2 + 0.621 * q2);
            px[1] = toByte(y - 0.272 * i2 - 0.647 * q2);
            px[2] = toByte(y - 1.106 * i2 + 1.703 * q2);
        });
        break;
    }
    case EffectKernel::Duotone:
        forEachPixel(image, [&](int, int, std::uint8_t *px) {
            const double l = luma(px);
            px[0] = toByte((p.color.r * (1 - l) + p.color2.r * l) * 255);
            px[1] = toByte((p.color.g * (1 - l) + p.color2.g * l) * 255);
            px[2] = toByte((p.color.b * (1 - l) + p.color2.b * l) * 255);
        });
        break;
    case EffectKernel::Thermal: {
        const Copy original(image);
        forEachPixel(image, [&](int, int, std::uint8_t *px) { thermalColor(luma(px), px); });
        mixInto(image, original, amount);
        break;
    }
    case EffectKernel::NightVision:
        forEachPixel(image, [&](int, int, std::uint8_t *px) {
            const double l = std::pow(luma(px), 0.7) * 1.3;
            px[0] = toByte(l * 60);
            px[1] = toByte(l * 255);
            px[2] = toByte(l * 70);
        });
        noise(image, 0.3 * amount + 0.1, 1.0, t);
        scanlines(image, 0.3, 0.05);
        vignette(image, 0.9);
        break;
    case EffectKernel::OldFilm: {
        const auto frame = static_cast<std::uint32_t>(std::floor(t * 24));
        const double flicker = 1.0 + (random01(frame, 1) - 0.5) * 0.15 * amount;
        forEachPixel(image, [&](int, int, std::uint8_t *px) {
            const double r = px[0], g = px[1], b = px[2];
            const double sr = (0.393 * r + 0.769 * g + 0.189 * b) * flicker;
            const double sg = (0.349 * r + 0.686 * g + 0.168 * b) * flicker;
            const double sb = (0.272 * r + 0.534 * g + 0.131 * b) * flicker;
            px[0] = toByte(r + (sr - r) * amount);
            px[1] = toByte(g + (sg - g) * amount);
            px[2] = toByte(b + (sb - b) * amount);
        });
        noise(image, 0.25 * amount, 1.0, t);
        // Vertical scratches that come and go.
        for (int s = 0; s < 3; ++s) {
            if (random01(frame, 10, static_cast<std::uint32_t>(s)) < 0.5 * amount) {
                const int x = static_cast<int>(random01(frame, 11, static_cast<std::uint32_t>(s)) * w);
                const bool light = random01(frame, 12, static_cast<std::uint32_t>(s)) < 0.5;
                for (int y = 0; y < h; ++y) {
                    std::uint8_t *px = image.row(y) + std::clamp(x, 0, w - 1) * 4;
                    for (int c = 0; c < 3; ++c) {
                        px[c] = toByte(light ? px[c] * 0.5 + 110 : px[c] * 0.5);
                    }
                }
            }
        }
        vignette(image, 0.6 * amount);
        break;
    }
    case EffectKernel::LightLeak: {
        const double s = t * std::max(0.05, p.speed) * 0.25;
        for (int k = 0; k < 3; ++k) {
            const double x = w * (0.5 + 0.6 * std::sin(s * (1.0 + k * 0.7) + k * 2.1));
            const double y = h * (0.5 + 0.5 * std::cos(s * (0.8 + k * 0.5) + k));
            addSpot(image, x, y, h * (0.6 + 0.2 * k), k == 1 ? p.color2 : p.color, amount * 0.55);
        }
        break;
    }
    case EffectKernel::Rain:
    case EffectKernel::Snow:
    case EffectKernel::Sparkles:
    case EffectKernel::Bokeh:
        particles(image, p, kernel);
        break;
    case EffectKernel::Wave: {
        const double wavelength = h * (0.05 + p.size * 0.4);
        const double a = h * 0.03 * amount;
        const bool vertical = std::abs(std::fmod(p.angle, 180.0)) > 45;
        remap(image, [&](double x, double y, double &sx, double &sy) {
            if (vertical) {
                sy = y + a * std::sin(x / wavelength * 2 * kPi + t * p.speed * 2 * kPi);
            } else {
                sx = x + a * std::sin(y / wavelength * 2 * kPi + t * p.speed * 2 * kPi);
            }
        });
        break;
    }
    case EffectKernel::Swirl: {
        const double radius = std::min(w, h) * 0.6;
        const double strength = amount * 4 * (p.speed != 0 ? std::sin(t * p.speed * kPi) * 0.5 + 0.5 : 1.0);
        remap(image, [&](double x, double y, double &sx, double &sy) {
            const double dx = x - cx;
            const double dy = y - cy;
            const double r = std::hypot(dx, dy);
            if (r >= radius) {
                return;
            }
            const double a = strength * std::pow(1.0 - r / radius, 2);
            sx = cx + dx * std::cos(a) - dy * std::sin(a);
            sy = cy + dx * std::sin(a) + dy * std::cos(a);
        });
        break;
    }
    case EffectKernel::Bulge: {
        const double radius = std::min(w, h) * 0.5;
        remap(image, [&](double x, double y, double &sx, double &sy) {
            const double dx = x - cx;
            const double dy = y - cy;
            const double r = std::hypot(dx, dy) / radius;
            if (r >= 1.0 || r == 0.0) {
                return;
            }
            const double k = std::pow(r, amount > 0 ? 1.0 + amount : 1.0 / (1.0 - amount)) / r;
            sx = cx + dx * k;
            sy = cy + dy * k;
        });
        break;
    }
    case EffectKernel::Grid: {
        const int n = std::clamp(p.count > 0 ? p.count : 2, 2, 4);
        remap(image, [&](double x, double y, double &sx, double &sy) {
            sx = std::fmod(x * n, w);
            sy = std::fmod(y * n, h);
        });
        break;
    }
    case EffectKernel::Letterbox: {
        const int bar = static_cast<int>(h * std::clamp(p.size, 0.0, 0.45) * 0.4);
        for (int y = 0; y < h; ++y) {
            if (y >= bar && y < h - bar) {
                continue;
            }
            std::uint8_t *row = image.row(y);
            for (int x = 0; x < w; ++x) {
                row[x * 4] = toByte(p.color.r * 255);
                row[x * 4 + 1] = toByte(p.color.g * 255);
                row[x * 4 + 2] = toByte(p.color.b * 255);
            }
        }
        break;
    }
    case EffectKernel::Halftone: {
        const Copy original(image);
        halftone(image, p.size, p.color);
        mixInto(image, original, amount);
        break;
    }
    case EffectKernel::Dither:
        dither(image, p.size, p.count > 0 ? p.count : 3);
        break;
    case EffectKernel::TiltShift: {
        const Copy sharp(image);
        blur(image, static_cast<int>(std::lround(h * 0.02 * amount)));
        const double band = std::clamp(p.size, 0.05, 0.9) * h / 2;
        forEachPixel(image, [&](int x, int y, std::uint8_t *px) {
            const double d = std::abs(y - cy);
            const double keep = std::clamp(1.0 - (d - band) / (h * 0.15), 0.0, 1.0);
            const std::uint8_t *s = sharp.at(x, y);
            for (int c = 0; c < 3; ++c) {
                px[c] = toByte(px[c] + (s[c] - px[c]) * keep);
            }
            // Toy-like colours.
            const double l = luma(px) * 255;
            for (int c = 0; c < 3; ++c) {
                px[c] = toByte(l + (px[c] - l) * 1.25);
            }
        });
        break;
    }
    case EffectKernel::Prism: {
        const Copy source(image);
        const double d = h * 0.03 * amount;
        forEachPixel(image, [&](int x, int y, std::uint8_t *px) {
            const auto a = source.sample(x - d, y);
            const auto b = source.sample(x + d, y - d * 0.5);
            // Two tinted ghosts lighten the picture, like light through a prism.
            px[0] = toByte(std::max<double>(px[0], a[0] * 0.9));
            px[1] = toByte(std::max<double>(px[1], b[1] * 0.9));
            px[2] = toByte(std::max<double>(px[2], std::max(a[2], b[2]) * 0.8));
        });
        break;
    }
    case EffectKernel::LensAberration: {
        const Copy source(image);
        const double k = 0.02 * amount;
        forEachPixel(image, [&](int x, int y, std::uint8_t *px) {
            const double dx = x - cx;
            const double dy = y - cy;
            px[0] = toByte(source.sample(cx + dx * (1 + k), cy + dy * (1 + k))[0]);
            px[2] = toByte(source.sample(cx + dx * (1 - k), cy + dy * (1 - k))[2]);
        });
        break;
    }
    case EffectKernel::Spotlight: {
        const double s = t * std::max(0.05, p.speed) * 0.5;
        const double sx = cx + std::sin(s * 1.3) * w * 0.3;
        const double sy = cy + std::cos(s) * h * 0.2;
        const double radius = h * (0.2 + p.size * 0.5);
        forEachPixel(image, [&](int x, int y, std::uint8_t *px) {
            const double d = std::hypot(x - sx, y - sy) / radius;
            const double dark = amount * std::clamp((d - 0.7) / 0.5, 0.0, 1.0);
            for (int c = 0; c < 3; ++c) {
                px[c] = toByte(px[c] * (1 - dark));
            }
        });
        break;
    }
    case EffectKernel::Flicker: {
        const double f = 1.0 + (smoothNoise(t * std::max(0.1, p.speed) * 12, 5) * 0.5 +
                                (random01(static_cast<std::uint32_t>(t * 24), 6) - 0.5) * 0.5) * amount * 0.6;
        forEachPixel(image, [&](int, int, std::uint8_t *px) {
            for (int c = 0; c < 3; ++c) {
                px[c] = toByte(px[c] * f);
            }
        });
        break;
    }
    case EffectKernel::ColorShift:
        forEachPixel(image, [&](int, int, std::uint8_t *px) {
            const std::array<std::uint8_t, 3> in{px[0], px[1], px[2]};
            const int shift = p.count == 1 ? 2 : 1;
            for (int c = 0; c < 3; ++c) {
                px[c] = toByte(in[static_cast<size_t>(c)] + (in[static_cast<size_t>((c + shift) % 3)] - in[static_cast<size_t>(c)]) * amount);
            }
        });
        break;
    }
    (void)cx;
    (void)cy;
}

} // namespace vedit::fx
