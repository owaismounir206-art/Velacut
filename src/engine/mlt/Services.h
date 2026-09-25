// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/project/Clip.h"
#include "fx/Color.h"
#include "fx/Transition.h"

#include <QByteArray>

#include <memory>

namespace Mlt {
class Filter;
class Producer;
class Profile;
class Repository;
class Transition;
} // namespace Mlt

namespace vedit::engine {

// Registers vedit's own MLT services (docs/ARCHITECTURE.md §5.2), in every process that renders. Each delegates the
// pixels to the CPU reference kernels of vedit_fx:
//  - transition "vedit.composite": track compositing, `b` over `a` (blank frames skipped: gaps never paint black);
//  - filter "vedit.transform": the clip placed on the canvas (fit, position, scale, rotation, flip, crop, opacity)
//    over its canvas background (main track: colour or the clip itself blurred);
//  - filter "vedit.adjust": colour look (filters, adjustments) + vignette, grain, sharpness;
//  - filter "vedit.gain": volume, fades, pan, level meters (clips, tracks, master);
//  - transition "vedit.transition": the transitions of the library between two clips;
//  - producer "vedit.text": a text clip as a canvas-sized layer.
// Called by MltRuntime right after Mlt::Factory::init().
void registerServices(Mlt::Repository *repository);

// ---- settings of the services (C++ data attached to the MLT objects) --------------------------------------------

struct TransformSettings
{
    // Size of the source as displayed (rotation and pixel aspect applied), in its own pixels. A canvas-sized layer
    // (text, colour) uses the canvas size and FitMode::Stretch.
    double sourceWidth = 1920;
    double sourceHeight = 1080;
    FitMode fit = FitMode::Contain;
    double x = 0, y = 0; // offset from the centre, in canvas widths / heights
    double scaleX = 1, scaleY = 1;
    double rotation = 0; // degrees clockwise
    bool flipH = false, flipV = false;
    double cropLeft = 0, cropTop = 0, cropRight = 0, cropBottom = 0; // fractions of the source
    double opacity = 1;
    // Canvas background behind the clip (main track only).
    std::optional<CanvasBackground> background;

    bool isIdentityLayer() const; // nothing to do for a canvas-sized layer
    QByteArray key() const;
};

struct AdjustSettings
{
    fx::ColorAdjust look;
    double intensity = 1;
    double vignette = 0;
    double grain = 0;
    double sharpness = 0;

    QByteArray key() const;
};

struct GainSettings
{
    double gainDb = 0;
    bool muted = false;
    double pan = 0;
    int fadeInFrames = 0;
    int fadeOutFrames = 0;
    int length = 0; // frames of the clip (for the fade out); 0 = no fades
    int firstFrame = 0; // position of the clip's first frame as the filter sees it (the in point of the cut)
    QByteArray meterKey; // where the level is reported (empty: not metered)

    bool isNeutral() const { return gainDb == 0 && !muted && pan == 0 && fadeInFrames == 0 && fadeOutFrames == 0; }
    QByteArray key() const;
};

struct TransitionSettings
{
    fx::TransitionKind kind = fx::TransitionKind::Dissolve;
    fx::Easing easing = fx::Easing::EaseInOut;
    double softness = 0.02;
};

std::unique_ptr<Mlt::Filter> makeTransformFilter(Mlt::Profile &profile, const TransformSettings &settings);
std::unique_ptr<Mlt::Filter> makeAdjustFilter(Mlt::Profile &profile, const AdjustSettings &settings);
std::unique_ptr<Mlt::Filter> makeGainFilter(Mlt::Profile &profile, const GainSettings &settings);
std::unique_ptr<Mlt::Transition> makeTransition(Mlt::Profile &profile, const TransitionSettings &settings);
std::unique_ptr<Mlt::Producer> makeTextProducer(Mlt::Profile &profile, const TextClipData &text);

} // namespace vedit::engine
