#include "Composite.h"

#include <algorithm>
#include <cmath>

namespace vedit::fx {

namespace {
// Rounded integer division for non-negative values.
inline unsigned divRound(unsigned numerator, unsigned denominator)
{
    return (numerator + denominator / 2) / denominator;
}

inline float lum(float r, float g, float b)
{
    return 0.3f * r + 0.59f * g + 0.11f * b;
}

inline void clipColor(float &r, float &g, float &b)
{
    const float l = lum(r, g, b);
    const float n = std::min({r, g, b});
    const float x = std::max({r, g, b});
    if (n < 0.0f) {
        const float denom = l - n;
        if (denom > 1e-5f) {
            r = l + (((r - l) * l) / denom);
            g = l + (((g - l) * l) / denom);
            b = l + (((b - l) * l) / denom);
        } else {
            r = g = b = l;
        }
    }
    if (x > 1.0f) {
        const float denom = x - l;
        if (denom > 1e-5f) {
            const float oneMinusL = 1.0f - l;
            r = l + (((r - l) * oneMinusL) / denom);
            g = l + (((g - l) * oneMinusL) / denom);
            b = l + (((b - l) * oneMinusL) / denom);
        } else {
            r = g = b = l;
        }
    }
}

inline void setLum(float &r, float &g, float &b, float l)
{
    const float d = l - lum(r, g, b);
    r += d;
    g += d;
    b += d;
    clipColor(r, g, b);
}

inline float sat(float r, float g, float b)
{
    return std::max({r, g, b}) - std::min({r, g, b});
}

inline void setSat(float &r, float &g, float &b, float s)
{
    float *cMin = &r;
    float *cMid = &g;
    float *cMax = &b;
    if (*cMin > *cMid) {
        std::swap(cMin, cMid);
    }
    if (*cMid > *cMax) {
        std::swap(cMid, cMax);
    }
    if (*cMin > *cMid) {
        std::swap(cMin, cMid);
    }

    if (*cMax > *cMin) {
        *cMid = ((*cMid - *cMin) * s) / (*cMax - *cMin);
        *cMax = s;
    } else {
        *cMid = *cMax = 0.0f;
    }
    *cMin = 0.0f;
}

inline void blendNonSeparable(BlendMode mode, const float cb[3], const float cs[3], float outB[3])
{
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    switch (mode) {
    case BlendMode::Hue:
        r = cs[0];
        g = cs[1];
        b = cs[2];
        setSat(r, g, b, sat(cb[0], cb[1], cb[2]));
        setLum(r, g, b, lum(cb[0], cb[1], cb[2]));
        break;
    case BlendMode::Saturation:
        r = cb[0];
        g = cb[1];
        b = cb[2];
        setSat(r, g, b, sat(cs[0], cs[1], cs[2]));
        setLum(r, g, b, lum(cb[0], cb[1], cb[2]));
        break;
    case BlendMode::Color:
        r = cs[0];
        g = cs[1];
        b = cs[2];
        setLum(r, g, b, lum(cb[0], cb[1], cb[2]));
        break;
    case BlendMode::Luminosity:
        r = cb[0];
        g = cb[1];
        b = cb[2];
        setLum(r, g, b, lum(cs[0], cs[1], cs[2]));
        break;
    default:
        r = cs[0];
        g = cs[1];
        b = cs[2];
        break;
    }
    outB[0] = r;
    outB[1] = g;
    outB[2] = b;
}

inline float blendSeparable(BlendMode mode, float cb, float cs)
{
    switch (mode) {
    case BlendMode::Normal:
        return cs;
    case BlendMode::Multiply:
        return cb * cs;
    case BlendMode::Screen:
        return cb + cs - cb * cs;
    case BlendMode::Overlay:
        return (cb <= 0.5f) ? (2.0f * cb * cs) : (1.0f - 2.0f * (1.0f - cb) * (1.0f - cs));
    case BlendMode::Darken:
        return std::min(cb, cs);
    case BlendMode::Lighten:
        return std::max(cb, cs);
    case BlendMode::ColorDodge:
        if (cb <= 0.0f) {
            return 0.0f;
        }
        if (cs >= 1.0f) {
            return 1.0f;
        }
        return std::min(1.0f, cb / (1.0f - cs));
    case BlendMode::ColorBurn:
        if (cb >= 1.0f) {
            return 1.0f;
        }
        if (cs <= 0.0f) {
            return 0.0f;
        }
        return 1.0f - std::min(1.0f, (1.0f - cb) / cs);
    case BlendMode::HardLight:
        return (cs <= 0.5f) ? (2.0f * cb * cs) : (1.0f - 2.0f * (1.0f - cb) * (1.0f - cs));
    case BlendMode::SoftLight: {
        const float d = (cb <= 0.25f) ? (((16.0f * cb - 12.0f) * cb + 4.0f) * cb) : std::sqrt(cb);
        return (cs <= 0.5f) ? (cb - (1.0f - 2.0f * cs) * cb * (1.0f - cb)) : (cb + (2.0f * cs - 1.0f) * (d - cb));
    }
    case BlendMode::Difference:
        return std::abs(cb - cs);
    case BlendMode::Exclusion:
        return cb + cs - 2.0f * cb * cs;
    case BlendMode::Add:
        return std::min(1.0f, cb + cs);
    default:
        return cs;
    }
}
} // namespace

void compositeOver(ImageView destination, ConstImageView source, int opacity, int rowBegin, int rowEnd)
{
    const int width = std::min(destination.width, source.width);
    const int endRow = std::min({rowEnd, destination.height, source.height});
    const unsigned op = static_cast<unsigned>(std::clamp(opacity, 0, 255));
    if (op == 0) {
        return;
    }
    for (int y = std::max(0, rowBegin); y < endRow; ++y) {
        std::uint8_t *d = destination.row(y);
        const std::uint8_t *s = source.row(y);
        for (int x = 0; x < width; ++x, d += 4, s += 4) {
            const unsigned sa = op == 255 ? s[3] : divRound(s[3] * op, 255);
            if (sa == 0) {
                continue;
            }
            if (sa == 255) {
                d[0] = s[0];
                d[1] = s[1];
                d[2] = s[2];
                d[3] = 255;
                continue;
            }
            const unsigned da = d[3];
            // outA = sa + da (1 - sa), everything scaled by 255.
            const unsigned inverse = 255 - sa;
            const unsigned outA255 = sa * 255 + da * inverse; // outA * 255
            if (outA255 == 0) {
                continue;
            }
            for (int c = 0; c < 3; ++c) {
                // Straight alpha: outC = (sC * sa + dC * da * (1 - sa)) / outA
                const unsigned numerator = s[c] * sa * 255 + d[c] * da * inverse;
                d[c] = static_cast<std::uint8_t>(std::min(255u, divRound(numerator, outA255)));
            }
            d[3] = static_cast<std::uint8_t>(divRound(outA255, 255));
        }
    }
}

void compositeBlend(ImageView destination, ConstImageView source, BlendMode mode, int opacity, int rowBegin, int rowEnd)
{
    if (mode == BlendMode::Normal) {
        compositeOver(destination, source, opacity, rowBegin, rowEnd);
        return;
    }
    const int width = std::min(destination.width, source.width);
    const int endRow = std::min({rowEnd, destination.height, source.height});
    const float op = static_cast<float>(std::clamp(opacity, 0, 255)) / 255.0f;
    if (op <= 0.0f) {
        return;
    }
    const bool isNonSeparable = (mode == BlendMode::Hue || mode == BlendMode::Saturation ||
                                 mode == BlendMode::Color || mode == BlendMode::Luminosity);

    for (int y = std::max(0, rowBegin); y < endRow; ++y) {
        std::uint8_t *d = destination.row(y);
        const std::uint8_t *s = source.row(y);
        for (int x = 0; x < width; ++x, d += 4, s += 4) {
            const float sa = (static_cast<float>(s[3]) / 255.0f) * op;
            if (sa <= 0.0f) {
                continue;
            }
            const float da = static_cast<float>(d[3]) / 255.0f;
            const float outA = sa + da * (1.0f - sa);
            if (outA <= 0.0f) {
                d[0] = d[1] = d[2] = d[3] = 0;
                continue;
            }
            const float cb[3] = {static_cast<float>(d[0]) / 255.0f, static_cast<float>(d[1]) / 255.0f, static_cast<float>(d[2]) / 255.0f};
            const float cs[3] = {static_cast<float>(s[0]) / 255.0f, static_cast<float>(s[1]) / 255.0f, static_cast<float>(s[2]) / 255.0f};
            float bResult[3];
            if (isNonSeparable) {
                blendNonSeparable(mode, cb, cs, bResult);
            } else {
                bResult[0] = blendSeparable(mode, cb[0], cs[0]);
                bResult[1] = blendSeparable(mode, cb[1], cs[1]);
                bResult[2] = blendSeparable(mode, cb[2], cs[2]);
            }
            const float sa_oneMinusDa = sa * (1.0f - da);
            const float da_oneMinusSa = da * (1.0f - sa);
            const float sa_da = sa * da;
            for (int c = 0; c < 3; ++c) {
                const float outC = (sa_oneMinusDa * cs[c] + da_oneMinusSa * cb[c] + sa_da * bResult[c]) / outA;
                d[c] = static_cast<std::uint8_t>(std::clamp(std::round(outC * 255.0f), 0.0f, 255.0f));
            }
            d[3] = static_cast<std::uint8_t>(std::clamp(std::round(outA * 255.0f), 0.0f, 255.0f));
        }
    }
}

} // namespace vedit::fx
