// SPDX-License-Identifier: GPL-3.0-or-later
#include "Blur.h"

#include "fx/Parallel.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace velacut::fx {

namespace {

constexpr int kBlockPixels = 16;
constexpr int kBlockBytes = kBlockPixels * 4;

// Exact integer division of 0 ≤ sum ≤ 255 × window by `window` with a float multiplication (vectorizes, a 64-bit one
// does not): sum = k × window + r, 0 ≤ r < window, so (sum + 0.5) / window lies in (k, k + 1) at least 0.5 / window
// away from both ends, far more than the float rounding error (≈ 2⁻²⁴ × 256 for windows up to 2¹⁶): the truncation is k.
struct Divider
{
    float reciprocal;
    float bias;

    Divider(int window, BoxRounding rounding)
        : reciprocal(1.0f / static_cast<float>(window))
        , bias((rounding == BoxRounding::Nearest ? static_cast<float>(window / 2) : 0.0f) + 0.5f)
    {
    }
    std::uint8_t operator()(std::uint32_t sum) const
    {
        return static_cast<std::uint8_t>(static_cast<std::int32_t>((static_cast<float>(static_cast<std::int32_t>(sum)) + bias) * reciprocal));
    }
};

thread_local std::vector<std::uint8_t> t_line;

// One box pass along a line of `length` pixels of `lanes` interleaved bytes each (4 for a row; 4 × block width for a
// block of columns), read from `source` (packed) and written through `out(i)` (pointer to the `lanes` bytes of pixel i).
// `Lanes` > 0: that many lanes, known at compile time (the loops over lanes are unrolled and vectorized); 0: `lanes`.
template<int Lanes, int MaxLanes, typename Out>
void boxLine(const std::uint8_t *source, int length, int runtimeLanes, int radius, const Divider &divide, Out out)
{
    const int lanes = Lanes > 0 ? Lanes : runtimeLanes;
    std::uint32_t sum[MaxLanes];
    for (int c = 0; c < lanes; ++c) {
        sum[c] = 0;
    }
    for (int k = -radius; k <= radius; ++k) {
        const std::uint8_t *p = source + static_cast<size_t>(std::clamp(k, 0, length - 1)) * lanes;
        for (int c = 0; c < lanes; ++c) {
            sum[c] += p[c];
        }
    }
    for (int i = 0; i < length; ++i) {
        std::uint8_t *o = out(i);
        for (int c = 0; c < lanes; ++c) {
            o[c] = divide(sum[c]);
        }
        const std::uint8_t *in = source + static_cast<size_t>(std::min(i + radius + 1, length - 1)) * lanes;
        const std::uint8_t *gone = source + static_cast<size_t>(std::max(i - radius, 0)) * lanes;
        for (int c = 0; c < lanes; ++c) {
            sum[c] += in[c];
            sum[c] -= gone[c];
        }
    }
}

void horizontalPass(const ImageView &image, int radius, const Divider &divide)
{
    parallelFor(image.height, [&](int begin, int end) {
        t_line.resize(static_cast<size_t>(image.width) * 4);
        for (int y = begin; y < end; ++y) {
            std::uint8_t *row = image.row(y);
            std::memcpy(t_line.data(), row, static_cast<size_t>(image.width) * 4);
            boxLine<4, 4>(t_line.data(), image.width, 4, radius, divide, [row](int i) { return row + i * 4; });
        }
    }, 8);
}

void verticalPass(const ImageView &image, int radius, const Divider &divide)
{
    const int blocks = (image.width + kBlockPixels - 1) / kBlockPixels;
    parallelFor(blocks, [&](int begin, int end) {
        t_line.resize(static_cast<size_t>(image.height) * kBlockBytes);
        for (int block = begin; block < end; ++block) {
            const int x0 = block * kBlockPixels;
            const int lanes = std::min(kBlockPixels, image.width - x0) * 4;
            for (int y = 0; y < image.height; ++y) {
                std::memcpy(t_line.data() + static_cast<size_t>(y) * lanes, image.row(y) + x0 * 4, static_cast<size_t>(lanes));
            }
            const auto out = [&image, x0](int i) { return image.row(i) + x0 * 4; };
            if (lanes == kBlockBytes) {
                boxLine<kBlockBytes, kBlockBytes>(t_line.data(), image.height, lanes, radius, divide, out);
            } else {
                boxLine<0, kBlockBytes>(t_line.data(), image.height, lanes, radius, divide, out);
            }
        }
    }, 2);
}

// Averages `factor` × `factor` blocks of `image` into `small` (blocks cut by the right and bottom edges average what
// they have).
void shrink(const ImageView &image, const ImageView &small, int factor)
{
    parallelFor(small.height, [&](int begin, int end) {
        for (int y = begin; y < end; ++y) {
            const int y0 = y * factor;
            const int y1 = std::min(image.height, y0 + factor);
            std::uint8_t *out = small.row(y);
            for (int x = 0; x < small.width; ++x) {
                const int x0 = x * factor;
                const int x1 = std::min(image.width, x0 + factor);
                std::uint32_t sum[4] = {0, 0, 0, 0};
                for (int yy = y0; yy < y1; ++yy) {
                    const std::uint8_t *p = image.row(yy) + x0 * 4;
                    for (int xx = x0; xx < x1; ++xx, p += 4) {
                        for (int c = 0; c < 4; ++c) {
                            sum[c] += p[c];
                        }
                    }
                }
                const auto n = static_cast<std::uint32_t>((x1 - x0) * (y1 - y0));
                for (int c = 0; c < 4; ++c) {
                    out[x * 4 + c] = static_cast<std::uint8_t>((sum[c] + n / 2) / n);
                }
            }
        }
    }, 4);
}

// Bilinear enlargement of `small` into the whole of `image` (pixel centres aligned, edges clamped), weights in 1/256:
// every row of `small` interpolated across once (into `across`, 16 bits a channel), then each row of `image` mixes two
// of them (a plain loop over bytes, vectorized).
void enlarge(const ImageView &small, const ImageView &image, int factor)
{
    struct Tap
    {
        int first;  // index of the left (top) neighbour
        int second; // of the right (bottom) one
        int weight; // of the second, 0–256
    };
    const auto taps = [factor](int count, int smallCount) {
        std::vector<Tap> result(static_cast<size_t>(count));
        for (int i = 0; i < count; ++i) {
            const double f = std::clamp((i + 0.5) / factor - 0.5, 0.0, smallCount - 1.0);
            const int i0 = static_cast<int>(f);
            result[static_cast<size_t>(i)] = {i0, std::min(i0 + 1, smallCount - 1), static_cast<int>(std::lround((f - i0) * 256))};
        }
        return result;
    };
    const std::vector<Tap> columns = taps(image.width, small.width);
    const std::vector<Tap> rows = taps(image.height, small.height);
    const size_t lineValues = static_cast<size_t>(image.width) * 4;
    const Scratch acrossBuffer(lineValues * static_cast<size_t>(small.height) * sizeof(std::uint16_t));
    auto *across = reinterpret_cast<std::uint16_t *>(acrossBuffer.data());
    parallelFor(small.height, [&](int begin, int end) {
        for (int y = begin; y < end; ++y) {
            const std::uint8_t *in = small.row(y);
            std::uint16_t *out = across + lineValues * static_cast<size_t>(y);
            for (int x = 0; x < image.width; ++x) {
                const Tap &t = columns[static_cast<size_t>(x)];
                for (int c = 0; c < 4; ++c) {
                    out[x * 4 + c] = static_cast<std::uint16_t>(in[t.first * 4 + c] * (256 - t.weight) + in[t.second * 4 + c] * t.weight);
                }
            }
        }
    }, 2);
    parallelFor(image.height, [&](int begin, int end) {
        for (int y = begin; y < end; ++y) {
            const Tap &t = rows[static_cast<size_t>(y)];
            const std::uint16_t *upper = across + lineValues * static_cast<size_t>(t.first);
            const std::uint16_t *lower = across + lineValues * static_cast<size_t>(t.second);
            const std::uint32_t wLower = static_cast<std::uint32_t>(t.weight);
            const std::uint32_t wUpper = 256U - wLower;
            std::uint8_t *out = image.row(y);
            for (size_t i = 0; i < lineValues; ++i) {
                out[i] = static_cast<std::uint8_t>((upper[i] * wUpper + lower[i] * wLower + 32768U) >> 16);
            }
        }
    }, 8);
}

} // namespace

