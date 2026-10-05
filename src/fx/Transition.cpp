// SPDX-License-Identifier: GPL-3.0-or-later
#include "Transition.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace vedit::fx {

namespace {

constexpr std::array kNames{
    // 1. Base (10)
    std::pair{TransitionKind::Dissolve, std::string_view("dissolve")},
    std::pair{TransitionKind::DipToBlack, std::string_view("dipToBlack")},
    std::pair{TransitionKind::DipToWhite, std::string_view("dipToWhite")},
    std::pair{TransitionKind::DipToColor, std::string_view("dipToColor")},
    std::pair{TransitionKind::FadeGrayscale, std::string_view("fadeGrayscale")},
    std::pair{TransitionKind::ExposureFlash, std::string_view("exposureFlash")},
    std::pair{TransitionKind::LumaFade, std::string_view("lumaFade")},
    std::pair{TransitionKind::Additive, std::string_view("additive")},
    std::pair{TransitionKind::Subtract, std::string_view("subtract")},
    std::pair{TransitionKind::Multiply, std::string_view("multiply")},

    // 2. Motion (16)
    std::pair{TransitionKind::SlideLeft, std::string_view("slideLeft")},
    std::pair{TransitionKind::SlideRight, std::string_view("slideRight")},
    std::pair{TransitionKind::SlideUp, std::string_view("slideUp")},
    std::pair{TransitionKind::SlideDown, std::string_view("slideDown")},
    std::pair{TransitionKind::SlideTopLeft, std::string_view("slideTopLeft")},
    std::pair{TransitionKind::SlideTopRight, std::string_view("slideTopRight")},
    std::pair{TransitionKind::SlideBottomLeft, std::string_view("slideBottomLeft")},
    std::pair{TransitionKind::SlideBottomRight, std::string_view("slideBottomRight")},
    std::pair{TransitionKind::PushLeft, std::string_view("pushLeft")},
    std::pair{TransitionKind::PushRight, std::string_view("pushRight")},
    std::pair{TransitionKind::PushUp, std::string_view("pushUp")},
    std::pair{TransitionKind::PushDown, std::string_view("pushDown")},
    std::pair{TransitionKind::PushTopLeft, std::string_view("pushTopLeft")},
    std::pair{TransitionKind::PushTopRight, std::string_view("pushTopRight")},
    std::pair{TransitionKind::PushBottomLeft, std::string_view("pushBottomLeft")},
    std::pair{TransitionKind::PushBottomRight, std::string_view("pushBottomRight")},

    // 3. Zoom & Spin (12)
    std::pair{TransitionKind::ZoomIn, std::string_view("zoomIn")},
    std::pair{TransitionKind::ZoomOut, std::string_view("zoomOut")},
    std::pair{TransitionKind::CrossZoom, std::string_view("crossZoom")},
    std::pair{TransitionKind::WarpZoom, std::string_view("warpZoom")},
    std::pair{TransitionKind::SpinZoomIn, std::string_view("spinZoomIn")},
    std::pair{TransitionKind::SpinZoomOut, std::string_view("spinZoomOut")},
    std::pair{TransitionKind::SpinCw, std::string_view("spinCw")},
    std::pair{TransitionKind::SpinCcw, std::string_view("spinCcw")},
    std::pair{TransitionKind::RotateScaleCw, std::string_view("rotateScaleCw")},
    std::pair{TransitionKind::RotateScaleCcw, std::string_view("rotateScaleCcw")},
    std::pair{TransitionKind::DollyZoomIn, std::string_view("dollyZoomIn")},
    std::pair{TransitionKind::DollyZoomOut, std::string_view("dollyZoomOut")},

    // 4. Wipes & Bands (16)
    std::pair{TransitionKind::WipeLeft, std::string_view("wipeLeft")},
    std::pair{TransitionKind::WipeRight, std::string_view("wipeRight")},
    std::pair{TransitionKind::WipeUp, std::string_view("wipeUp")},
    std::pair{TransitionKind::WipeDown, std::string_view("wipeDown")},
    std::pair{TransitionKind::WipeTopLeft, std::string_view("wipeTopLeft")},
    std::pair{TransitionKind::WipeTopRight, std::string_view("wipeTopRight")},
    std::pair{TransitionKind::WipeBottomLeft, std::string_view("wipeBottomLeft")},
    std::pair{TransitionKind::WipeBottomRight, std::string_view("wipeBottomRight")},
    std::pair{TransitionKind::SplitHorizontal, std::string_view("splitHorizontal")},
    std::pair{TransitionKind::SplitVertical, std::string_view("splitVertical")},
    std::pair{TransitionKind::BarnDoorHorizontal, std::string_view("barnDoorHorizontal")},
    std::pair{TransitionKind::BarnDoorVertical, std::string_view("barnDoorVertical")},
    std::pair{TransitionKind::BlindsHorizontal, std::string_view("blindsHorizontal")},
    std::pair{TransitionKind::BlindsVertical, std::string_view("blindsVertical")},
    std::pair{TransitionKind::Checkerboard, std::string_view("checkerboard")},
    std::pair{TransitionKind::MosaicWipe, std::string_view("mosaicWipe")},

    // 5. Shapes & Geometry (12)
    std::pair{TransitionKind::Iris, std::string_view("iris")},
    std::pair{TransitionKind::IrisDiamond, std::string_view("irisDiamond")},
    std::pair{TransitionKind::IrisStar, std::string_view("irisStar")},
    std::pair{TransitionKind::IrisHeart, std::string_view("irisHeart")},
    std::pair{TransitionKind::IrisTriangle, std::string_view("irisTriangle")},
    std::pair{TransitionKind::Clock, std::string_view("clock")},
    std::pair{TransitionKind::ClockCounter, std::string_view("clockCounter")},
    std::pair{TransitionKind::ClockDual, std::string_view("clockDual")},
    std::pair{TransitionKind::HexagonGrid, std::string_view("hexagonGrid")},
    std::pair{TransitionKind::PolygonOpen, std::string_view("polygonOpen")},
    std::pair{TransitionKind::DiagonalSliceLeft, std::string_view("diagonalSliceLeft")},
    std::pair{TransitionKind::DiagonalSliceRight, std::string_view("diagonalSliceRight")},

    // 6. Blurs & Camera (10)
    std::pair{TransitionKind::BlurDissolve, std::string_view("blurDissolve")},
    std::pair{TransitionKind::DirectionalBlurLeft, std::string_view("directionalBlurLeft")},
    std::pair{TransitionKind::DirectionalBlurRight, std::string_view("directionalBlurRight")},
    std::pair{TransitionKind::RadialBlur, std::string_view("radialBlur")},
    std::pair{TransitionKind::TiltShift, std::string_view("tiltShift")},
    std::pair{TransitionKind::WhipPanLeft, std::string_view("whipPanLeft")},
    std::pair{TransitionKind::WhipPanRight, std::string_view("whipPanRight")},
    std::pair{TransitionKind::WhipPanUp, std::string_view("whipPanUp")},
    std::pair{TransitionKind::WhipPanDown, std::string_view("whipPanDown")},
    std::pair{TransitionKind::CameraShutter, std::string_view("cameraShutter")},

    // 7. Glitch & Digital (10)
    std::pair{TransitionKind::GlitchRgb, std::string_view("glitchRgb")},
    std::pair{TransitionKind::Pixelate, std::string_view("pixelate")},
    std::pair{TransitionKind::ScanlineTear, std::string_view("scanlineTear")},
    std::pair{TransitionKind::VhsDistortion, std::string_view("vhsDistortion")},
    std::pair{TransitionKind::BlockDissolve, std::string_view("blockDissolve")},
    std::pair{TransitionKind::DigitalNoise, std::string_view("digitalNoise")},
    std::pair{TransitionKind::Jitter, std::string_view("jitter")},
    std::pair{TransitionKind::DataCorruption, std::string_view("dataCorruption")},
    std::pair{TransitionKind::ScreenTear, std::string_view("screenTear")},
    std::pair{TransitionKind::LumaGlitch, std::string_view("lumaGlitch")},

    // 8. Lights & Leaks (10)
    std::pair{TransitionKind::LightLeakWarm, std::string_view("lightLeakWarm")},
    std::pair{TransitionKind::LightLeakCool, std::string_view("lightLeakCool")},
    std::pair{TransitionKind::LensGlow, std::string_view("lensGlow")},
    std::pair{TransitionKind::FilmBurn, std::string_view("filmBurn")},
    std::pair{TransitionKind::RainbowFlash, std::string_view("rainbowFlash")},
    std::pair{TransitionKind::ColorInvert, std::string_view("colorInvert")},
    std::pair{TransitionKind::Solarize, std::string_view("solarize")},
    std::pair{TransitionKind::NeonFlash, std::string_view("neonFlash")},
    std::pair{TransitionKind::Strobe, std::string_view("strobe")},
    std::pair{TransitionKind::LumaGlow, std::string_view("lumaGlow")},

    // 9. Distortions (10)
    std::pair{TransitionKind::WaveHorizontal, std::string_view("waveHorizontal")},
    std::pair{TransitionKind::WaveVertical, std::string_view("waveVertical")},
    std::pair{TransitionKind::RippleWater, std::string_view("rippleWater")},
    std::pair{TransitionKind::Vortex, std::string_view("vortex")},
    std::pair{TransitionKind::Shockwave, std::string_view("shockwave")},
    std::pair{TransitionKind::Pinch, std::string_view("pinch")},
    std::pair{TransitionKind::PinchTwist, std::string_view("pinchTwist")},
    std::pair{TransitionKind::SphereLens, std::string_view("sphereLens")},
    std::pair{TransitionKind::Swirl, std::string_view("swirl")},
    std::pair{TransitionKind::Kaleidoscope, std::string_view("kaleidoscope")},

    // 10. 3D Transitions (8)
    std::pair{TransitionKind::CubeLeft, std::string_view("cubeLeft")},
    std::pair{TransitionKind::CubeRight, std::string_view("cubeRight")},
    std::pair{TransitionKind::CubeUp, std::string_view("cubeUp")},
    std::pair{TransitionKind::CubeDown, std::string_view("cubeDown")},
    std::pair{TransitionKind::FlipHorizontal, std::string_view("flipHorizontal")},
    std::pair{TransitionKind::FlipVertical, std::string_view("flipVertical")},
    std::pair{TransitionKind::DoorSwingOpen, std::string_view("doorSwingOpen")},
    std::pair{TransitionKind::FoldOver, std::string_view("foldOver")},
};

struct Pixel
{
    float r = 0, g = 0, b = 0, a = 0; // premultiplied, 0…1
};

inline Pixel load(ConstImageView image, int x, int y)
{
    if (x < 0 || y < 0 || x >= image.width || y >= image.height) {
        return {};
    }
    const std::uint8_t *p = image.row(y) + x * 4;
    const float a = p[3] / 255.0f;
    return {p[0] / 255.0f * a, p[1] / 255.0f * a, p[2] / 255.0f * a, a};
}

inline Pixel mix(Pixel p, Pixel q, float t)
{
    return {p.r + (q.r - p.r) * t, p.g + (q.g - p.g) * t, p.b + (q.b - p.b) * t, p.a + (q.a - p.a) * t};
}

inline void store(std::uint8_t *d, Pixel p)
{
    const auto byte = [](float v) { return static_cast<std::uint8_t>(std::clamp(std::lround(v * 255.0f), 0L, 255L)); };
    if (p.a <= 0.0f) {
        d[0] = d[1] = d[2] = d[3] = 0;
        return;
    }
    d[0] = byte(p.r / p.a);
    d[1] = byte(p.g / p.a);
    d[2] = byte(p.b / p.a);
    d[3] = byte(p.a);
}

inline float smooth(float edge0, float edge1, float x)
{
    if (edge1 <= edge0) {
        return x < edge0 ? 0.0f : 1.0f;
    }
    const float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3 - 2 * t);
}

