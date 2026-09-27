// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/project/Clip.h"
#include "fx/Color.h"
#include "fx/Grade.h"
#include "fx/Composite.h"
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
//  - filter "vedit.chroma_key": green/blue screen removal with UV color distance, feather and spill suppression;
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
    fx::BlendMode blendMode = fx::BlendMode::Normal;

    // Animation / keyframe parameters evaluated dynamically per frame
    Param positionParam{Vec2{0, 0}};
    Param scaleParam{Vec2{1, 1}};
    Param rotationParam{0.0};
    Param opacityParam{1.0};
    Param cropLeftParam{0.0};
    Param cropTopParam{0.0};
    Param cropRightParam{0.0};
    Param cropBottomParam{0.0};
    RationalTime sourceIn{0, 30};
    Rational frameRate{30, 1};
    int firstFrame = 0;
    int clipLength = 0;
    // How the clip plays its source, for the keyframe time (D-05, core/project/ClipTime.h).
    double speed = 1;
    bool reversed = false;
    ClipAnimations animations;

    // Canvas background behind the clip (main track only).
    std::optional<CanvasBackground> background;

    bool isIdentityLayer() const; // nothing to do for a canvas-sized layer
    QByteArray key() const;
};

struct ChromaKeySettings
{
    Color keyColor{0, 255, 0, 255};
    double similarity = 0.4;
    double smoothness = 0.1;
    double spill = 0.5;

    bool isIdentity() const { return similarity <= 0.0; }
    QByteArray key() const;
};

struct MaskSettings
{
    std::vector<Mask> masks;
    RationalTime sourceIn{0, 30};
    Rational frameRate{30, 1};
    int firstFrame = 0;
    int clipLength = 0;
    double speed = 1;
    bool reversed = false;

    bool isIdentity() const { return masks.empty(); }
    QByteArray key() const;
};

struct AdjustSettings
{
    fx::ColorAdjust look;
    fx::Grade grade;                        // "vedit.grade"
    std::shared_ptr<const fx::CubeLut> cube; // "vedit.lut", applied after look and grade
    QString cubeKey;                        // identifies the cube (file and date) in key()
    double intensity = 1;
    double vignette = 0;
    double grain = 0;
    double sharpness = 0;

    QByteArray key() const;
    bool changesColour() const { return !look.isIdentity() || !grade.isIdentity() || cube; }
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

struct DeflickerSettings
{
    int size = 5;
    QString mode = QStringLiteral("pm");

    QByteArray key() const
    {
        return QByteArray::number(size) + ':' + mode.toLatin1();
    }
};

std::unique_ptr<Mlt::Filter> makeTransformFilter(Mlt::Profile &profile, const TransformSettings &settings);
std::unique_ptr<Mlt::Filter> makeAdjustFilter(Mlt::Profile &profile, const AdjustSettings &settings);
std::unique_ptr<Mlt::Filter> makeDeflickerFilter(Mlt::Profile &profile, const DeflickerSettings &settings);
std::unique_ptr<Mlt::Filter> makeChromaKeyFilter(Mlt::Profile &profile, const ChromaKeySettings &settings);
std::unique_ptr<Mlt::Filter> makeMaskFilter(Mlt::Profile &profile, const MaskSettings &settings);
std::unique_ptr<Mlt::Filter> makeGainFilter(Mlt::Profile &profile, const GainSettings &settings);
std::unique_ptr<Mlt::Transition> makeTransition(Mlt::Profile &profile, const TransitionSettings &settings);
std::unique_ptr<Mlt::Producer> makeTextProducer(Mlt::Profile &profile, const TextClipData &text);

} // namespace vedit::engine
