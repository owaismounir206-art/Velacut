// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "fx/Image.h"

#include <array>
#include <optional>
#include <string_view>

namespace velacut::fx {

// Comprehensive transition library (SPEC §5.11bis, Phase 5: 100+ transitions across 10 categories).
enum class TransitionKind
{
    // 1. Base (10)
    Dissolve,
    DipToBlack,
    DipToWhite,
    DipToColor,
    FadeGrayscale,
    ExposureFlash,
    LumaFade,
    Additive,
    Subtract,
    Multiply,

    // 2. Motion (16)
    SlideLeft,
    SlideRight,
    SlideUp,
    SlideDown,
    SlideTopLeft,
    SlideTopRight,
    SlideBottomLeft,
    SlideBottomRight,
    PushLeft,
    PushRight,
    PushUp,
    PushDown,
    PushTopLeft,
    PushTopRight,
    PushBottomLeft,
    PushBottomRight,

    // 3. Zoom & Spin (12)
    ZoomIn,
    ZoomOut,
    CrossZoom,
    WarpZoom,
    SpinZoomIn,
    SpinZoomOut,
    SpinCw,
    SpinCcw,
    RotateScaleCw,
    RotateScaleCcw,
    DollyZoomIn,
    DollyZoomOut,

    // 4. Wipes & Bands (16)
    WipeLeft,
    WipeRight,
    WipeUp,
    WipeDown,
    WipeTopLeft,
    WipeTopRight,
    WipeBottomLeft,
    WipeBottomRight,
    SplitHorizontal,
    SplitVertical,
    BarnDoorHorizontal,
    BarnDoorVertical,
    BlindsHorizontal,
    BlindsVertical,
    Checkerboard,
    MosaicWipe,

    // 5. Shapes & Geometry (12)
    Iris,
    IrisDiamond,
    IrisStar,
    IrisHeart,
    IrisTriangle,
    Clock,
    ClockCounter,
    ClockDual,
    HexagonGrid,
    PolygonOpen,
    DiagonalSliceLeft,
    DiagonalSliceRight,

    // 6. Blurs & Camera (10)
    BlurDissolve,
    DirectionalBlurLeft,
    DirectionalBlurRight,
    RadialBlur,
    TiltShift,
    WhipPanLeft,
    WhipPanRight,
    WhipPanUp,
    WhipPanDown,
    CameraShutter,

    // 7. Glitch & Digital (10)
    GlitchRgb,
    Pixelate,
    ScanlineTear,
    VhsDistortion,
    BlockDissolve,
    DigitalNoise,
    Jitter,
    DataCorruption,
    ScreenTear,
    LumaGlitch,

    // 8. Lights & Leaks (10)
    LightLeakWarm,
    LightLeakCool,
    LensGlow,
    FilmBurn,
    RainbowFlash,
    ColorInvert,
    Solarize,
    NeonFlash,
    Strobe,
    LumaGlow,

    // 9. Distortions (10)
    WaveHorizontal,
    WaveVertical,
    RippleWater,
    Vortex,
    Shockwave,
    Pinch,
    PinchTwist,
    SphereLens,
    Swirl,
    Kaleidoscope,

    // 10. 3D Transitions (8)
    CubeLeft,
    CubeRight,
    CubeUp,
    CubeDown,
    FlipHorizontal,
    FlipVertical,
    DoorSwingOpen,
    FoldOver,
};

std::optional<TransitionKind> transitionKindFromName(std::string_view name);
std::string_view transitionKindName(TransitionKind kind);

struct TransitionParams
{
    double softness = 0.02; // width of the soft edge (wipes, iris, clock), fraction of the image
    std::array<float, 4> color{0.0f, 0.0f, 0.0f, 1.0f}; // color for dipToColor
};

// Easing of the progress (SPEC §5.6 presets used by transitions).
enum class Easing
{
    Linear,
    EaseIn,
    EaseOut,
    EaseInOut,
};
double ease(Easing easing, double t);

// The pseudo-random value 0…1 the kernels use for pixel or cell (x, y) (noise, mosaics, glitches): also uploaded to the
// GPU path, so that both draw the same pattern.
float transitionNoise(int x, int y);

// out = transition from `a` to `b` at `progress` 0…1 (already eased). All images the same size; `out` may not alias.
// Straight alpha; mixing in premultiplied space. Rows [rowBegin, rowEnd).
void renderTransition(TransitionKind kind, ImageView out, ConstImageView a, ConstImageView b, double progress,
                      const TransitionParams &params, int rowBegin, int rowEnd);

} // namespace velacut::fx