// Bilinear sample (clamped to the edges) at pixel coordinates.
inline Pixel sample(ConstImageView image, float x, float y)
{
    x = std::clamp(x - 0.5f, 0.0f, float(image.width - 1));
    y = std::clamp(y - 0.5f, 0.0f, float(image.height - 1));
    const int x0 = static_cast<int>(x);
    const int y0 = static_cast<int>(y);
    const int x1 = std::min(x0 + 1, image.width - 1);
    const int y1 = std::min(y0 + 1, image.height - 1);
    const float tx = x - x0;
    const float ty = y - y0;
    return mix(mix(load(image, x0, y0), load(image, x1, y0), tx), mix(load(image, x0, y1), load(image, x1, y1), tx), ty);
}

inline Pixel grayscale(Pixel p)
{
    const float lum = 0.2126f * p.r + 0.7152f * p.g + 0.0722f * p.b;
    return {lum, lum, lum, p.a};
}

inline float pseudoRandom(int x, int y)
{
    unsigned int n = static_cast<unsigned int>(x * 374761393 + y * 668265263);
    n = (n ^ (n >> 13)) * 1274126177;
    return static_cast<float>(n & 0x7fffffff) / static_cast<float>(0x7fffffff);
}

} // namespace

float transitionNoise(int x, int y)
{
    return pseudoRandom(x, y);
}

std::optional<TransitionKind> transitionKindFromName(std::string_view name)
{
    for (const auto &[kind, text] : kNames) {
        if (text == name) {
            return kind;
        }
    }
    return std::nullopt;
}

std::string_view transitionKindName(TransitionKind kind)
{
    for (const auto &[value, text] : kNames) {
        if (value == kind) {
            return text;
        }
    }
    return {};
}

double ease(Easing easing, double t)
{
    t = std::clamp(t, 0.0, 1.0);
    switch (easing) {
    case Easing::Linear:
        return t;
    case Easing::EaseIn:
        return t * t * t;
    case Easing::EaseOut:
        return 1.0 - std::pow(1.0 - t, 3);
    case Easing::EaseInOut:
        return t < 0.5 ? 4.0 * t * t * t : 1.0 - std::pow(-2.0 * t + 2.0, 3) / 2.0;
    }
    return t;
}

