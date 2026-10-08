// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/project/Clip.h"
#include "core/project/SpeedCurve.h"
#include "fx/Color.h"
#include "fx/Stabilization.h"
#include "fx/Grade.h"
#include "fx/Composite.h"
#include "fx/MotionBlur.h"
#include "fx/AudioVisualizer.h"
#include "fx/Graphic.h"
#include "fx/VideoEffect.h"
#include "fx/Transition.h"
#include "engine/analysis/Spectrum.h"

#include <QByteArray>
#include <QJsonObject>
#include <QImage>

#include <map>
#include <memory>
#include <vector>

namespace Mlt {
class Filter;
class Producer;
class Profile;
class Repository;
class Transition;
} // namespace Mlt

namespace velacut::engine {

// Registers velacut's own MLT services (docs/ARCHITECTURE.md §5.2), in every process that renders. Each delegates the
// pixels to the CPU reference kernels of velacut_fx:
//  - transition "vedit.composite": track compositing, `b` over `a` (blank frames skipped: gaps never paint black);
//  - filter "vedit.transform": the clip placed on the canvas (fit, position, scale, rotation, flip, crop, opacity)
//    over its canvas background (main track: colour or the clip itself blurred);
//  - filter "vedit.adjust": colour look (filters, adjustments) + vignette, grain, sharpness;
//  - filter "vedit.chroma_key": green/blue screen removal with UV color distance, feather and spill suppression;
//  - filter "vedit.gain": volume, fades, pan, level meters (clips, tracks, master);
//  - transition "vedit.transition": the transitions of the library between two clips;
//  - producer "vedit.text": a text clip as a canvas-sized layer;
//  - producer "vedit.sticker": a sticker (picture, animated picture, emoji) fitted in a canvas-sized layer;
//  - producer "vedit.visualizer": an audio visualizer drawn from the spectrum of the audio under it;
//  - filter "vedit.beat": flash, zoom or shake on the beats;
//  - producer "vedit.graphic": an animated graphic element (counter, clock, progress bar, hand-drawn mark);
//  - filter "vedit.effect": a video effect of the library (fx::renderVideoEffect), animated with the clip's time.
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
    double denoise = 0; // 0…1, before everything else

    QByteArray key() const;
    bool changesColour() const { return !look.isIdentity() || !grade.isIdentity() || cube; }
};

struct GainSettings
{
    double gainDb = 0;
    Param gainDbParam{0.0};
    bool muted = false;
    double pan = 0;
    int fadeInFrames = 0;
    int fadeOutFrames = 0;
    int length = 0; // frames of the clip (for the fade out); 0 = no fades
    int firstFrame = 0; // position of the clip's first frame as the filter sees it (the in point of the cut)
    RationalTime sourceIn;
    double speed = 1.0;
    bool reversed = false;
    Rational frameRate;
    QByteArray meterKey; // where the level is reported (empty: not metered)

    bool isNeutral() const
    {
        return gainDb == 0 && !gainDbParam.isAnimated() && !muted && pan == 0 && fadeInFrames == 0 && fadeOutFrames == 0;
    }
    QByteArray key() const;
};

struct AudioEffectsSettings
{
    bool denoise = false;
    double denoiseAmount = 1.0;
    QString voiceEffect; // "none", "enhance", "deep", "chipmunk", "robot", "radio", "megaphone", "echo"
    double eqLow = 0.0;
    double eqMid = 0.0;
    double eqHigh = 0.0;
    bool compressor = false;
    double compressorThreshold = -18.0;
    double compressorRatio = 3.0;

    bool hasEffects() const
    {
        return denoise || (!voiceEffect.isEmpty() && voiceEffect != QStringLiteral("none")) ||
               eqLow != 0.0 || eqMid != 0.0 || eqHigh != 0.0 || compressor;
    }
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
std::unique_ptr<Mlt::Filter> makeMotionBlurFilter(Mlt::Profile &profile, const fx::MotionBlurSettings &settings);
std::unique_ptr<Mlt::Producer> makeSpeedRampProducer(Mlt::Profile &profile,
                                                     std::shared_ptr<Mlt::Producer> baseProducer,
                                                     const SpeedCurve &curve,
                                                     int sourceIn,
                                                     int length,
                                                     bool reversed);
// ---- stickers, visualizers and beat effects (Phase 5, docs/ARCHITECTURE.md D-49…D-51) ------------------------------

// The frames of a sticker's picture (one for a still picture), decoded on the calling thread (the projection's:
// fonts and SVG are never used in MLT's threads, D-39).
struct StickerPicture
{
    std::vector<QImage> frames; // RGBA8888
    std::vector<int> delaysMs;  // of every frame of an animated picture