void blur3(const ImageView &image, int radius)
{
    if (radius < 1 || !image.data || image.width < 1 || image.height < 1) {
        return;
    }
    const int factor = radius >= 24 ? 4 : (radius >= 8 ? 2 : 1);
    if (factor == 1 || image.width < 4 * factor || image.height < 4 * factor) {
        boxBlurPasses(image, radius, 3, BoxRounding::Truncate);
        return;
    }
    const int width = (image.width + factor - 1) / factor;
    const int height = (image.height + factor - 1) / factor;
    const Scratch buffer(static_cast<size_t>(width) * static_cast<size_t>(height) * 4);
    const ImageView small{buffer.data(), width, height, width * 4};
    shrink(image, small, factor);
    boxBlurPasses(small, std::max(1, static_cast<int>(std::lround(static_cast<double>(radius) / factor))), 3, BoxRounding::Truncate);
    enlarge(small, image, factor);
}

void boxBlurPasses(const ImageView &image, int radius, int iterations, BoxRounding rounding)
{
    if (radius < 1 || !image.data || image.width < 1 || image.height < 1) {
        return;
    }
    const Divider divide(2 * radius + 1, rounding);
    for (int i = 0; i < iterations; ++i) {
        horizontalPass(image, radius, divide);
        verticalPass(image, radius, divide);
    }
}

} // namespace velacut::fx