void renderTransition(TransitionKind kind, ImageView out, ConstImageView a, ConstImageView b, double progress,
                      const TransitionParams &params, int rowBegin, int rowEnd)
{
    const float t = static_cast<float>(std::clamp(progress, 0.0, 1.0));
    const int w = out.width;
    const int h = out.height;

    if (t <= 0.0f) {
        for (int y = std::max(0, rowBegin); y < std::min(rowEnd, h); ++y) {
            std::uint8_t *d = out.row(y);
            for (int x = 0; x < w; ++x, d += 4) {
                store(d, load(a, x, y));
            }
        }
        return;
    }
    if (t >= 1.0f) {
        for (int y = std::max(0, rowBegin); y < std::min(rowEnd, h); ++y) {
            std::uint8_t *d = out.row(y);
            for (int x = 0; x < w; ++x, d += 4) {
                store(d, load(b, x, y));
            }
        }
        return;
    }

    const float soft = static_cast<float>(std::max(0.0001, params.softness));
    const Pixel black{0, 0, 0, 1};
    const Pixel white{1, 1, 1, 1};
    const Pixel customColor{params.color[0] * params.color[3], params.color[1] * params.color[3],
                            params.color[2] * params.color[3], params.color[3]};
    const float cx = w / 2.0f;
    const float cy = h / 2.0f;
    const float maxRadius = std::sqrt(cx * cx + cy * cy);
    const float pi = 3.14159265358979323846f;

    for (int y = std::max(0, rowBegin); y < std::min(rowEnd, h); ++y) {
        std::uint8_t *d = out.row(y);
        for (int x = 0; x < w; ++x, d += 4) {
            Pixel p;
            const Pixel pixA = load(a, x, y);
            const Pixel pixB = load(b, x, y);

            switch (kind) {
            // ---- 1. Base -------------------------------------------------------------
            case TransitionKind::Dissolve:
                p = mix(pixA, pixB, t);
                break;
            case TransitionKind::DipToBlack:
                p = t < 0.5f ? mix(pixA, black, t * 2.0f) : mix(black, pixB, (t - 0.5f) * 2.0f);
                break;
            case TransitionKind::DipToWhite:
                p = t < 0.5f ? mix(pixA, white, t * 2.0f) : mix(white, pixB, (t - 0.5f) * 2.0f);
                break;
            case TransitionKind::DipToColor:
                p = t < 0.5f ? mix(pixA, customColor, t * 2.0f) : mix(customColor, pixB, (t - 0.5f) * 2.0f);
                break;
            case TransitionKind::FadeGrayscale: {
                const Pixel gA = grayscale(pixA);
                const Pixel gB = grayscale(pixB);
                p = t < 0.5f ? mix(mix(pixA, gA, t * 2.0f), gB, t) : mix(gA, mix(gB, pixB, (t - 0.5f) * 2.0f), t);
                break;
            }
            case TransitionKind::ExposureFlash: {
                const float flash = 1.0f + 2.5f * (1.0f - std::abs(2.0f * t - 1.0f));
                Pixel mixed = mix(pixA, pixB, t);
                p = {std::min(mixed.a, mixed.r * flash), std::min(mixed.a, mixed.g * flash),
                     std::min(mixed.a, mixed.b * flash), mixed.a};
                break;
            }
            case TransitionKind::LumaFade: {
                const float lum = 0.2126f * pixA.r + 0.7152f * pixA.g + 0.0722f * pixA.b;
                const float edge = t * (1.0f + 2.0f * soft) - soft;
                p = mix(pixA, pixB, smooth(edge - soft, edge + soft, lum));
                break;
            }
            case TransitionKind::Additive: {
                const float alpha = std::min(1.0f, pixA.a * (1.0f - t) + pixB.a * t);
                p = {std::min(alpha, pixA.r * (1.0f - t) + pixB.r * t + (pixA.r + pixB.r) * 0.25f * (1.0f - std::abs(2.0f * t - 1.0f))),
                     std::min(alpha, pixA.g * (1.0f - t) + pixB.g * t + (pixA.g + pixB.g) * 0.25f * (1.0f - std::abs(2.0f * t - 1.0f))),
                     std::min(alpha, pixA.b * (1.0f - t) + pixB.b * t + (pixA.b + pixB.b) * 0.25f * (1.0f - std::abs(2.0f * t - 1.0f))),
                     alpha};
                break;
            }
            case TransitionKind::Subtract: {
                Pixel mixed = mix(pixA, pixB, t);
                const float sub = 0.3f * std::sin(t * pi);
                p = {std::max(0.0f, mixed.r - sub * mixed.a), std::max(0.0f, mixed.g - sub * mixed.a),
                     std::max(0.0f, mixed.b - sub * mixed.a), mixed.a};
                break;
            }
            case TransitionKind::Multiply: {
                Pixel mult{pixA.r * pixB.r / std::max(0.01f, pixA.a * pixB.a),
                           pixA.g * pixB.g / std::max(0.01f, pixA.a * pixB.a),
                           pixA.b * pixB.b / std::max(0.01f, pixA.a * pixB.a),
                           std::min(pixA.a, pixB.a)};
                p = t < 0.5f ? mix(pixA, mult, t * 2.0f) : mix(mult, pixB, (t - 0.5f) * 2.0f);
                break;
            }

            // ---- 2. Motion (Slide & Push) --------------------------------------------
            case TransitionKind::SlideLeft:
            case TransitionKind::SlideRight:
            case TransitionKind::SlideUp:
            case TransitionKind::SlideDown:
            case TransitionKind::SlideTopLeft:
            case TransitionKind::SlideTopRight:
            case TransitionKind::SlideBottomLeft:
            case TransitionKind::SlideBottomRight: {
                float dx = 0.0f;
                float dy = 0.0f;
                if (kind == TransitionKind::SlideLeft) dx = (1.0f - t) * w;
                else if (kind == TransitionKind::SlideRight) dx = -(1.0f - t) * w;
                else if (kind == TransitionKind::SlideUp) dy = (1.0f - t) * h;
                else if (kind == TransitionKind::SlideDown) dy = -(1.0f - t) * h;
                else if (kind == TransitionKind::SlideTopLeft) { dx = (1.0f - t) * w; dy = (1.0f - t) * h; }
                else if (kind == TransitionKind::SlideTopRight) { dx = -(1.0f - t) * w; dy = (1.0f - t) * h; }
                else if (kind == TransitionKind::SlideBottomLeft) { dx = (1.0f - t) * w; dy = -(1.0f - t) * h; }
                else if (kind == TransitionKind::SlideBottomRight) { dx = -(1.0f - t) * w; dy = -(1.0f - t) * h; }

                const float bx = x + 0.5f - dx;
                const float by = y + 0.5f - dy;
                if (bx >= 0.0f && bx < w && by >= 0.0f && by < h) {
                    Pixel sampB = sample(b, bx, by);
                    p = sampB.a >= 1.0f ? sampB : mix(pixA, sampB, sampB.a);
                } else {
                    p = pixA;
                }
                break;
            }
            case TransitionKind::PushLeft:
            case TransitionKind::PushRight:
            case TransitionKind::PushUp:
            case TransitionKind::PushDown:
            case TransitionKind::PushTopLeft:
            case TransitionKind::PushTopRight:
            case TransitionKind::PushBottomLeft:
            case TransitionKind::PushBottomRight: {
                float dx = 0.0f;
                float dy = 0.0f;
                if (kind == TransitionKind::PushLeft) dx = (1.0f - t) * w;
                else if (kind == TransitionKind::PushRight) dx = -(1.0f - t) * w;
                else if (kind == TransitionKind::PushUp) dy = (1.0f - t) * h;
                else if (kind == TransitionKind::PushDown) dy = -(1.0f - t) * h;
                else if (kind == TransitionKind::PushTopLeft) { dx = (1.0f - t) * w; dy = (1.0f - t) * h; }
                else if (kind == TransitionKind::PushTopRight) { dx = -(1.0f - t) * w; dy = (1.0f - t) * h; }
                else if (kind == TransitionKind::PushBottomLeft) { dx = (1.0f - t) * w; dy = -(1.0f - t) * h; }
                else if (kind == TransitionKind::PushBottomRight) { dx = -(1.0f - t) * w; dy = -(1.0f - t) * h; }

                const float bx = x + 0.5f - dx;
                const float by = y + 0.5f - dy;
                if (bx >= 0.0f && bx < w && by >= 0.0f && by < h) {
                    p = sample(b, bx, by);
                } else {
                    const float signX = (dx > 0.0f) ? 1.0f : (dx < 0.0f ? -1.0f : 0.0f);
                    const float signY = (dy > 0.0f) ? 1.0f : (dy < 0.0f ? -1.0f : 0.0f);
                    const float ax = x + 0.5f + (1.0f - (1.0f - t)) * w * signX;
                    const float ay = y + 0.5f + (1.0f - (1.0f - t)) * h * signY;
                    p = sample(a, ax, ay);
                }
                break;
            }

            // ---- 3. Zoom & Spin ------------------------------------------------------
            case TransitionKind::ZoomIn: {
                const float zoom = 1.0f + t * 2.0f;
                const Pixel zoomed = sample(a, cx + (x + 0.5f - cx) / zoom, cy + (y + 0.5f - cy) / zoom);
                p = mix(zoomed, pixB, smooth(0.2f, 1.0f, t));
                break;
            }
            case TransitionKind::ZoomOut: {
                const float zoom = 1.0f + (1.0f - t) * 2.0f;
                const Pixel zoomed = sample(b, cx + (x + 0.5f - cx) / zoom, cy + (y + 0.5f - cy) / zoom);
                p = mix(pixA, zoomed, smooth(0.0f, 0.8f, t));
                break;
            }
            case TransitionKind::CrossZoom: {
                const float zA = 1.0f + t * 1.5f;
                const float zB = 1.0f + (1.0f - t) * 1.5f;
                const Pixel sampA = sample(a, cx + (x + 0.5f - cx) / zA, cy + (y + 0.5f - cy) / zA);
                const Pixel sampB = sample(b, cx + (x + 0.5f - cx) / zB, cy + (y + 0.5f - cy) / zB);
                p = mix(sampA, sampB, t);
                break;
            }
            case TransitionKind::WarpZoom: {
                const float dist = std::sqrt((x + 0.5f - cx) * (x + 0.5f - cx) + (y + 0.5f - cy) * (y + 0.5f - cy)) / maxRadius;
                const float warp = 1.0f + t * dist * 3.0f;
                const Pixel sampA = sample(a, cx + (x + 0.5f - cx) / warp, cy + (y + 0.5f - cy) / warp);
                p = mix(sampA, pixB, smooth(0.3f, 1.0f, t));
                break;
            }
            case TransitionKind::SpinCw:
            case TransitionKind::SpinCcw: {
                const float angle = (kind == TransitionKind::SpinCw ? 1.0f : -1.0f) * t * 2.0f * pi;
                const float cosA = std::cos(angle);
                const float sinA = std::sin(angle);
                const float rx = (x + 0.5f - cx) * cosA - (y + 0.5f - cy) * sinA + cx;
                const float ry = (x + 0.5f - cx) * sinA + (y + 0.5f - cy) * cosA + cy;
                p = t < 0.5f ? mix(sample(a, rx, ry), pixB, t * 2.0f) : mix(pixA, sample(b, rx, ry), (t - 0.5f) * 2.0f);
                break;
            }
            case TransitionKind::SpinZoomIn:
            case TransitionKind::SpinZoomOut: {
                const float factor = (kind == TransitionKind::SpinZoomIn) ? (1.0f + t * 2.0f) : (1.0f + (1.0f - t) * 2.0f);
                const float angle = t * pi;
                const float cosA = std::cos(angle) / factor;
                const float sinA = std::sin(angle) / factor;
                const float rx = (x + 0.5f - cx) * cosA - (y + 0.5f - cy) * sinA + cx;
                const float ry = (x + 0.5f - cx) * sinA + (y + 0.5f - cy) * cosA + cy;
                p = mix(sample(a, rx, ry), pixB, t);
                break;
            }
            case TransitionKind::RotateScaleCw:
            case TransitionKind::RotateScaleCcw: {
                const float dir = (kind == TransitionKind::RotateScaleCw) ? 1.0f : -1.0f;
                const float angle = dir * t * pi * 0.5f;
                const float scale = 1.0f + t * 0.5f;
                const float cosA = std::cos(angle) / scale;
                const float sinA = std::sin(angle) / scale;
                const float rx = (x + 0.5f - cx) * cosA - (y + 0.5f - cy) * sinA + cx;
                const float ry = (x + 0.5f - cx) * sinA + (y + 0.5f - cy) * cosA + cy;
                p = mix(sample(a, rx, ry), pixB, t);
                break;
            }
            case TransitionKind::DollyZoomIn:
            case TransitionKind::DollyZoomOut: {
                const float d = std::sqrt((x + 0.5f - cx) * (x + 0.5f - cx) + (y + 0.5f - cy) * (y + 0.5f - cy)) / maxRadius;
                const float f = (kind == TransitionKind::DollyZoomIn) ? (1.0f + t * (1.0f - d) * 2.0f) : (1.0f + (1.0f - t) * (1.0f - d) * 2.0f);
                p = mix(sample(a, cx + (x + 0.5f - cx) / f, cy + (y + 0.5f - cy) / f), pixB, t);
                break;
            }

            // ---- 4. Wipes & Bands ----------------------------------------------------
            case TransitionKind::WipeLeft:
            case TransitionKind::WipeRight:
            case TransitionKind::WipeUp:
            case TransitionKind::WipeDown:
            case TransitionKind::WipeTopLeft:
            case TransitionKind::WipeTopRight:
            case TransitionKind::WipeBottomLeft:
            case TransitionKind::WipeBottomRight: {
                float s = 0.0f;
                if (kind == TransitionKind::WipeLeft) s = 1.0f - (x + 0.5f) / w;
                else if (kind == TransitionKind::WipeRight) s = (x + 0.5f) / w;
                else if (kind == TransitionKind::WipeUp) s = 1.0f - (y + 0.5f) / h;
                else if (kind == TransitionKind::WipeDown) s = (y + 0.5f) / h;
                else if (kind == TransitionKind::WipeTopLeft) s = 1.0f - 0.5f * ((x + 0.5f) / w + (y + 0.5f) / h);
                else if (kind == TransitionKind::WipeTopRight) s = 0.5f * ((x + 0.5f) / w + 1.0f - (y + 0.5f) / h);
                else if (kind == TransitionKind::WipeBottomLeft) s = 0.5f * (1.0f - (x + 0.5f) / w + (y + 0.5f) / h);
                else if (kind == TransitionKind::WipeBottomRight) s = 0.5f * ((x + 0.5f) / w + (y + 0.5f) / h);

                const float edge = t * (1.0f + 2.0f * soft) - soft;
                p = mix(pixA, pixB, 1.0f - smooth(edge - soft, edge + soft, s));
                break;
            }
            case TransitionKind::SplitHorizontal: {
                const float s = std::abs((y + 0.5f - cy) / cy);
                const float edge = t * (1.0f + 2.0f * soft) - soft;
                p = mix(pixA, pixB, 1.0f - smooth(edge - soft, edge + soft, s));
                break;
            }
            case TransitionKind::SplitVertical: {
                const float s = std::abs((x + 0.5f - cx) / cx);
                const float edge = t * (1.0f + 2.0f * soft) - soft;
                p = mix(pixA, pixB, 1.0f - smooth(edge - soft, edge + soft, s));
                break;
            }
            case TransitionKind::BarnDoorHorizontal: {
                const float s = 1.0f - std::abs((y + 0.5f - cy) / cy);
                const float edge = t * (1.0f + 2.0f * soft) - soft;
                p = mix(pixA, pixB, 1.0f - smooth(edge - soft, edge + soft, s));
                break;
            }
            case TransitionKind::BarnDoorVertical: {
                const float s = 1.0f - std::abs((x + 0.5f - cx) / cx);
                const float edge = t * (1.0f + 2.0f * soft) - soft;
                p = mix(pixA, pixB, 1.0f - smooth(edge - soft, edge + soft, s));
                break;
            }
            case TransitionKind::BlindsHorizontal: {
                const float s = std::fmod((y + 0.5f) / 16.0f, 1.0f);
                const float edge = t * (1.0f + 2.0f * soft) - soft;
                p = mix(pixA, pixB, 1.0f - smooth(edge - soft, edge + soft, s));
                break;
            }
            case TransitionKind::BlindsVertical: {
                const float s = std::fmod((x + 0.5f) / 16.0f, 1.0f);
                const float edge = t * (1.0f + 2.0f * soft) - soft;
                p = mix(pixA, pixB, 1.0f - smooth(edge - soft, edge + soft, s));
                break;
            }
            case TransitionKind::Checkerboard: {
                const int bx = x / 24;
                const int by = y / 24;
                const float s = (bx + by) % 2 == 0 ? 0.0f : 1.0f;
                p = (t < 0.5f) ? (s > 0.5f ? mix(pixA, pixB, t * 2.0f) : pixA)
                               : (s > 0.5f ? pixB : mix(pixA, pixB, (t - 0.5f) * 2.0f));
                break;
            }
            case TransitionKind::MosaicWipe: {
                const int bx = x / 16;
                const int by = y / 16;
                const float randVal = pseudoRandom(bx, by);
                p = mix(pixA, pixB, smooth(randVal - 0.1f, randVal + 0.1f, t));
                break;
            }

            // ---- 5. Shapes & Geometry ------------------------------------------------
            case TransitionKind::Iris: {
                const float r = std::sqrt((x + 0.5f - cx) * (x + 0.5f - cx) + (y + 0.5f - cy) * (y + 0.5f - cy)) / maxRadius;
                const float edge = t * (1.0f + 2.0f * soft) - soft;
                p = mix(pixA, pixB, 1.0f - smooth(edge - soft, edge + soft, r));
                break;
            }
            case TransitionKind::IrisDiamond: {
                const float d = (std::abs(x + 0.5f - cx) / cx + std::abs(y + 0.5f - cy) / cy) * 0.5f;
                const float edge = t * (1.0f + 2.0f * soft) - soft;
                p = mix(pixA, pixB, 1.0f - smooth(edge - soft, edge + soft, d));
                break;
            }
            case TransitionKind::IrisStar: {
                const float angle = std::atan2(y + 0.5f - cy, x + 0.5f - cx);
                const float r = std::sqrt((x + 0.5f - cx) * (x + 0.5f - cx) + (y + 0.5f - cy) * (y + 0.5f - cy)) / maxRadius;
                const float starFactor = 0.7f + 0.3f * std::cos(5.0f * angle);
                const float dist = r / starFactor;
                const float edge = t * (1.0f + 2.0f * soft) - soft;
                p = mix(pixA, pixB, 1.0f - smooth(edge - soft, edge + soft, dist));
                break;
            }
            case TransitionKind::IrisHeart: {
                const float nx = (x + 0.5f - cx) / cx;
                const float ny = -(y + 0.5f - cy) / cy;
                const float dist = std::sqrt(nx * nx + ny * ny) - 0.3f * std::sqrt(std::abs(nx));
                const float edge = t * 1.5f - 0.2f;
                p = mix(pixA, pixB, smooth(edge - soft, edge + soft, 1.0f - dist));
                break;
            }
            case TransitionKind::IrisTriangle: {
                const float nx = std::abs((x + 0.5f - cx) / cx);
                const float ny = (y + 0.5f - cy) / cy;
                const float d = std::max(nx * 0.866025f + ny * 0.5f, -ny);
                const float edge = t * (1.0f + 2.0f * soft) - soft;
                p = mix(pixA, pixB, 1.0f - smooth(edge - soft, edge + soft, d));
                break;
            }
            case TransitionKind::Clock: {
                float angle = std::atan2(x + 0.5f - cx, cy - (y + 0.5f)) / (2.0f * pi);
                if (angle < 0.0f) angle += 1.0f;
                const float edge = t * (1.0f + 2.0f * soft) - soft;
                p = mix(pixA, pixB, 1.0f - smooth(edge - soft, edge + soft, angle));
                break;
            }
            case TransitionKind::ClockCounter: {
                float angle = std::atan2(cx - (x + 0.5f), cy - (y + 0.5f)) / (2.0f * pi);
                if (angle < 0.0f) angle += 1.0f;
                const float edge = t * (1.0f + 2.0f * soft) - soft;
                p = mix(pixA, pixB, 1.0f - smooth(edge - soft, edge + soft, angle));
                break;
            }
            case TransitionKind::ClockDual: {
                float angle = std::abs(std::atan2(x + 0.5f - cx, cy - (y + 0.5f))) / pi;
                const float edge = t * (1.0f + 2.0f * soft) - soft;
                p = mix(pixA, pixB, 1.0f - smooth(edge - soft, edge + soft, angle));
                break;
            }
            case TransitionKind::HexagonGrid: {
                const int hx = static_cast<int>((x + 0.5f) / 28.0f);
                const int hy = static_cast<int>((y + 0.5f) / 24.0f);
                const float r = pseudoRandom(hx, hy);
                p = mix(pixA, pixB, smooth(r - 0.15f, r + 0.15f, t));
                break;
            }
            case TransitionKind::PolygonOpen: {
                const float nx = std::abs(x + 0.5f - cx) / cx;
                const float ny = std::abs(y + 0.5f - cy) / cy;
                const float poly = std::max({nx, ny, (nx + ny) * 0.7071f});
                const float edge = t * (1.0f + 2.0f * soft) - soft;
                p = mix(pixA, pixB, 1.0f - smooth(edge - soft, edge + soft, poly));
                break;
            }
            case TransitionKind::DiagonalSliceLeft:
            case TransitionKind::DiagonalSliceRight: {
                const float dir = (kind == TransitionKind::DiagonalSliceLeft) ? 1.0f : -1.0f;
                const int stripe = static_cast<int>((x + dir * y) / 32.0f);
                const float offset = ((stripe % 2 == 0) ? 1.0f : -1.0f) * (1.0f - t) * w;
                const float bx = x + 0.5f - offset;
                if (bx >= 0.0f && bx < w) {
                    p = mix(pixA, sample(b, bx, y + 0.5f), t);
                } else {
                    p = pixA;
                }
                break;
            }

            // ---- 6. Blurs & Camera ---------------------------------------------------
            case TransitionKind::BlurDissolve: {
                const float blurDist = std::sin(t * pi) * 12.0f;
                Pixel bA = mix(mix(sample(a, x - blurDist, y), sample(a, x + blurDist, y), 0.5f),
                               mix(sample(a, x, y - blurDist), sample(a, x, y + blurDist), 0.5f), 0.5f);
                Pixel bB = mix(mix(sample(b, x - blurDist, y), sample(b, x + blurDist, y), 0.5f),
                               mix(sample(b, x, y - blurDist), sample(b, x, y + blurDist), 0.5f), 0.5f);
                p = mix(bA, bB, t);
                break;
            }
            case TransitionKind::DirectionalBlurLeft:
            case TransitionKind::DirectionalBlurRight: {
                const float dir = (kind == TransitionKind::DirectionalBlurLeft) ? -1.0f : 1.0f;
                const float blurDist = dir * std::sin(t * pi) * 20.0f;
                Pixel bA = mix(sample(a, x - blurDist, y), sample(a, x + blurDist, y), 0.5f);
                Pixel bB = mix(sample(b, x - blurDist, y), sample(b, x + blurDist, y), 0.5f);
                p = mix(bA, bB, t);
                break;
            }
            case TransitionKind::RadialBlur: {
                const float blurDist = std::sin(t * pi) * 0.15f;
                Pixel bA = mix(sample(a, cx + (x - cx) * (1.0f - blurDist), cy + (y - cy) * (1.0f - blurDist)),
                               sample(a, cx + (x - cx) * (1.0f + blurDist), cy + (y - cy) * (1.0f + blurDist)), 0.5f);
                Pixel bB = mix(sample(b, cx + (x - cx) * (1.0f - blurDist), cy + (y - cy) * (1.0f - blurDist)),
                               sample(b, cx + (x - cx) * (1.0f + blurDist), cy + (y - cy) * (1.0f + blurDist)), 0.5f);
                p = mix(bA, bB, t);
                break;
            }
            case TransitionKind::TiltShift: {
                const float blurDist = std::abs((y - cy) / cy) * std::sin(t * pi) * 10.0f;
                Pixel bA = mix(sample(a, x, y - blurDist), sample(a, x, y + blurDist), 0.5f);
                Pixel bB = mix(sample(b, x, y - blurDist), sample(b, x, y + blurDist), 0.5f);
                p = mix(bA, bB, t);
                break;
            }
            case TransitionKind::WhipPanLeft:
            case TransitionKind::WhipPanRight:
            case TransitionKind::WhipPanUp:
            case TransitionKind::WhipPanDown: {
                const float blur = std::sin(t * pi) * 30.0f;
                float ox = 0.0f;
                float oy = 0.0f;
                if (kind == TransitionKind::WhipPanLeft) ox = (1.0f - t) * w;
                else if (kind == TransitionKind::WhipPanRight) ox = -(1.0f - t) * w;
                else if (kind == TransitionKind::WhipPanUp) oy = (1.0f - t) * h;
                else oy = -(1.0f - t) * h;

                Pixel sA = mix(sample(a, x + 0.5f - blur, y + 0.5f), sample(a, x + 0.5f + blur, y + 0.5f), 0.5f);
                Pixel sB = mix(sample(b, x + 0.5f - ox - blur, y + 0.5f - oy), sample(b, x + 0.5f - ox + blur, y + 0.5f - oy), 0.5f);
                p = mix(sA, sB, t);
                break;
            }
            case TransitionKind::CameraShutter: {
                const float blade = 1.0f - std::abs(2.0f * t - 1.0f);
                const float r = std::sqrt((x + 0.5f - cx) * (x + 0.5f - cx) + (y + 0.5f - cy) * (y + 0.5f - cy)) / maxRadius;
                if (r > (1.0f - blade * 0.8f)) {
                    p = black;
                } else {
                    p = mix(pixA, pixB, t);
                }
                break;
            }

            // ---- 7. Glitch & Digital -------------------------------------------------
            case TransitionKind::GlitchRgb: {
                const float shift = std::sin(t * pi) * 16.0f;
                Pixel rSample = mix(sample(a, x + shift, y), sample(b, x + shift, y), t);
                Pixel bSample = mix(sample(a, x - shift, y), sample(b, x - shift, y), t);
                Pixel center = mix(pixA, pixB, t);
                p = {rSample.r, center.g, bSample.b, center.a};
                break;
            }
            case TransitionKind::Pixelate: {
                const float strength = std::sin(t * pi) * 20.0f;
                const int bs = std::max(1, static_cast<int>(strength));
                const int px = (x / bs) * bs + bs / 2;
                const int py = (y / bs) * bs + bs / 2;
                p = mix(sample(a, px, py), sample(b, px, py), t);
                break;
            }
            case TransitionKind::ScanlineTear: {
                const float shift = (y % 4 == 0) ? std::sin(t * pi) * 15.0f : 0.0f;
                p = mix(sample(a, x + shift, y), sample(b, x - shift, y), t);
                break;
            }
            case TransitionKind::VhsDistortion: {
                const float noise = (pseudoRandom(x, y) - 0.5f) * std::sin(t * pi) * 0.2f;
                const float wave = std::sin(y * 0.1f + t * 10.0f) * std::sin(t * pi) * 8.0f;
                Pixel samp = mix(sample(a, x + wave, y), sample(b, x, y), t);
                p = {std::clamp(samp.r + noise, 0.0f, samp.a), std::clamp(samp.g + noise, 0.0f, samp.a),
                     std::clamp(samp.b + noise, 0.0f, samp.a), samp.a};
                break;
            }
            case TransitionKind::BlockDissolve: {
                const int bx = x / 20;
                const int by = y / 20;
                const float thresh = pseudoRandom(bx, by);
                p = t > thresh ? pixB : pixA;
                break;
            }
            case TransitionKind::DigitalNoise: {
                const float noise = pseudoRandom(x, y);
                p = mix(pixA, pixB, smooth(noise - 0.2f, noise + 0.2f, t));
                break;
            }
            case TransitionKind::Jitter: {
                const float jx = (pseudoRandom(x / 4, y / 4) - 0.5f) * std::sin(t * pi) * 12.0f;
                const float jy = (pseudoRandom(y / 4, x / 4) - 0.5f) * std::sin(t * pi) * 12.0f;
                p = mix(sample(a, x + jx, y + jy), sample(b, x - jx, y - jy), t);
                break;
            }
            case TransitionKind::DataCorruption: {
                const int bx = x / 32;
                const int by = y / 32;
                const float r = pseudoRandom(bx, by);
                if (std::sin(t * pi) > 0.5f && r > 0.7f) {
                    p = {pixA.g, pixA.b, pixA.r, pixA.a};
                } else {
                    p = mix(pixA, pixB, t);
                }
                break;
            }
            case TransitionKind::ScreenTear: {
                const float splitY = h * t;
                p = (y < splitY) ? pixB : pixA;
                break;
            }
            case TransitionKind::LumaGlitch: {
                const float lum = 0.2126f * pixA.r + 0.7152f * pixA.g + 0.0722f * pixA.b;
                const float shift = (lum > 0.6f) ? std::sin(t * pi) * 25.0f : 0.0f;
                p = mix(sample(a, x + shift, y), pixB, t);
                break;
            }

            // ---- 8. Lights & Leaks ---------------------------------------------------
            case TransitionKind::LightLeakWarm: {
                const float leak = std::sin(t * pi) * 0.8f;
                Pixel mixed = mix(pixA, pixB, t);
                p = {std::min(mixed.a, mixed.r + leak * 0.9f * mixed.a),
                     std::min(mixed.a, mixed.g + leak * 0.5f * mixed.a),
                     std::min(mixed.a, mixed.b + leak * 0.2f * mixed.a), mixed.a};
                break;
            }
            case TransitionKind::LightLeakCool: {
                const float leak = std::sin(t * pi) * 0.8f;
                Pixel mixed = mix(pixA, pixB, t);
                p = {std::min(mixed.a, mixed.r + leak * 0.2f * mixed.a),
                     std::min(mixed.a, mixed.g + leak * 0.6f * mixed.a),
                     std::min(mixed.a, mixed.b + leak * 0.9f * mixed.a), mixed.a};
                break;
            }
            case TransitionKind::LensGlow: {
                const float glow = std::sin(t * pi) * 1.5f;
                Pixel mixed = mix(pixA, pixB, t);
                p = {std::min(mixed.a, mixed.r * (1.0f + glow)), std::min(mixed.a, mixed.g * (1.0f + glow)),
                     std::min(mixed.a, mixed.b * (1.0f + glow)), mixed.a};
                break;
            }
            case TransitionKind::FilmBurn: {
                const float burn = std::sin(t * pi);
                const float edge = pseudoRandom(x / 8, y / 8) * burn;
                Pixel mixed = mix(pixA, pixB, t);
                if (edge > 0.4f) {
                    p = {mixed.a, mixed.a * 0.8f, mixed.a * 0.3f, mixed.a};
                } else {
                    p = mixed;
                }
                break;
            }
            case TransitionKind::RainbowFlash: {
                const float flash = std::sin(t * pi);
                const float hue = std::fmod((x + 0.5f) / w + t, 1.0f);
                Pixel mixed = mix(pixA, pixB, t);
                p = {std::min(mixed.a, mixed.r + flash * std::abs(std::sin(hue * pi))),
                     std::min(mixed.a, mixed.g + flash * std::abs(std::sin((hue + 0.33f) * pi))),
                     std::min(mixed.a, mixed.b + flash * std::abs(std::sin((hue + 0.66f) * pi))), mixed.a};
                break;
            }
            case TransitionKind::ColorInvert: {
                const float inv = std::sin(t * pi);
                Pixel mixed = mix(pixA, pixB, t);
                const auto flerp = [](float u, float v, float factor) { return u + (v - u) * factor; };
                p = {flerp(mixed.r, mixed.a - mixed.r, inv), flerp(mixed.g, mixed.a - mixed.g, inv),
                     flerp(mixed.b, mixed.a - mixed.b, inv), mixed.a};
                break;
            }
            case TransitionKind::Solarize: {
                const float sol = std::sin(t * pi);
                Pixel mixed = mix(pixA, pixB, t);
                const auto curve = [sol](float v, float a) {
                    float norm = a > 0 ? v / a : 0;
                    return (norm > 0.5f ? (1.0f - norm) * 2.0f : norm * 2.0f) * a * sol + v * (1.0f - sol);
                };
                p = {curve(mixed.r, mixed.a), curve(mixed.g, mixed.a), curve(mixed.b, mixed.a), mixed.a};
                break;
            }
            case TransitionKind::NeonFlash: {
                const float neon = std::sin(t * pi) * 0.6f;
                Pixel mixed = mix(pixA, pixB, t);
                p = {std::min(mixed.a, mixed.r + neon * 0.1f * mixed.a),
                     std::min(mixed.a, mixed.g + neon * 0.9f * mixed.a),
                     std::min(mixed.a, mixed.b + neon * 0.9f * mixed.a), mixed.a};
                break;
            }
            case TransitionKind::Strobe: {
                const bool flash = (static_cast<int>(t * 12.0f) % 2 == 1);
                p = flash ? (t < 0.5f ? white : black) : mix(pixA, pixB, t);
                break;
            }
            case TransitionKind::LumaGlow: {
                const float glow = std::sin(t * pi) * 0.7f;
                Pixel mixed = mix(pixA, pixB, t);
                const float lum = 0.2126f * mixed.r + 0.7152f * mixed.g + 0.0722f * mixed.b;
                p = {std::min(mixed.a, mixed.r + lum * glow), std::min(mixed.a, mixed.g + lum * glow),
                     std::min(mixed.a, mixed.b + lum * glow), mixed.a};
                break;
            }

            // ---- 9. Distortions ------------------------------------------------------
            case TransitionKind::WaveHorizontal: {
                const float wave = std::sin(y * 0.05f + t * 4.0f * pi) * std::sin(t * pi) * 20.0f;
                p = mix(sample(a, x + wave, y), sample(b, x - wave, y), t);
                break;
            }
            case TransitionKind::WaveVertical: {
                const float wave = std::sin(x * 0.05f + t * 4.0f * pi) * std::sin(t * pi) * 20.0f;
                p = mix(sample(a, x, y + wave), sample(b, x, y - wave), t);
                break;
            }
            case TransitionKind::RippleWater: {
                const float dist = std::sqrt((x + 0.5f - cx) * (x + 0.5f - cx) + (y + 0.5f - cy) * (y + 0.5f - cy));
                const float ripple = std::sin(dist * 0.1f - t * 10.0f) * std::sin(t * pi) * 10.0f;
                p = mix(sample(a, x + ripple, y + ripple), sample(b, x - ripple, y - ripple), t);
                break;
            }
            case TransitionKind::Vortex: {
                const float dist = std::sqrt((x + 0.5f - cx) * (x + 0.5f - cx) + (y + 0.5f - cy) * (y + 0.5f - cy)) / maxRadius;
                const float angle = (1.0f - dist) * std::sin(t * pi) * pi;
                const float cosA = std::cos(angle);
                const float sinA = std::sin(angle);
                const float rx = (x + 0.5f - cx) * cosA - (y + 0.5f - cy) * sinA + cx;
                const float ry = (x + 0.5f - cx) * sinA + (y + 0.5f - cy) * cosA + cy;
                p = mix(sample(a, rx, ry), sample(b, x, y), t);
                break;
            }
            case TransitionKind::Shockwave: {
                const float ring = t * maxRadius;
                const float dist = std::sqrt((x + 0.5f - cx) * (x + 0.5f - cx) + (y + 0.5f - cy) * (y + 0.5f - cy));
                const float wave = std::sin(std::abs(dist - ring) * 0.1f) * std::max(0.0f, 1.0f - std::abs(dist - ring) / 30.0f) * 15.0f;
                p = mix(sample(a, x + wave, y + wave), pixB, t);
                break;
            }
            case TransitionKind::Pinch: {
                const float r = std::sqrt((x + 0.5f - cx) * (x + 0.5f - cx) + (y + 0.5f - cy) * (y + 0.5f - cy)) / maxRadius;
                const float f = 1.0f + std::sin(t * pi) * (1.0f - r) * 0.8f;
                p = mix(sample(a, cx + (x + 0.5f - cx) * f, cy + (y + 0.5f - cy) * f), pixB, t);
                break;
            }
            case TransitionKind::PinchTwist: {
                const float r = std::sqrt((x + 0.5f - cx) * (x + 0.5f - cx) + (y + 0.5f - cy) * (y + 0.5f - cy)) / maxRadius;
                const float angle = (1.0f - r) * std::sin(t * pi) * pi * 0.5f;
                const float scale = 1.0f + std::sin(t * pi) * (1.0f - r) * 0.5f;
                const float cosA = std::cos(angle) * scale;
                const float sinA = std::sin(angle) * scale;
                const float rx = (x + 0.5f - cx) * cosA - (y + 0.5f - cy) * sinA + cx;
                const float ry = (x + 0.5f - cx) * sinA + (y + 0.5f - cy) * cosA + cy;
                p = mix(sample(a, rx, ry), pixB, t);
                break;
            }
            case TransitionKind::SphereLens: {
                const float nx = (x + 0.5f - cx) / cx;
                const float ny = (y + 0.5f - cy) / cy;
                const float r2 = nx * nx + ny * ny;
                const float lens = 1.0f + std::sin(t * pi) * (1.0f - std::min(1.0f, r2)) * 0.5f;
                p = mix(sample(a, cx + (x + 0.5f - cx) / lens, cy + (y + 0.5f - cy) / lens), pixB, t);
                break;
            }
            case TransitionKind::Swirl: {
                const float r = std::sqrt((x + 0.5f - cx) * (x + 0.5f - cx) + (y + 0.5f - cy) * (y + 0.5f - cy));
                const float angle = r * 0.02f * std::sin(t * pi);
                const float cosA = std::cos(angle);
                const float sinA = std::sin(angle);
                const float rx = (x + 0.5f - cx) * cosA - (y + 0.5f - cy) * sinA + cx;
                const float ry = (x + 0.5f - cx) * sinA + (y + 0.5f - cy) * cosA + cy;
                p = mix(sample(a, rx, ry), pixB, t);
                break;
            }
            case TransitionKind::Kaleidoscope: {
                const float angle = std::atan2(y + 0.5f - cy, x + 0.5f - cx);
                const float r = std::sqrt((x + 0.5f - cx) * (x + 0.5f - cx) + (y + 0.5f - cy) * (y + 0.5f - cy));
                const float segment = pi / 3.0f;
                const float symAngle = std::abs(std::fmod(angle, segment) - segment * 0.5f);
                const float kx = cx + r * std::cos(symAngle);
                const float ky = cy + r * std::sin(symAngle);
                p = mix(mix(pixA, sample(a, kx, ky), std::sin(t * pi)), pixB, t);
                break;
            }

            // ---- 10. 3D Transitions --------------------------------------------------
            case TransitionKind::CubeLeft:
            case TransitionKind::CubeRight: {
                const float dir = (kind == TransitionKind::CubeLeft) ? 1.0f : -1.0f;
                const float rot = t * 0.5f * pi;
                if (t < 0.5f) {
                    const float scaleX = std::cos(rot);
                    const float offset = dir * (1.0f - scaleX) * w * 0.5f;
                    const float ax = cx + (x + 0.5f - cx - offset) / std::max(0.01f, scaleX);
                    p = sample(a, ax, y + 0.5f);
                    const float shade = 1.0f - t * 0.5f;
                    p = {p.r * shade, p.g * shade, p.b * shade, p.a};
                } else {
                    const float scaleX = std::sin(rot);
                    const float offset = -dir * (1.0f - scaleX) * w * 0.5f;
                    const float bx = cx + (x + 0.5f - cx - offset) / std::max(0.01f, scaleX);
                    p = sample(b, bx, y + 0.5f);
                    const float shade = 0.5f + (t - 0.5f);
                    p = {p.r * shade, p.g * shade, p.b * shade, p.a};
                }
                break;
            }
            case TransitionKind::CubeUp:
            case TransitionKind::CubeDown: {
                const float dir = (kind == TransitionKind::CubeUp) ? 1.0f : -1.0f;
                const float rot = t * 0.5f * pi;
                if (t < 0.5f) {
                    const float scaleY = std::cos(rot);
                    const float offset = dir * (1.0f - scaleY) * h * 0.5f;
                    const float ay = cy + (y + 0.5f - cy - offset) / std::max(0.01f, scaleY);
                    p = sample(a, x + 0.5f, ay);
                    const float shade = 1.0f - t * 0.5f;
                    p = {p.r * shade, p.g * shade, p.b * shade, p.a};
                } else {
                    const float scaleY = std::sin(rot);
                    const float offset = -dir * (1.0f - scaleY) * h * 0.5f;
                    const float by = cy + (y + 0.5f - cy - offset) / std::max(0.01f, scaleY);
                    p = sample(b, x + 0.5f, by);
                    const float shade = 0.5f + (t - 0.5f);
                    p = {p.r * shade, p.g * shade, p.b * shade, p.a};
                }
                break;
            }
            case TransitionKind::FlipHorizontal: {
                const float scaleX = std::cos(t * pi);
                // The side shown is decided by the progress, not by the sign of a cosine that is ~0 at the middle
                // (its sign there differs between the CPU and a GPU).
                if (t < 0.5f) {
                    p = sample(a, cx + (x + 0.5f - cx) / std::max(0.01f, scaleX), y + 0.5f);
                } else {
                    p = sample(b, cx + (x + 0.5f - cx) / std::max(0.01f, -scaleX), y + 0.5f);
                }
                const float shade = 0.5f + 0.5f * std::abs(scaleX);
                p = {p.r * shade, p.g * shade, p.b * shade, p.a};
                break;
            }
            case TransitionKind::FlipVertical: {
                const float scaleY = std::cos(t * pi);
                if (t < 0.5f) {
                    p = sample(a, x + 0.5f, cy + (y + 0.5f - cy) / std::max(0.01f, scaleY));
                } else {
                    p = sample(b, x + 0.5f, cy + (y + 0.5f - cy) / std::max(0.01f, -scaleY));
                }
                const float shade = 0.5f + 0.5f * std::abs(scaleY);
                p = {p.r * shade, p.g * shade, p.b * shade, p.a};
                break;
            }
            case TransitionKind::DoorSwingOpen: {
                const float scaleX = 1.0f - t;
                const float ax = (x + 0.5f) / std::max(0.01f, scaleX);
                if (ax < w) {
                    p = sample(a, ax, y + 0.5f);
                } else {
                    p = pixB;
                }
                break;
            }
            case TransitionKind::FoldOver: {
                const float fold = t * (w + h);
                if ((x + y) < fold) {
                    p = pixB;
                } else {
                    p = pixA;
                }
                break;
            }
            }

            store(d, p);
        }
    }
}

} // namespace vedit::fx