    int delayOf(int index) const;
};
// SVG (rendered at `maxSide`), PNG/JPEG/WebP, or every frame of an animated GIF/WebP (at most `maxSide`).
StickerPicture loadStickerPicture(const QString &path, int maxSide);
// An emoji drawn with the system's colour emoji font, `size` pixels square.
StickerPicture renderEmoji(const QString &emoji, int size);
void tintPicture(StickerPicture &picture, const Color &tint);
std::unique_ptr<Mlt::Producer> makeStickerProducer(Mlt::Profile &profile, StickerPicture picture, bool loop, double speed);

// Audio playing under a visualizer: from `startFrame` to `endFrame` (frames of the visualizer clip, profile rate), the
// source `sourceSeconds` on, at `speed`.
struct VisualizerAudio
{
    int startFrame = 0;
    int endFrame = 0;
    double sourceSeconds = 0.0;
    double speed = 1.0;
    std::shared_ptr<const Spectrum> spectrum;
};
struct VisualizerRender
{
    fx::VisualizerSettings settings;
    double smoothing = 0.5;
    std::vector<VisualizerAudio> audio;
};
std::unique_ptr<Mlt::Producer> makeVisualizerProducer(Mlt::Profile &profile, VisualizerRender render);
fx::VisualizerSettings visualizerSettings(const AudioVisualizerSettings &settings);

struct BeatEffectSettings
{
    enum class Kind
    {
        Flash, // amount: 0–1 towards white
        Zoom,  // amount: extra scale at the beat (0.15 = +15 %)
        Shake, // amount: pixels at 1080p
    };
    Kind kind = Kind::Flash;
    double amount = 0.7;
    double decay = 0.18;       // seconds for the pulse to fade
    std::vector<double> beats; // seconds from `firstFrame`, ascending
    int firstFrame = 0;        // position of the clip's first frame for the filter
    Rational frameRate{30, 1};

    QByteArray key() const;
};
std::unique_ptr<Mlt::Filter> makeBeatFilter(Mlt::Profile &profile, const BeatEffectSettings &settings);

fx::GraphicParams graphicParams(const GraphicSettings &settings);
// The characters of a graphic element with text, `canvasHeight` the height of the canvas: drawn on the calling thread
// (the projection's, D-39) with the application's bold font, filled with `color` and outlined with `color2`.
fx::GraphicGlyphs makeGraphicGlyphs(const GraphicSettings &settings, int canvasHeight);
// `length`: frames of the clip (the animation spans it).
std::unique_ptr<Mlt::Producer> makeGraphicProducer(Mlt::Profile &profile, const GraphicSettings &settings, int length);
// A caption line `length` frames long in the style of its track (engine/text/CaptionRenderer.h).
std::unique_ptr<Mlt::Producer> makeCaptionProducer(Mlt::Profile &profile, const SubtitleClipData &line,
                                                   const CaptionStyle &style, int length);

// "Stabilize" (effect "vedit.stabilize"): each frame of the clip moved back onto a smoothed camera path and enlarged
// to hide the borders (fx/Stabilization.h). The corrections are for the analysed frames of the file.
struct StabilizeSettings
{
    std::shared_ptr<const fx::Stabilization> stabilization;
    double analysisStart = 0.0;     // seconds of the file of the first analysed frame
    double analysisFps = 30.0;      // analysed frames per second (the video's rate)
    double secondsPerPosition = 0.0; // seconds of the file per position of the clip's producer (speed / output rate)
    QByteArray dataKey;             // what the corrections were made from (motion data, strength)

    QByteArray key() const;
};
std::unique_ptr<Mlt::Filter> makeStabilizeFilter(Mlt::Profile &profile, const StabilizeSettings &settings);

struct VideoEffectSettings
{
    fx::EffectKernel kernel = fx::EffectKernel::Blur;
    fx::VideoEffectParams params; // `time` is set per frame
    double mix = 1.0;             // the effect over the original: 0 = none
    int firstFrame = 0;           // position of the clip's first frame for the filter
    Rational frameRate{30, 1};

    QByteArray key() const;
};
std::unique_ptr<Mlt::Filter> makeVideoEffectFilter(Mlt::Profile &profile, const VideoEffectSettings &settings);
// The parameters of a video effect: the preset's (manifest "params"), overridden by the clip's own
// (docs/EFFECT_FORMAT.md §9).
fx::VideoEffectParams videoEffectParams(const QJsonObject &preset, const std::map<QString, Param> &own);

} // namespace velacut::engine
