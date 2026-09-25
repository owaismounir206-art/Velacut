// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "fx/Image.h"

#include <optional>
#include <string_view>

namespace vedit::fx {

// Base transitions (SPEC §5.11bis "Base", "Movimento", "Wipe e forme"): the CPU reference of every transition.
enum class TransitionKind
{
    Dissolve,
    DipToBlack,
    DipToWhite,
    SlideLeft, // B enters from the right, moving left, over A
    SlideRight,
    SlideUp,
    SlideDown,
    PushLeft, // A leaves to the left pushed by B
    PushRight,
    PushUp,
    PushDown,
    WipeLeft, // the edge moves leftwards revealing B
    WipeRight,
    WipeUp,
    WipeDown,
    Iris,  // a circle opening from the centre
    Clock, // a hand sweeping clockwise from 12
    ZoomIn, // A zooms towards the viewer while fading into B
};

std::optional<TransitionKind> transitionKindFromName(std::string_view name);
std::string_view transitionKindName(TransitionKind kind);

struct TransitionParams
{
    double softness = 0.02; // width of the soft edge (wipes, iris, clock), fraction of the image
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

// out = transition from `a` to `b` at `progress` 0…1 (already eased). All images the same size; `out` may not alias.
// Straight alpha; mixing in premultiplied space. Rows [rowBegin, rowEnd).
void renderTransition(TransitionKind kind, ImageView out, ConstImageView a, ConstImageView b, double progress,
                      const TransitionParams &params, int rowBegin, int rowEnd);

} // namespace vedit::fx
