// SPDX-License-Identifier: GPL-3.0-or-later
#include "Services.h"

#include "core/project/ClipTime.h"
#include "engine/playback/AudioMeters.h"
#include "engine/text/TextRenderer.h"
#include "engine/timeline/ClipPlacement.h"
#include "fx/Animation.h"
#include "fx/Audio.h"
#include "fx/AudioVisualizer.h"
#include "fx/BeatEffects.h"
#include "fx/ChromaKey.h"
#include "fx/Composite.h"
#include "fx/Mask.h"
#include "fx/Transform.h"

#include <QCryptographicHash>
#include <QDataStream>
#include <QFont>
#include <QFontMetricsF>
#include <QPainterPath>
#include <QImageReader>
#include <QIODevice>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMutex>
#include <QPainter>
#include <QSvgRenderer>
#include <QtGlobal>

#include <mlt++/Mlt.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace vedit::engine {

namespace {

constexpr const char *kSettings = "vedit.settings";

// Runs `rows(begin, end)` over the image in horizontal bands on MLT's slice threads.
template<typename Rows>
void runSliced(int height, Rows rows)
{
    struct Job
    {
        Rows *rows;
        int height;
    } job{&rows, height};
    const int jobs = std::min(mlt_slices_count_normal(), std::max(1, height / 32));
    mlt_slices_run_normal(jobs, [](int, int index, int count, void *cookie) {
        auto *j = static_cast<Job *>(cookie);
        (*j->rows)(j->height * index / count, j->height * (index + 1) / count);
        return 0;
    }, &job);
}

template<typename T>
const T *settingsOf(mlt_properties properties)
{
    return static_cast<const T *>(mlt_properties_get_data(properties, kSettings, nullptr));
}

template<typename T>
void attachSettings(Mlt::Properties &properties, const T &settings)
{
    properties.set(kSettings, new T(settings), 0, [](void *p) { delete static_cast<T *>(p); });
}

void profileSize(mlt_service service, int &width, int &height)
{
    if (width > 0 && height > 0) {
        return;
    }
    const mlt_profile profile = mlt_service_profile(service);
    width = profile ? profile->width : 1920;
    height = profile ? profile->height : 1080;
}

// ---- vedit.composite ------------------------------------------------------------------------------------------

int compositeGetImage(mlt_frame aFrame, uint8_t **image, mlt_image_format *format, int *width, int *height, int)
{
    mlt_frame bFrame = mlt_frame_pop_frame(aFrame);
    auto transition = static_cast<mlt_transition>(mlt_frame_pop_service(aFrame));
    *format = mlt_image_rgba;
    int error = mlt_frame_get_image(aFrame, image, format, width, height, 1);
    if (error || *format != mlt_image_rgba || !bFrame || mlt_frame_is_test_card(bFrame)) {
        return error;
    }
    mlt_image_format bFormat = mlt_image_rgba;
    int bWidth = *width;
    int bHeight = *height;
    uint8_t *bImage = nullptr;
    if (mlt_frame_get_image(bFrame, &bImage, &bFormat, &bWidth, &bHeight, 0) != 0 || !bImage ||
        bFormat != mlt_image_rgba || bWidth != *width || bHeight != *height) {
        return 0; // nothing usable to composite: keep the lower tracks
    }
    const mlt_properties properties = MLT_TRANSITION_PROPERTIES(transition);
    const int opacity = mlt_properties_exists(properties, "opacity_255") ? mlt_properties_get_int(properties, "opacity_255") : 255;
    const mlt_properties bProperties = mlt_frame_properties(bFrame);
    const int modeInt = mlt_properties_exists(bProperties, "vedit.blend_mode")
                            ? mlt_properties_get_int(bProperties, "vedit.blend_mode")
                            : (mlt_properties_exists(properties, "blend_mode") ? mlt_properties_get_int(properties, "blend_mode") : 0);
    const fx::ImageView destination{*image, *width, *height, *width * 4};
    const fx::ConstImageView source{bImage, bWidth, bHeight, bWidth * 4};
    const auto mode = static_cast<fx::BlendMode>(modeInt);
    runSliced(*height, [&](int begin, int end) { fx::compositeBlend(destination, source, mode, opacity, begin, end); });
    return 0;
}

mlt_frame compositeProcess(mlt_transition transition, mlt_frame aFrame, mlt_frame bFrame)
{
    mlt_frame_push_service(aFrame, transition);
    mlt_frame_push_frame(aFrame, bFrame);
    mlt_frame_push_get_image(aFrame, compositeGetImage);
    return aFrame;
}

void *createComposite(mlt_profile, mlt_service_type, const char *, const void *)
{
    mlt_transition transition = mlt_transition_new();
    if (transition) {
        transition->process = compositeProcess;
        mlt_properties_set_int(MLT_TRANSITION_PROPERTIES(transition), "_transition_type", 1); // video only
    }
    return transition;
}

// ---- vedit.transform ------------------------------------------------------------------------------------------

void fillBackground(const TransformSettings &s, uint8_t *canvas, int w, int h, const uint8_t *source, int sw, int sh)
{
    const CanvasBackground &background = *s.background;
    if (background.type == BackgroundType::Blur && source && sw > 0 && sh > 0) {
        // The clip itself, covering the canvas, small and blurred, then enlarged: cheap and smooth.
        const int smallW = std::max(8, w / 8);
        const int smallH = std::max(8, h / 8);
        std::vector<uint8_t> cover(static_cast<size_t>(smallW * smallH * 4), 0);
        fx::ImageView coverView{cover.data(), smallW, smallH, smallW * 4};
        const double scale = std::max(double(smallW) / sw, double(smallH) / sh);
        const fx::Affine forward = fx::Affine::translation(smallW / 2.0, smallH / 2.0) * fx::Affine::scaling(scale, scale) *
                                   fx::Affine::translation(-sw / 2.0, -sh / 2.0);
        fx::drawAffine(coverView, fx::ConstImageView{source, sw, sh, sw * 4}, forward.inverted(),
                       fx::SourceWindow{0, 0, double(sw), double(sh)});
        for (size_t i = 3; i < cover.size(); i += 4) {
            cover[i] = 255;
        }
        fx::boxBlur(coverView, std::max(1, static_cast<int>(std::lround(background.amount * smallW / 12.0))));
        fx::ImageView target{canvas, w, h, w * 4};
        runSliced(h, [&](int begin, int end) { fx::resizeBilinear(target, coverView, begin, end); });
        return;
    }
    // Colour (and, until they are rendered, images and patterns: the colour of the background).
    const Color c = background.type == BackgroundType::Color ? background.color : Color{0, 0, 0, 255};
    for (int i = 0; i < w * h; ++i) {
        canvas[i * 4] = c.r;
        canvas[i * 4 + 1] = c.g;
        canvas[i * 4 + 2] = c.b;
        canvas[i * 4 + 3] = c.a;
    }
}

int transformGetImage(mlt_frame frame, uint8_t **image, mlt_image_format *format, int *width, int *height, int)
{
    auto filter = static_cast<mlt_filter>(mlt_frame_pop_service(frame));
    const int framePosition = mlt_frame_pop_service_int(frame);
    const TransformSettings *s = settingsOf<TransformSettings>(MLT_FILTER_PROPERTIES(filter));
    int w = *width;
    int h = *height;
    profileSize(MLT_FILTER_SERVICE(filter), w, h);
    if (!s) {
        *format = mlt_image_rgba;
        return mlt_frame_get_image(frame, image, format, width, height, 0);
    }

    mlt_properties frameProps = mlt_frame_properties(frame);
    mlt_properties_set_int(frameProps, "vedit.blend_mode", static_cast<int>(s->blendMode));

    double posX = s->x;
    double posY = s->y;
    double scX = s->scaleX;
    double scY = s->scaleY;
    double rot = s->rotation;
    double op = s->opacity;
    double cL = s->cropLeft;
    double cT = s->cropTop;
    double cR = s->cropRight;
    double cB = s->cropBottom;

    const bool isAnimated = s->positionParam.isAnimated() || s->scaleParam.isAnimated() ||
                            s->rotationParam.isAnimated() || s->opacityParam.isAnimated() ||
                            s->cropLeftParam.isAnimated() || s->cropTopParam.isAnimated() ||
                            s->cropRightParam.isAnimated() || s->cropBottomParam.isAnimated();

    if (isAnimated) {
        const Rational rate = (s->frameRate.num() > 0 && s->frameRate.den() > 0) ? s->frameRate : Rational(30, 1);
        const int localFrame = framePosition - s->firstFrame;
        const RationalTime contentTime = keyframeTime(s->sourceIn, s->speed, s->reversed, RationalTime(s->clipLength, rate),
                                                      RationalTime(localFrame, rate));

        if (s->positionParam.isAnimated()) {
            const ParamValue v = s->positionParam.valueAt(contentTime);
            if (const auto *p = std::get_if<Vec2>(&v)) {
                posX = p->x;
                posY = p->y;
            }
        }
        if (s->scaleParam.isAnimated()) {
            const ParamValue v = s->scaleParam.valueAt(contentTime);
            if (const auto *p = std::get_if<Vec2>(&v)) {
                scX = p->x;
                scY = p->y;
            }
        }
        if (s->rotationParam.isAnimated()) {
            rot = s->rotationParam.numberAt(contentTime, s->rotation);
        }
        if (s->opacityParam.isAnimated()) {
            op = s->opacityParam.numberAt(contentTime, s->opacity);
        }
        if (s->cropLeftParam.isAnimated()) {
            cL = s->cropLeftParam.numberAt(contentTime, s->cropLeft);
        }
        if (s->cropTopParam.isAnimated()) {
            cT = s->cropTopParam.numberAt(contentTime, s->cropTop);
        }
        if (s->cropRightParam.isAnimated()) {
            cR = s->cropRightParam.numberAt(contentTime, s->cropRight);
        }
        if (s->cropBottomParam.isAnimated()) {
            cB = s->cropBottomParam.numberAt(contentTime, s->cropBottom);
        }
    }

    if (!s->animations.isEmpty()) {
        const Rational rate = (s->frameRate.num() > 0 && s->frameRate.den() > 0) ? s->frameRate : Rational(30, 1);
        const int localFrame = framePosition - s->firstFrame;
        const int clipLen = s->clipLength > 0 ? s->clipLength : 90;

        // 1. Entry animation ("in")
        if (s->animations.in && localFrame >= 0) {
            const int inFrames = std::clamp(static_cast<int>(s->animations.in->duration.rescaled(rate, Rounding::NearestEven).value()), 1, clipLen);
            if (localFrame < inFrames) {
                const double rawT = std::clamp(static_cast<double>(localFrame) / static_cast<double>(inFrames), 0.0, 1.0);
                const double t = s->animations.in->easing.apply(rawT);
                fx::applyInAnimation(s->animations.in->type.id, t, posX, posY, scX, scY, rot, op, cL, cT, cR, cB);
            }
        }

        // 2. Loop animation ("loop")
        if (s->animations.loop && localFrame >= 0 && localFrame < clipLen) {
            // A loop without a duration spans the whole clip (Ken Burns).
            const double cycleSec = s->animations.loop->duration.toSecondsDouble() > 0.05
                                        ? s->animations.loop->duration.toSecondsDouble()
                                        : std::max(1.0, static_cast<double>(clipLen)) / rate.toDouble();
            const double timeSec = localFrame / rate.toDouble();
            const double phase = std::fmod(timeSec, cycleSec) / cycleSec;
            const double cycleT = s->animations.loop->easing.apply(phase >= 0.0 ? phase : phase + 1.0);
            fx::applyLoopAnimation(s->animations.loop->type.id, cycleT, posX, posY, scX, scY, rot, op);
        }

        // 3. Exit animation ("out")
        if (s->animations.out && localFrame >= 0 && localFrame < clipLen) {
            const int outFrames = std::clamp(static_cast<int>(s->animations.out->duration.rescaled(rate, Rounding::NearestEven).value()), 1, clipLen);
            const int outStart = clipLen - outFrames;
            if (localFrame >= outStart) {
                const double rawT = std::clamp(static_cast<double>(localFrame - outStart) / static_cast<double>(outFrames), 0.0, 1.0);
                const double t = s->animations.out->easing.apply(rawT);
                fx::applyOutAnimation(s->animations.out->type.id, t, posX, posY, scX, scY, rot, op, cL, cT, cR, cB);
            }
        }
    }

    const QSizeF fitted = fittedSize(QSizeF(s->sourceWidth, s->sourceHeight), s->fit, QSize(w, h));
    const double fitW = fitted.width();
    const double fitH = fitted.height();
    const double shownW = fitW * std::abs(scX);
    const double shownH = fitH * std::abs(scY);
    // The source is requested at about the size it is shown: no work on pixels nobody sees.
    const double limit = 2.0 * std::max(w, h);
    int sw = std::clamp(static_cast<int>(std::lround(shownW)), 1, static_cast<int>(limit));
    int sh = std::clamp(static_cast<int>(std::lround(shownH)), 1, static_cast<int>(limit));
    mlt_image_format sourceFormat = mlt_image_rgba;
    uint8_t *source = nullptr;
    const int error = mlt_frame_get_image(frame, &source, &sourceFormat, &sw, &sh, 0);
    if (error || !source || sourceFormat != mlt_image_rgba) {
        return error ? error : 1;
    }
    const int size = w * h * 4;
    auto *canvas = static_cast<uint8_t *>(mlt_pool_alloc(size));
    if (s->background) {
        fillBackground(*s, canvas, w, h, source, sw, sh);
    } else {
        std::memset(canvas, 0, static_cast<size_t>(size));
    }
    // Source pixels → canvas pixels: centre, scale to the shown size (with flips), rotate, move.
    const double kx = shownW / sw * (s->flipH ? -1 : 1);
    const double ky = shownH / sh * (s->flipV ? -1 : 1);
    const fx::Affine forward = fx::Affine::translation(w / 2.0 + posX * w, h / 2.0 + posY * h) * fx::Affine::rotation(rot) *
                               fx::Affine::scaling(kx, ky) * fx::Affine::translation(-sw / 2.0, -sh / 2.0);
    const fx::Affine toSource = forward.inverted();
    const fx::SourceWindow window{cL * sw, cT * sh, (1.0 - cR) * sw, (1.0 - cB) * sh};
    const fx::ImageView target{canvas, w, h, w * 4};
    const fx::ConstImageView src{source, sw, sh, sw * 4};
    runSliced(h, [&](int begin, int end) { fx::drawAffine(target, src, toSource, window, op, begin, end); });
    mlt_frame_set_image(frame, canvas, size, mlt_pool_release);
    *image = canvas;
    *width = w;
    *height = h;
    *format = mlt_image_rgba;
    return 0;
}

mlt_frame transformProcess(mlt_filter filter, mlt_frame frame)
{
    mlt_frame_push_service_int(frame, static_cast<int>(mlt_frame_get_position(frame)));
    mlt_frame_push_service(frame, filter);
    mlt_frame_push_get_image(frame, transformGetImage);
    return frame;
}

// ---- vedit.adjust ---------------------------------------------------------------------------------------------

struct AdjustState
{
    AdjustSettings settings;
    fx::ColorLut lut;
};

int adjustGetImage(mlt_frame frame, uint8_t **image, mlt_image_format *format, int *width, int *height, int)
{
    auto filter = static_cast<mlt_filter>(mlt_frame_pop_service(frame));
    const int position = mlt_frame_pop_service_int(frame);
    *format = mlt_image_rgba;
    const int error = mlt_frame_get_image(frame, image, format, width, height, 1);
    const auto *state = static_cast<const AdjustState *>(mlt_properties_get_data(MLT_FILTER_PROPERTIES(filter), kSettings, nullptr));
    if (error || !state || *format != mlt_image_rgba || !*image) {
        return error;
    }
    const AdjustSettings &s = state->settings;
    const double k = std::clamp(s.intensity, 0.0, 1.0);
    const fx::ImageView view{*image, *width, *height, *width * 4};
    if (s.changesColour()) {
        runSliced(*height, [&](int begin, int end) { state->lut.apply(view, k, begin, end); });
    }
    if (s.sharpness > 0) {
        const std::vector<uint8_t> copy(*image, *image + static_cast<size_t>(*width * *height * 4));
        const fx::ConstImageView source{copy.data(), *width, *height, *width * 4};
        runSliced(*height, [&](int begin, int end) { fx::sharpen(view, source, s.sharpness * k * 1.5, begin, end); });
    }
    if (s.vignette != 0) {
        runSliced(*height, [&](int begin, int end) { fx::vignette(view, s.vignette * k, 0.6, begin, end); });
    }
    if (s.grain > 0) {
        runSliced(*height, [&](int begin, int end) {
            fx::grain(view, s.grain * k, static_cast<std::uint32_t>(position), begin, end);
        });
    }
    return 0;
}

mlt_frame adjustProcess(mlt_filter filter, mlt_frame frame)
{
    mlt_frame_push_service_int(frame, static_cast<int>(mlt_frame_get_position(frame)));
    mlt_frame_push_service(frame, filter);
    mlt_frame_push_get_image(frame, adjustGetImage);
    return frame;
}

// ---- vedit.chroma_key -----------------------------------------------------------------------------------------

int chromaKeyGetImage(mlt_frame frame, uint8_t **image, mlt_image_format *format, int *width, int *height, int)
{
    auto filter = static_cast<mlt_filter>(mlt_frame_pop_service(frame));
    *format = mlt_image_rgba;
    const int error = mlt_frame_get_image(frame, image, format, width, height, 1);
    const auto *s = settingsOf<ChromaKeySettings>(MLT_FILTER_PROPERTIES(filter));
    if (error || !s || *format != mlt_image_rgba || !*image) {
        return error;
    }
    fx::ChromaKeySettings fxSettings;
    fxSettings.keyR = s->keyColor.r;
    fxSettings.keyG = s->keyColor.g;
    fxSettings.keyB = s->keyColor.b;
    fxSettings.similarity = s->similarity;
    fxSettings.smoothness = s->smoothness;
    fxSettings.spill = s->spill;

    const fx::ImageView view{*image, *width, *height, *width * 4};
    runSliced(*height, [&](int begin, int end) {
        fx::applyChromaKey(view, fxSettings, begin, end);
    });
    return 0;
}

mlt_frame chromaKeyProcess(mlt_filter filter, mlt_frame frame)
{
    mlt_frame_push_service(frame, filter);
    mlt_frame_push_get_image(frame, chromaKeyGetImage);
    return frame;
}

// ---- vedit.motion_blur ----------------------------------------------------------------------------------------

int motionBlurGetImage(mlt_frame frame, uint8_t **image, mlt_image_format *format, int *width, int *height, int)
{
    auto filter = static_cast<mlt_filter>(mlt_frame_pop_service(frame));
    *format = mlt_image_rgba;
    const int error = mlt_frame_get_image(frame, image, format, width, height, 1);
    const auto *s = settingsOf<fx::MotionBlurSettings>(MLT_FILTER_PROPERTIES(filter));
    if (error || !s || s->intensity <= 0.001 || *format != mlt_image_rgba || !*image) {
        return error;
    }

    const int w = *width;
    const int h = *height;
    const fx::ImageView view{*image, w, h, w * 4};
    fx::applyMotionBlur(view, *s);
    return 0;
}

mlt_frame motionBlurProcess(mlt_filter filter, mlt_frame frame)
{
    mlt_frame_push_service(frame, filter);
    mlt_frame_push_get_image(frame, motionBlurGetImage);
    return frame;
}

// ---- vedit.mask -----------------------------------------------------------------------------------------------

int maskGetImage(mlt_frame frame, uint8_t **image, mlt_image_format *format, int *width, int *height, int)
{
    auto filter = static_cast<mlt_filter>(mlt_frame_pop_service(frame));
    const int framePosition = mlt_frame_pop_service_int(frame);
    *format = mlt_image_rgba;
    const int error = mlt_frame_get_image(frame, image, format, width, height, 1);
    const auto *s = settingsOf<MaskSettings>(MLT_FILTER_PROPERTIES(filter));
    if (error || !s || s->masks.empty() || *format != mlt_image_rgba || !*image) {
        return error;
    }

    const Rational rate = (s->frameRate.num() > 0 && s->frameRate.den() > 0) ? s->frameRate : Rational(30, 1);
    const int localFrame = framePosition - s->firstFrame;
    const RationalTime contentTime = keyframeTime(s->sourceIn, s->speed, s->reversed, RationalTime(s->clipLength, rate),
                                                  RationalTime(localFrame, rate));

    std::vector<fx::MaskParams> params;
    params.reserve(s->masks.size());

    for (const Mask &m : s->masks) {
        fx::MaskParams p;
        p.shape = static_cast<fx::MaskShape>(m.shape);
        p.invert = m.invert;

        const ParamValue centerVal = m.center.valueAt(contentTime);
        if (const auto *v = std::get_if<Vec2>(&centerVal)) {
            p.centerX = v->x;
            p.centerY = v->y;
        }

        const ParamValue sizeVal = m.size.valueAt(contentTime);
        if (const auto *v = std::get_if<Vec2>(&sizeVal)) {
            p.sizeX = v->x;
            p.sizeY = v->y;
        }

        p.rotation = m.rotation.numberAt(contentTime, 0.0);
        p.roundness = m.roundness.numberAt(contentTime, 0.0);
        p.feather = m.feather.numberAt(contentTime, 0.0);

        p.points.reserve(m.points.size());
        for (const auto &pt : m.points) {
            p.points.push_back(fx::MaskPoint{pt.p.x, pt.p.y, pt.in.x, pt.in.y, pt.out.x, pt.out.y});
        }

        params.push_back(std::move(p));
    }

    const fx::ImageView view{*image, *width, *height, *width * 4};
    runSliced(*height, [&](int begin, int end) {
        fx::applyMasks(view, params, begin, end);
    });

    return 0;
}

mlt_frame maskProcess(mlt_filter filter, mlt_frame frame)
{
    mlt_frame_push_service_int(frame, static_cast<int>(mlt_frame_get_position(frame)));
    mlt_frame_push_service(frame, filter);
    mlt_frame_push_get_image(frame, maskGetImage);
    return frame;
}

// ---- vedit.gain -----------------------------------------------------------------------------------------------

double envelope(const GainSettings &s, double position)
{
    if (s.muted) {
        return 0.0;
    }
    double db = s.gainDb;
    if (s.gainDbParam.isAnimated()) {
        const Rational rate = (s.frameRate.num() > 0 && s.frameRate.den() > 0) ? s.frameRate : Rational(30, 1);
        const int localFrame = static_cast<int>(std::llround(position));
        const RationalTime contentTime = keyframeTime(s.sourceIn, s.speed, s.reversed, RationalTime(s.length, rate),
                                                      RationalTime(localFrame, rate));
        db = s.gainDbParam.numberAt(contentTime, s.gainDb);
    }
    double gain = fx::dbToGain(db);
    if (s.fadeInFrames > 0 && position < s.fadeInFrames) {
        gain *= std::clamp(position / s.fadeInFrames, 0.0, 1.0);
    }
    if (s.fadeOutFrames > 0 && s.length > 0 && position > s.length - s.fadeOutFrames) {
        gain *= std::clamp((s.length - position) / s.fadeOutFrames, 0.0, 1.0);
    }
    return gain;
}

int gainGetAudio(mlt_frame frame, void **buffer, mlt_audio_format *format, int *frequency, int *channels, int *samples)
{
    auto filter = static_cast<mlt_filter>(mlt_frame_pop_audio(frame));
    const auto framePosition = static_cast<double>(reinterpret_cast<intptr_t>(mlt_frame_pop_audio(frame)));
    *format = mlt_audio_f32le;
    const int error = mlt_frame_get_audio(frame, buffer, format, frequency, channels, samples);
    const GainSettings *s = settingsOf<GainSettings>(MLT_FILTER_PROPERTIES(filter));
    if (error || !s || !*buffer || *format != mlt_audio_f32le) {
        return error;
    }
    // A filter attached to a cut sees the positions of the source (verified by tst_services): local = − in point.
    const double position = framePosition - s->firstFrame;
    const float peak = fx::applyGain(static_cast<float *>(*buffer), *channels, *samples,
                                     static_cast<float>(envelope(*s, position)), static_cast<float>(envelope(*s, position + 1.0)),
                                     static_cast<float>(s->pan));
    if (!s->meterKey.isEmpty()) {
        AudioMeters::report(s->meterKey, peak);
    }
    return 0;
}

mlt_frame gainProcess(mlt_filter filter, mlt_frame frame)
{
    const intptr_t position = mlt_frame_get_position(frame);
    mlt_frame_push_audio(frame, reinterpret_cast<void *>(position));
    mlt_frame_push_audio(frame, filter);
    mlt_frame_push_audio(frame, reinterpret_cast<void *>(gainGetAudio));
    return frame;
}

// ---- vedit.transition -----------------------------------------------------------------------------------------

int transitionGetImage(mlt_frame aFrame, uint8_t **image, mlt_image_format *format, int *width, int *height, int)
{
    mlt_frame bFrame = mlt_frame_pop_frame(aFrame);
    auto transition = static_cast<mlt_transition>(mlt_frame_pop_service(aFrame));
    const int length = mlt_frame_pop_service_int(aFrame);
    const int position = mlt_frame_pop_service_int(aFrame);
    int w = *width;
    int h = *height;
    profileSize(MLT_TRANSITION_SERVICE(transition), w, h);
    *format = mlt_image_rgba;
    uint8_t *a = nullptr;
    int aw = w, ah = h;
    int error = mlt_frame_get_image(aFrame, &a, format, &aw, &ah, 0);
    if (error || !a) {
        return error ? error : 1;
    }
    mlt_image_format bFormat = mlt_image_rgba;
    uint8_t *b = nullptr;
    int bw = w, bh = h;
    const TransitionSettings *s = settingsOf<TransitionSettings>(MLT_TRANSITION_PROPERTIES(transition));
    if (!bFrame || mlt_frame_get_image(bFrame, &b, &bFormat, &bw, &bh, 0) != 0 || !b || bw != aw || bh != ah || !s) {
        *image = a;
        *width = aw;
        *height = ah;
        return 0;
    }
    const int size = aw * ah * 4;
    auto *out = static_cast<uint8_t *>(mlt_pool_alloc(size));
    // Strictly between the two clips on every frame of the transition: (k + 1) / (n + 1).
    const double progress = fx::ease(s->easing, (position + 1.0) / (std::max(1, length) + 1.0));
    const fx::ImageView target{out, aw, ah, aw * 4};
    const fx::ConstImageView av{a, aw, ah, aw * 4};
    const fx::ConstImageView bv{b, bw, bh, bw * 4};
    const fx::TransitionParams params{s->softness};
    runSliced(ah, [&](int begin, int end) { fx::renderTransition(s->kind, target, av, bv, progress, params, begin, end); });
    mlt_frame_set_image(aFrame, out, size, mlt_pool_release);
    *image = out;
    *width = aw;
    *height = ah;
    *format = mlt_image_rgba;
    return 0;
}

mlt_frame transitionProcess(mlt_transition transition, mlt_frame aFrame, mlt_frame bFrame)
{
    mlt_frame_push_service_int(aFrame, static_cast<int>(mlt_transition_get_position(transition, aFrame)));
    mlt_frame_push_service_int(aFrame, static_cast<int>(mlt_transition_get_length(transition)));
    mlt_frame_push_service(aFrame, transition);
    mlt_frame_push_frame(aFrame, bFrame);
    mlt_frame_push_get_image(aFrame, transitionGetImage);
    return aFrame;
}

// ---- vedit.text -----------------------------------------------------------------------------------------------

// The layer is drawn on the calling thread (the projection's): fonts are never used in
// MLT's threads. Besides the cost, Qt's per-thread FreeType data leaks when such a thread exits (D-39).
// For animated text, frames across the animation duration are pre-rendered into `frames`.
// Subsequent frames reuse the last (fully completed) frame, or loop for continuous effects like Wave.
struct TextState
{
    std::vector<QImage> frames; // pre-rendered at profile size
    bool isLoop = false;
    QMutex mutex;
    QImage scaled; // the last other size asked for
    int lastScaledIndex = -1;
};

int textGetImage(mlt_frame frame, uint8_t **image, mlt_image_format *format, int *width, int *height, int)
{
    auto producer = static_cast<mlt_producer>(mlt_frame_pop_service(frame));
    const int framePosition = mlt_frame_pop_service_int(frame);
    auto *state = static_cast<TextState *>(mlt_properties_get_data(MLT_PRODUCER_PROPERTIES(producer), kSettings, nullptr));
    int w = *width;
    int h = *height;
    profileSize(MLT_PRODUCER_SERVICE(producer), w, h);
    if (!state || state->frames.empty()) {
        const int size = w * h * 4;
        auto *buffer = static_cast<uint8_t *>(mlt_pool_alloc(size));
        std::memset(buffer, 0, static_cast<size_t>(size));
        mlt_frame_set_image(frame, buffer, size, mlt_pool_release);
        *image = buffer;
        *width = w;
        *height = h;
        *format = mlt_image_rgba;
        return 0;
    }

    const int total = static_cast<int>(state->frames.size());
    const int frameIndex = state->isLoop ? ((framePosition >= 0) ? (framePosition % total) : 0)
                                         : std::clamp(framePosition, 0, total - 1);
    QImage layer = state->frames[static_cast<size_t>(frameIndex)];
    if (!layer.isNull() && layer.size() != QSize(w, h)) {
        QMutexLocker lock(&state->mutex);
        if (state->scaled.size() != QSize(w, h) || state->lastScaledIndex != frameIndex) {
            state->scaled = layer.scaled(w, h, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                                .convertToFormat(QImage::Format_RGBA8888);
            state->lastScaledIndex = frameIndex;
        }
        layer = state->scaled;
    }
    const int size = w * h * 4;
    auto *buffer = static_cast<uint8_t *>(mlt_pool_alloc(size));
    if (layer.size() != QSize(w, h)) {
        std::memset(buffer, 0, static_cast<size_t>(size));
    } else {
        for (int y = 0; y < h; ++y) {
            std::memcpy(buffer + y * w * 4, layer.constScanLine(y), static_cast<size_t>(w * 4));
        }
    }
    mlt_frame_set_image(frame, buffer, size, mlt_pool_release);
    *image = buffer;
    *width = w;
    *height = h;
    *format = mlt_image_rgba;
    return 0;
}

int textGetFrame(mlt_producer producer, mlt_frame_ptr frame, int)
{
    *frame = mlt_frame_init(MLT_PRODUCER_SERVICE(producer));
    if (*frame) {
        mlt_properties properties = MLT_FRAME_PROPERTIES(*frame);
        const int pos = static_cast<int>(mlt_producer_position(producer));
        mlt_frame_set_position(*frame, pos);
        mlt_properties_set_int(properties, "progressive", 1);
        mlt_properties_set_int(properties, "test_audio", 1); // no sound
        mlt_frame_push_service_int(*frame, pos);
        mlt_frame_push_service(*frame, producer);
        mlt_frame_push_get_image(*frame, textGetImage);
    }
    mlt_producer_prepare_next(producer);
    return 0;
}

void *createText(mlt_profile profile, mlt_service_type, const char *, const void *)
{
    mlt_producer producer = mlt_producer_new(profile);
    if (producer) {
        producer->get_frame = textGetFrame;
        mlt_properties properties = MLT_PRODUCER_PROPERTIES(producer);
        mlt_properties_set_position(properties, "length", 0x7fffffff);
        mlt_properties_set_position(properties, "out", 0x7ffffffe);
    }
    return producer;
}

// ---- vedit.speed_ramp -----------------------------------------------------------------------------------------

struct SpeedRampState
{
    std::shared_ptr<Mlt::Producer> base;
    SpeedCurve curve;
    int sourceIn = 0;
    int length = 0;
    bool reversed = false;
    QMutex mutex;
};

int speedRampGetFrame(mlt_producer producer, mlt_frame_ptr frame, int index)
{
    auto *state = static_cast<SpeedRampState *>(mlt_properties_get_data(MLT_PRODUCER_PROPERTIES(producer), kSettings, nullptr));
    if (!state || !state->base || state->length <= 0) {
        *frame = mlt_frame_init(MLT_PRODUCER_SERVICE(producer));
        mlt_producer_prepare_next(producer);
        return 0;
    }

    const int pos = static_cast<int>(mlt_producer_position(producer));
    const double u = std::clamp(static_cast<double>(pos) / std::max(1, state->length - 1), 0.0, 1.0);
    const double integral = SpeedCurveUtil::integratedTime(state->curve, u);
    const double sourceFramesFromStart = integral * static_cast<double>(state->length);
    int mappedSourceFrame = 0;
    if (!state->reversed) {
        mappedSourceFrame = state->sourceIn + static_cast<int>(std::round(sourceFramesFromStart));
    } else {
        const int totalSource = static_cast<int>(std::round(SpeedCurveUtil::averageSpeed(state->curve) * static_cast<double>(state->length)));
        mappedSourceFrame = state->sourceIn + totalSource - 1 - static_cast<int>(std::round(sourceFramesFromStart));
    }

    QMutexLocker lock(&state->mutex);
    state->base->seek(mappedSourceFrame);
    const int error = mlt_service_get_frame(MLT_PRODUCER_SERVICE(state->base->get_producer()), frame, index);
    if (!error && *frame) {
        mlt_frame_set_position(*frame, pos);
    } else {
        *frame = mlt_frame_init(MLT_PRODUCER_SERVICE(producer));
    }
    mlt_producer_prepare_next(producer);
    return 0;
}

void *createSpeedRamp(mlt_profile profile, mlt_service_type, const char *, const void *)
{
    mlt_producer producer = mlt_producer_new(profile);
    if (producer) {
        producer->get_frame = speedRampGetFrame;
        mlt_properties properties = MLT_PRODUCER_PROPERTIES(producer);
        mlt_properties_set_position(properties, "length", 0x7fffffff);
        mlt_properties_set_position(properties, "out", 0x7ffffffe);
    }
    return producer;
}

template<mlt_frame (*Process)(mlt_filter, mlt_frame)>
void *createFilter(mlt_profile, mlt_service_type, const char *, const void *)
{
    mlt_filter filter = mlt_filter_new();
    if (filter) {
        filter->process = Process;
    }
    return filter;
}

void *createTransitionService(mlt_profile, mlt_service_type, const char *, const void *)
{
    mlt_transition transition = mlt_transition_new();
    if (transition) {
        transition->process = transitionProcess;
        mlt_properties_set_int(MLT_TRANSITION_PROPERTIES(transition), "_transition_type", 1); // video only
    }
    return transition;
}

// ---- vedit.sticker --------------------------------------------------------------------------------------------

// The pictures are decoded on the projection's thread (makeStickerProducer); MLT's threads only scale and copy.
struct StickerState
{
    StickerPicture picture;
    bool loop = true;
    double speed = 1.0;
    double fps = 30.0;
    QMutex mutex;
    QImage scaled; // the last frame asked for, fitted in the last size asked for
    int scaledIndex = -1;
};

int stickerFrameIndex(const StickerState &state, int position)
{
    const int count = static_cast<int>(state.picture.frames.size());
    if (count <= 1) {
        return 0;
    }
    int total = 0;
    for (int i = 0; i < count; ++i) {
        total += state.picture.delayOf(i);
    }
    auto ms = static_cast<std::int64_t>(std::llround(position / state.fps * state.speed * 1000.0));
    ms = state.loop ? ((ms % total) + total) % total : std::clamp<std::int64_t>(ms, 0, total - 1);
    for (int i = 0; i < count; ++i) {
        ms -= state.picture.delayOf(i);
        if (ms < 0) {
            return i;
        }
    }
    return count - 1;
}

// A canvas-sized layer (w × h) with the picture fitted inside and centred, like a text layer.
void fitCentred(const QImage &picture, QImage &layer, int w, int h)
{
    layer = QImage(w, h, QImage::Format_RGBA8888);
    layer.fill(Qt::transparent);
    if (picture.isNull()) {
        return;
    }
    const QSize size = picture.size().scaled(w, h, Qt::KeepAspectRatio);
    QPainter painter(&layer);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.drawImage(QRect((w - size.width()) / 2, (h - size.height()) / 2, size.width(), size.height()), picture);
}

void setTransparent(mlt_frame frame, uint8_t **image, mlt_image_format *format, int *width, int *height, int w, int h)
{
    const int size = w * h * 4;
    auto *buffer = static_cast<uint8_t *>(mlt_pool_alloc(size));
    std::memset(buffer, 0, static_cast<size_t>(size));
    mlt_frame_set_image(frame, buffer, size, mlt_pool_release);
    *image = buffer;
    *width = w;
    *height = h;
    *format = mlt_image_rgba;
}

void setLayer(mlt_frame frame, const QImage &layer, uint8_t **image, mlt_image_format *format, int *width, int *height)
{
    const int w = layer.width();
    const int h = layer.height();
    const int size = w * h * 4;
    auto *buffer = static_cast<uint8_t *>(mlt_pool_alloc(size));
    for (int y = 0; y < h; ++y) {
        std::memcpy(buffer + static_cast<std::ptrdiff_t>(y) * w * 4, layer.constScanLine(y), static_cast<size_t>(w) * 4);
    }
    mlt_frame_set_image(frame, buffer, size, mlt_pool_release);
    *image = buffer;
    *width = w;
    *height = h;
    *format = mlt_image_rgba;
}

int stickerGetImage(mlt_frame frame, uint8_t **image, mlt_image_format *format, int *width, int *height, int)
{
    auto producer = static_cast<mlt_producer>(mlt_frame_pop_service(frame));
    const int position = mlt_frame_pop_service_int(frame);
    auto *state = static_cast<StickerState *>(mlt_properties_get_data(MLT_PRODUCER_PROPERTIES(producer), kSettings, nullptr));
    int w = *width;
    int h = *height;
    profileSize(MLT_PRODUCER_SERVICE(producer), w, h);
    if (!state || state->picture.frames.empty()) {
        setTransparent(frame, image, format, width, height, w, h);
        return 0;
    }
    const int index = stickerFrameIndex(*state, position);
    QImage layer;
    {
        QMutexLocker lock(&state->mutex);
        if (state->scaledIndex != index || state->scaled.size() != QSize(w, h)) {
            fitCentred(state->picture.frames[static_cast<size_t>(index)], state->scaled, w, h);
            state->scaledIndex = index;
        }
        layer = state->scaled;
    }
    setLayer(frame, layer, image, format, width, height);
    return 0;
}

// Producers drawing a layer from their own position (stickers, visualizers): no sound.
template<int (*GetImage)(mlt_frame, uint8_t **, mlt_image_format *, int *, int *, int)>
int layerGetFrame(mlt_producer producer, mlt_frame_ptr frame, int)
{
    *frame = mlt_frame_init(MLT_PRODUCER_SERVICE(producer));
    if (*frame) {
        mlt_properties properties = MLT_FRAME_PROPERTIES(*frame);
        const int pos = static_cast<int>(mlt_producer_position(producer));
        mlt_frame_set_position(*frame, pos);
        mlt_properties_set_int(properties, "progressive", 1);
        mlt_properties_set_int(properties, "test_audio", 1);
        mlt_frame_push_service_int(*frame, pos);
        mlt_frame_push_service(*frame, producer);
        mlt_frame_push_get_image(*frame, GetImage);
    }
    mlt_producer_prepare_next(producer);
    return 0;
}

template<int (*GetImage)(mlt_frame, uint8_t **, mlt_image_format *, int *, int *, int)>
void *createLayerProducer(mlt_profile profile, mlt_service_type, const char *, const void *)
{
    mlt_producer producer = mlt_producer_new(profile);
    if (producer) {
        producer->get_frame = layerGetFrame<GetImage>;
        mlt_properties properties = MLT_PRODUCER_PROPERTIES(producer);
        mlt_properties_set_position(properties, "length", 0x7fffffff);
        mlt_properties_set_position(properties, "out", 0x7ffffffe);
    }
    return producer;
}

// ---- vedit.graphic -------------------------------------------------------------------------------------------

struct GraphicState
{
    fx::GraphicParams params;
    fx::GraphicGlyphs glyphs; // at the profile's size
    QSize canvas;             // the profile's
    int length = 1;
    double fps = 30.0;
};

int graphicGetImage(mlt_frame frame, uint8_t **image, mlt_image_format *format, int *width, int *height, int)
{
    auto producer = static_cast<mlt_producer>(mlt_frame_pop_service(frame));
    const int position = mlt_frame_pop_service_int(frame);
    const auto *state = static_cast<const GraphicState *>(mlt_properties_get_data(MLT_PRODUCER_PROPERTIES(producer), kSettings, nullptr));
    int w = *width;
    int h = *height;
    profileSize(MLT_PRODUCER_SERVICE(producer), w, h);
    if (!state) {
        setTransparent(frame, image, format, width, height, w, h);
        return 0;
    }
    QImage layer = fx::renderGraphic(state->params, position / state->fps, state->length / state->fps, state->canvas, state->glyphs);
    if (layer.size() != QSize(w, h)) {
        layer = layer.scaled(w, h, Qt::IgnoreAspectRatio, Qt::SmoothTransformation).convertToFormat(QImage::Format_RGBA8888);
    }
    setLayer(frame, layer, image, format, width, height);
    return 0;
}

// ---- vedit.visualizer -----------------------------------------------------------------------------------------

struct VisualizerState
{
    VisualizerRender render;
    double fps = 30.0;
};

// Band levels at `frame` of the clip: the loudest of the audio playing then, averaged over the smoothing window.
void visualizerLevels(const VisualizerState &state, int frame, std::vector<float> &levels)
{
    const VisualizerRender &render = state.render;
    const int window = std::max(1, static_cast<int>(std::lround(render.smoothing * 0.25 * state.fps)) + 1);
    std::vector<float> sample;
    int bands = 0;
    for (const VisualizerAudio &audio : render.audio) {
        if (audio.spectrum) {
            bands = std::max(bands, audio.spectrum->bands);
        }
    }
    levels.assign(static_cast<size_t>(bands), 0.0f);
    if (bands == 0) {
        return;
    }
    for (int step = 0; step < window; ++step) {
        const int f = frame - step;
        std::vector<float> loudest(static_cast<size_t>(bands), 0.0f);
        for (const VisualizerAudio &audio : render.audio) {
            if (!audio.spectrum || f < audio.startFrame || f >= audio.endFrame) {
                continue;
            }
            const double seconds = audio.sourceSeconds + (f - audio.startFrame) / state.fps * audio.speed;
            audio.spectrum->levelsAt(seconds, sample);
            for (size_t b = 0; b < sample.size() && b < loudest.size(); ++b) {
                loudest[b] = std::max(loudest[b], sample[b]);
            }
        }
        for (size_t b = 0; b < levels.size(); ++b) {
            levels[b] += loudest[b] / static_cast<float>(window);
        }
    }
}

int visualizerGetImage(mlt_frame frame, uint8_t **image, mlt_image_format *format, int *width, int *height, int)
{
    auto producer = static_cast<mlt_producer>(mlt_frame_pop_service(frame));
    const int position = mlt_frame_pop_service_int(frame);
    const auto *state = static_cast<const VisualizerState *>(
        mlt_properties_get_data(MLT_PRODUCER_PROPERTIES(producer), kSettings, nullptr));
    int w = *width;
    int h = *height;
    profileSize(MLT_PRODUCER_SERVICE(producer), w, h);
    if (!state) {
        setTransparent(frame, image, format, width, height, w, h);
        return 0;
    }
    std::vector<float> levels;
    visualizerLevels(*state, position, levels);
    const fx::VisualizerFrameData data =
        fx::visualizerFrame(levels, state->render.settings.barCount, state->render.settings.sensitivity);
    setLayer(frame, fx::renderAudioVisualizer(state->render.settings, data, QSize(w, h)), image, format, width, height);
    return 0;
}

// ---- vedit.beat -----------------------------------------------------------------------------------------------

int beatGetImage(mlt_frame frame, uint8_t **image, mlt_image_format *format, int *width, int *height, int)
{
    auto filter = static_cast<mlt_filter>(mlt_frame_pop_service(frame));
    const int position = mlt_frame_pop_service_int(frame);
    *format = mlt_image_rgba;
    const int error = mlt_frame_get_image(frame, image, format, width, height, 1);
    const auto *s = settingsOf<BeatEffectSettings>(MLT_FILTER_PROPERTIES(filter));
    if (error || !s || *format != mlt_image_rgba || !*image) {
        return error;
    }
    const double rate = s->frameRate.toDouble() > 0 ? s->frameRate.toDouble() : 30.0;
    const double seconds = (position - s->firstFrame) / rate;
    const double pulse = fx::computeBeatPulse(seconds, s->beats, s->decay);
    if (pulse <= 0.001) {
        return 0;
    }
    const int w = *width;
    const int h = *height;
    switch (s->kind) {
    case BeatEffectSettings::Kind::Flash:
        fx::applyBeatFlash(*image, w, h, pulse, s->amount);
        break;
    case BeatEffectSettings::Kind::Zoom:
    case BeatEffectSettings::Kind::Shake: {
        const std::vector<uint8_t> source(*image, *image + static_cast<std::ptrdiff_t>(w) * h * 4);
        if (s->kind == BeatEffectSettings::Kind::Zoom) {
            fx::applyBeatZoom(*image, source.data(), w, h, pulse, s->amount);
        } else {
            // The amplitude is given in pixels at 1080p.
            fx::applyBeatShake(*image, source.data(), w, h, pulse, seconds, s->amount * h / 1080.0);
        }
        break;
    }
    }
    return 0;
}

// ---- vedit.effect ---------------------------------------------------------------------------------------------

int videoEffectGetImage(mlt_frame frame, uint8_t **image, mlt_image_format *format, int *width, int *height, int)
{
    auto filter = static_cast<mlt_filter>(mlt_frame_pop_service(frame));
    const int position = mlt_frame_pop_service_int(frame);
    *format = mlt_image_rgba;
    const int error = mlt_frame_get_image(frame, image, format, width, height, 1);
    const auto *s = settingsOf<VideoEffectSettings>(MLT_FILTER_PROPERTIES(filter));
    if (error || !s || *format != mlt_image_rgba || !*image || s->mix <= 0.001) {
        return error;
    }
    const int w = *width;
    const int h = *height;
    const fx::ImageView view{*image, w, h, w * 4};
    fx::VideoEffectParams params = s->params;
    const double rate = s->frameRate.toDouble() > 0 ? s->frameRate.toDouble() : 30.0;
    params.time = (position - s->firstFrame) / rate;
    if (s->mix >= 0.999) {
        fx::renderVideoEffect(s->kernel, view, params);
        return 0;
    }
    const std::vector<uint8_t> original(*image, *image + static_cast<std::ptrdiff_t>(w) * h * 4);
    fx::renderVideoEffect(s->kernel, view, params);
    for (size_t i = 0; i < original.size(); ++i) {
        if (i % 4 != 3) {
            (*image)[i] = static_cast<uint8_t>(std::lround(original[i] + ((*image)[i] - original[i]) * s->mix));
        }
    }
    return 0;
}

mlt_frame videoEffectProcess(mlt_filter filter, mlt_frame frame)
{
    mlt_frame_push_service_int(frame, static_cast<int>(mlt_frame_get_position(frame)));
    mlt_frame_push_service(frame, filter);
    mlt_frame_push_get_image(frame, videoEffectGetImage);
    return frame;
}

mlt_frame beatProcess(mlt_filter filter, mlt_frame frame)
{
    mlt_frame_push_service_int(frame, static_cast<int>(mlt_frame_get_position(frame)));
    mlt_frame_push_service(frame, filter);
    mlt_frame_push_get_image(frame, beatGetImage);
    return frame;
}

void appendDouble(QDataStream &stream, double value)
{
    stream << value;
}

} // namespace

void registerServices(Mlt::Repository *repository)
{
    if (!repository) {
        return;
    }
    repository->register_service(mlt_service_transition_type, "vedit.composite", createComposite);
    repository->register_service(mlt_service_filter_type, "vedit.transform", createFilter<transformProcess>);
    repository->register_service(mlt_service_filter_type, "vedit.adjust", createFilter<adjustProcess>);
    repository->register_service(mlt_service_filter_type, "vedit.chroma_key", createFilter<chromaKeyProcess>);
    repository->register_service(mlt_service_filter_type, "vedit.motion_blur", createFilter<motionBlurProcess>);
    repository->register_service(mlt_service_filter_type, "vedit.mask", createFilter<maskProcess>);
    repository->register_service(mlt_service_filter_type, "vedit.gain", createFilter<gainProcess>);
    repository->register_service(mlt_service_transition_type, "vedit.transition", createTransitionService);
    repository->register_service(mlt_service_producer_type, "vedit.text", createText);
    repository->register_service(mlt_service_producer_type, "vedit.speed_ramp", createSpeedRamp);
    repository->register_service(mlt_service_producer_type, "vedit.sticker", createLayerProducer<stickerGetImage>);
    repository->register_service(mlt_service_producer_type, "vedit.visualizer", createLayerProducer<visualizerGetImage>);
    repository->register_service(mlt_service_producer_type, "vedit.graphic", createLayerProducer<graphicGetImage>);
    repository->register_service(mlt_service_filter_type, "vedit.beat", createFilter<beatProcess>);
    repository->register_service(mlt_service_filter_type, "vedit.effect", createFilter<videoEffectProcess>);
}

// ---- settings -----------------------------------------------------------------------------------------------------

bool TransformSettings::isIdentityLayer() const
{
    if (!animations.isEmpty()) {
        return false;
    }
    return fit == FitMode::Stretch && x == 0 && y == 0 && scaleX == 1 && scaleY == 1 && rotation == 0 && !flipH && !flipV &&
           cropLeft == 0 && cropTop == 0 && cropRight == 0 && cropBottom == 0 && opacity == 1 && !background &&
           blendMode == fx::BlendMode::Normal && !positionParam.isAnimated() && !scaleParam.isAnimated() &&
           !rotationParam.isAnimated() && !opacityParam.isAnimated() && !cropLeftParam.isAnimated() &&
           !cropTopParam.isAnimated() && !cropRightParam.isAnimated() && !cropBottomParam.isAnimated();
}

// Every value that decides the pictures of a parameter (static value or keyframes), for the render keys: a change of
// any keyframe must give another key, or the projection would keep the old filter.
void streamValue(QDataStream &stream, const ParamValue &value)
{
    stream << quint8(value.index());
    std::visit([&stream](const auto &v) {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, double> || std::is_same_v<T, bool> || std::is_same_v<T, QString>) {
            stream << v;
        } else if constexpr (std::is_same_v<T, Color>) {
            stream << v.r << v.g << v.b << v.a;
        } else if constexpr (std::is_same_v<T, Vec2>) {
            stream << v.x << v.y;
        } else {
            stream << QJsonDocument(QJsonArray{v}).toJson(QJsonDocument::Compact);
        }
    }, value);
}

void streamParam(QDataStream &stream, const Param &param)
{
    stream << quint32(param.keyframes().size());
    if (!param.isAnimated()) {
        streamValue(stream, param.staticValue());
        return;
    }
    for (const Keyframe &keyframe : param.keyframes()) {
        stream << qint64(keyframe.time.value()) << qint64(keyframe.time.rate().num()) << qint64(keyframe.time.rate().den())
               << int(keyframe.interpolation) << keyframe.easing.name();
        for (const double b : keyframe.easing.bezier()) {
            stream << b;
        }
        streamValue(stream, keyframe.value);
    }
}

QByteArray TransformSettings::key() const
{
    QByteArray bytes;
    QDataStream stream(&bytes, QIODevice::WriteOnly);
    stream << sourceWidth << sourceHeight << int(fit) << x << y << scaleX << scaleY << rotation << flipH << flipV << cropLeft
           << cropTop << cropRight << cropBottom << opacity << background.has_value() << int(blendMode)
           << positionParam.isAnimated() << scaleParam.isAnimated() << rotationParam.isAnimated() << opacityParam.isAnimated()
           << cropLeftParam.isAnimated() << cropTopParam.isAnimated() << cropRightParam.isAnimated() << cropBottomParam.isAnimated()
           << firstFrame << clipLength << animations.isEmpty();
    if (!animations.isEmpty()) {
        stream << animations.in.has_value();
        if (animations.in) {
            stream << animations.in->type.id << qint64(animations.in->duration.value())
                   << qint64(animations.in->duration.rate().num()) << qint64(animations.in->duration.rate().den())
                   << animations.in->easing.name();
        }
        stream << animations.out.has_value();
        if (animations.out) {
            stream << animations.out->type.id << qint64(animations.out->duration.value())
                   << qint64(animations.out->duration.rate().num()) << qint64(animations.out->duration.rate().den())
                   << animations.out->easing.name();
        }
        stream << animations.loop.has_value();
        if (animations.loop) {
            stream << animations.loop->type.id << qint64(animations.loop->duration.value())
                   << qint64(animations.loop->duration.rate().num()) << qint64(animations.loop->duration.rate().den())
                   << animations.loop->easing.name();
        }
    }
    // Keyframes are read at render time (content time: sourceIn, speed, direction).
    for (const Param *param : {&positionParam, &scaleParam, &rotationParam, &opacityParam, &cropLeftParam, &cropTopParam,
                               &cropRightParam, &cropBottomParam}) {
        streamParam(stream, *param);
    }
    stream << qint64(sourceIn.value()) << qint64(sourceIn.rate().num()) << qint64(sourceIn.rate().den()) << speed << reversed;
    if (background) {
        stream << int(background->type) << background->color.toString() << background->amount;
    }
    return bytes;
}

QByteArray ChromaKeySettings::key() const
{
    QByteArray bytes;
    QDataStream stream(&bytes, QIODevice::WriteOnly);
    stream << keyColor.r << keyColor.g << keyColor.b << similarity << smoothness << spill;
    return bytes;
}

QByteArray MaskSettings::key() const
{
    QByteArray bytes;
    QDataStream stream(&bytes, QIODevice::WriteOnly);
    stream << quint32(masks.size()) << firstFrame << clipLength << speed << reversed << qint64(sourceIn.value())
           << qint64(sourceIn.rate().num()) << qint64(sourceIn.rate().den());
    for (const Mask &m : masks) {
        for (const Param *param : {&m.center, &m.size, &m.rotation, &m.roundness, &m.feather}) {
            streamParam(stream, *param);
        }
        stream << int(m.shape) << m.invert << m.center.isAnimated() << m.size.isAnimated()
               << m.rotation.isAnimated() << m.roundness.isAnimated() << m.feather.isAnimated()
               << quint32(m.points.size());
        if (!m.center.isAnimated()) {
            if (const auto *v = std::get_if<Vec2>(&m.center.staticValue())) {
                stream << v->x << v->y;
            }
        }
        if (!m.size.isAnimated()) {
            if (const auto *v = std::get_if<Vec2>(&m.size.staticValue())) {
                stream << v->x << v->y;
            }
        }
        if (!m.rotation.isAnimated()) {
            stream << m.rotation.numberAt(RationalTime(0, 1));
        }
        if (!m.roundness.isAnimated()) {
            stream << m.roundness.numberAt(RationalTime(0, 1));
        }
        if (!m.feather.isAnimated()) {
            stream << m.feather.numberAt(RationalTime(0, 1));
        }
        for (const auto &p : m.points) {
            stream << p.p.x << p.p.y << p.in.x << p.in.y << p.out.x << p.out.y;
        }
    }
    return bytes;
}

QByteArray AdjustSettings::key() const
{
    QByteArray bytes;
    QDataStream stream(&bytes, QIODevice::WriteOnly);
    const fx::ColorAdjust &a = look;
    for (double v : {a.exposure, a.brightness, a.contrast, a.highlights, a.shadows, a.whites, a.blacks, a.saturation, a.vibrance,
                     a.temperature, a.tint, a.fade, a.splitAmount, intensity, vignette, grain, sharpness}) {
        appendDouble(stream, v);
    }
    for (double v : a.shadowTone) {
        appendDouble(stream, v);
    }
    for (double v : a.highlightTone) {
        appendDouble(stream, v);
    }
    for (const auto &[x, y] : a.curve) {
        stream << x << y;
    }
    stream << QJsonDocument(grade.toJson()).toJson(QJsonDocument::Compact) << cubeKey;
    return bytes;
}

QByteArray GainSettings::key() const
{
    QByteArray bytes;
    QDataStream stream(&bytes, QIODevice::WriteOnly);
    stream << gainDb << muted << pan << fadeInFrames << fadeOutFrames << length << firstFrame << meterKey;
    streamParam(stream, gainDbParam);
    if (gainDbParam.isAnimated()) {
        stream << qint64(sourceIn.value()) << qint64(sourceIn.rate().num()) << qint64(sourceIn.rate().den()) << speed << reversed;
    }
    return bytes;
}

QByteArray AudioEffectsSettings::key() const
{
    QByteArray bytes;
    QDataStream stream(&bytes, QIODevice::WriteOnly);
    stream << denoise << denoiseAmount << voiceEffect << eqLow << eqMid << eqHigh << compressor
           << compressorThreshold << compressorRatio;
    return bytes;
}

std::unique_ptr<Mlt::Filter> makeTransformFilter(Mlt::Profile &profile, const TransformSettings &settings)
{
    auto filter = std::make_unique<Mlt::Filter>(profile, "vedit.transform");
    TransformSettings copy = settings;
    if (copy.frameRate.num() <= 0 || copy.frameRate.den() <= 0) {
        copy.frameRate = Rational(profile.fps(), 1);
    }
    attachSettings(*filter, copy);
    return filter;
}

std::unique_ptr<Mlt::Filter> makeAdjustFilter(Mlt::Profile &profile, const AdjustSettings &settings)
{
    auto filter = std::make_unique<Mlt::Filter>(profile, "vedit.adjust");
    // One LUT for the whole colour transform: adjustments, then grade, then the .cube (docs/ARCHITECTURE.md §22).
    auto *state = new AdjustState{settings, fx::ColorLut([&settings](const std::array<double, 3> &rgb) {
                                      std::array<double, 3> c = fx::ColorLut::evaluate(settings.look, rgb);
                                      if (!settings.grade.isIdentity()) {
                                          c = fx::applyGrade(settings.grade, c);
                                      }
                                      return settings.cube ? settings.cube->sample(c) : c;
                                  })};
    filter->set(kSettings, state, 0, [](void *p) { delete static_cast<AdjustState *>(p); });
    return filter;
}

std::unique_ptr<Mlt::Filter> makeDeflickerFilter(Mlt::Profile &profile, const DeflickerSettings &settings)
{
    auto filter = std::make_unique<Mlt::Filter>(profile, "avfilter.deflicker");
    filter->set("av.size", settings.size);
    if (!settings.mode.isEmpty()) {
        filter->set("av.mode", settings.mode.toUtf8().constData());
    }
    return filter;
}

std::unique_ptr<Mlt::Filter> makeChromaKeyFilter(Mlt::Profile &profile, const ChromaKeySettings &settings)
{
    auto filter = std::make_unique<Mlt::Filter>(profile, "vedit.chroma_key");
    attachSettings(*filter, settings);
    return filter;
}

std::unique_ptr<Mlt::Filter> makeMaskFilter(Mlt::Profile &profile, const MaskSettings &settings)
{
    auto filter = std::make_unique<Mlt::Filter>(profile, "vedit.mask");
    MaskSettings copy = settings;
    if (copy.frameRate.num() <= 0 || copy.frameRate.den() <= 0) {
        copy.frameRate = Rational(profile.fps(), 1);
    }
    attachSettings(*filter, copy);
    return filter;
}

std::unique_ptr<Mlt::Filter> makeGainFilter(Mlt::Profile &profile, const GainSettings &settings)
{
    auto filter = std::make_unique<Mlt::Filter>(profile, "vedit.gain");
    attachSettings(*filter, settings);
    return filter;
}

std::unique_ptr<Mlt::Transition> makeTransition(Mlt::Profile &profile, const TransitionSettings &settings)
{
    auto transition = std::make_unique<Mlt::Transition>(profile, "vedit.transition");
    attachSettings(*transition, settings);
    return transition;
}

std::unique_ptr<Mlt::Producer> makeTextProducer(Mlt::Profile &profile, const TextClipData &text)
{
    auto producer = std::make_unique<Mlt::Producer>(profile, "vedit.text");
    auto *state = new TextState;
    const QSize canvasSize(profile.width(), profile.height());
    if (!text.animation || text.animation->type == TextAnimationType::None) {
        state->frames.push_back(TextRenderer::render(text, canvasSize));
    } else {
        const double fps = profile.fps() > 0 ? profile.fps() : 30.0;
        const double duration = (text.animation->duration.toSecondsDouble() > 0.05)
                                    ? text.animation->duration.toSecondsDouble()
                                    : 1.5;
        const int totalFrames = std::clamp(static_cast<int>(std::ceil(duration * fps)), 2, 120);
        state->isLoop = (text.animation->type == TextAnimationType::Wave);
        state->frames.reserve(static_cast<size_t>(totalFrames));
        for (int f = 0; f < totalFrames; ++f) {
            const double t = static_cast<double>(f) / fps;
            state->frames.push_back(TextRenderer::render(text, canvasSize, t, duration));
        }
    }
    producer->set(kSettings, state, 0, [](void *p) { delete static_cast<TextState *>(p); });
    return producer;
}

std::unique_ptr<Mlt::Filter> makeMotionBlurFilter(Mlt::Profile &profile, const fx::MotionBlurSettings &settings)
{
    auto filter = std::make_unique<Mlt::Filter>(profile, "vedit.motion_blur");
    attachSettings(*filter, settings);
    return filter;
}

std::unique_ptr<Mlt::Producer> makeSpeedRampProducer(Mlt::Profile &profile,
                                                     std::shared_ptr<Mlt::Producer> baseProducer,
                                                     const SpeedCurve &curve,
                                                     int sourceIn,
                                                     int length,
                                                     bool reversed)
{
    auto producer = std::make_unique<Mlt::Producer>(profile, "vedit.speed_ramp");
    auto *state = new SpeedRampState;
    state->base = std::shared_ptr<Mlt::Producer>(baseProducer ? baseProducer->cut(0, baseProducer->get_length() - 1) : nullptr);
    state->curve = curve;
    state->sourceIn = sourceIn;
    state->length = length;
    state->reversed = reversed;
    producer->set("length", length);
    producer->set("out", length - 1);
    producer->set(kSettings, state, 0, [](void *p) { delete static_cast<SpeedRampState *>(p); });
    return producer;
}

int StickerPicture::delayOf(int index) const
{
    const int delay = index >= 0 && index < static_cast<int>(delaysMs.size()) ? delaysMs[static_cast<size_t>(index)] : 0;
    return delay > 0 ? delay : 100; // GIFs without a delay play at 10 fps, as browsers do
}

StickerPicture loadStickerPicture(const QString &path, int maxSide)
{
    StickerPicture picture;
    if (path.endsWith(QStringLiteral(".svg"), Qt::CaseInsensitive) || path.endsWith(QStringLiteral(".svgz"), Qt::CaseInsensitive)) {
        QSvgRenderer renderer(path);
        if (!renderer.isValid()) {
            return picture;
        }
        const QSize natural = renderer.defaultSize().isValid() ? renderer.defaultSize() : QSize(maxSide, maxSide);
        QImage image(natural.scaled(maxSide, maxSide, Qt::KeepAspectRatio), QImage::Format_RGBA8888);
        image.fill(Qt::transparent);
        QPainter painter(&image);
        renderer.render(&painter);
        painter.end();
        picture.frames.push_back(std::move(image));
        return picture;
    }
    QImageReader reader(path);
    reader.setDecideFormatFromContent(true);
    const bool animated = reader.supportsAnimation() && reader.imageCount() != 1;
    do {
        const int delay = reader.nextImageDelay();
        QImage frame = reader.read();
        if (frame.isNull()) {
            break;
        }
        if (std::max(frame.width(), frame.height()) > maxSide) {
            frame = frame.scaled(maxSide, maxSide, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        }
        picture.frames.push_back(frame.convertToFormat(QImage::Format_RGBA8888));
        picture.delaysMs.push_back(delay);
    } while (animated && reader.canRead());
    return picture;
}

StickerPicture renderEmoji(const QString &emoji, int size)
{
    StickerPicture picture;
    QImage image(size, size, QImage::Format_RGBA8888);
    image.fill(Qt::transparent);
    // The family is a hint: without it fontconfig falls back to any colour emoji font it has.
    QFont font(QStringLiteral("Noto Color Emoji"));
    font.setPixelSize(static_cast<int>(size * 0.8));
    QPainter painter(&image);
    painter.setFont(font);
    painter.drawText(QRect(0, 0, size, size), Qt::AlignCenter, emoji);
    painter.end();
    picture.frames.push_back(std::move(image));
    return picture;
}

void tintPicture(StickerPicture &picture, const Color &tint)
{
    if (tint.a == 0) {
        return;
    }
    // Recoloured towards the tint, keeping the shading (luminance) and the alpha.
    const double amount = tint.a / 255.0;
    for (QImage &frame : picture.frames) {
        for (int y = 0; y < frame.height(); ++y) {
            auto *p = frame.scanLine(y);
            for (int x = 0; x < frame.width(); ++x, p += 4) {
                const double luma = (0.2126 * p[0] + 0.7152 * p[1] + 0.0722 * p[2]) / 255.0;
                const auto mix = [&](uint8_t value, int target) {
                    return static_cast<uint8_t>(std::lround(value * (1.0 - amount) + target * luma * amount));
                };
                p[0] = mix(p[0], tint.r);
                p[1] = mix(p[1], tint.g);
                p[2] = mix(p[2], tint.b);
            }
        }
    }
}

std::unique_ptr<Mlt::Producer> makeStickerProducer(Mlt::Profile &profile, StickerPicture picture, bool loop, double speed)
{
    auto producer = std::make_unique<Mlt::Producer>(profile, "vedit.sticker");
    auto *state = new StickerState;
    state->picture = std::move(picture);
    state->loop = loop;
    state->speed = std::clamp(speed, 0.1, 10.0);
    state->fps = profile.fps() > 0 ? profile.fps() : 30.0;
    producer->set(kSettings, state, 0, [](void *p) { delete static_cast<StickerState *>(p); });
    return producer;
}

std::unique_ptr<Mlt::Producer> makeVisualizerProducer(Mlt::Profile &profile, VisualizerRender render)
{
    auto producer = std::make_unique<Mlt::Producer>(profile, "vedit.visualizer");
    auto *state = new VisualizerState;
    state->render = std::move(render);
    state->fps = profile.fps() > 0 ? profile.fps() : 30.0;
    producer->set(kSettings, state, 0, [](void *p) { delete static_cast<VisualizerState *>(p); });
    return producer;
}

fx::GraphicParams graphicParams(const GraphicSettings &settings)
{
    const auto color = [](const Color &c) { return QColor(c.r, c.g, c.b, c.a); };
    fx::GraphicParams params;
    params.type = static_cast<fx::GraphicType>(settings.kind);
    params.from = settings.from;
    params.to = settings.to;
    params.decimals = settings.decimals;
    params.color = color(settings.color);
    params.color2 = color(settings.color2);
    params.thickness = settings.thickness;
    params.drawSeconds = settings.drawSeconds;
    return params;
}

fx::GraphicGlyphs makeGraphicGlyphs(const GraphicSettings &settings, int canvasHeight)
{
    fx::GraphicGlyphs glyphs;
    const fx::GraphicParams params = graphicParams(settings);
    if (!fx::graphicHasText(params.type)) {
        return glyphs;
    }
    glyphs.height = fx::graphicTextHeight(params, canvasHeight);
    QFont font(QStringLiteral("Inter"));
    font.setPixelSize(static_cast<int>(glyphs.height * 0.78));
    font.setWeight(QFont::ExtraBold);
    font.setFeature("tnum", 1); // digits of equal width: the number does not jump while it counts
    const QFontMetricsF metrics(font);
    const double outline = std::max(1.0, glyphs.height * 0.035);
    const auto draw = [&](const QString &text) {
        if (text.isEmpty()) {
            return QImage();
        }
        const int width = static_cast<int>(std::ceil(metrics.horizontalAdvance(text) + outline * 2));
        QImage image(std::max(1, width), glyphs.height, QImage::Format_RGBA8888);
        image.fill(Qt::transparent);
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing);
        QPainterPath path;
        path.addText(outline, (glyphs.height + metrics.capHeight()) / 2.0, font, text);
        if (params.color2.alpha() > 0) {
            painter.strokePath(path, QPen(params.color2, outline * 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        }
        painter.fillPath(path, params.color);
        return image;
    };
    for (const QChar c : fx::graphicCharacters()) {
        glyphs.glyphs.insert(c, draw(QString(c)));
    }
    glyphs.prefix = draw(settings.prefix);
    glyphs.suffix = draw(settings.suffix);
    return glyphs;
}

std::unique_ptr<Mlt::Producer> makeGraphicProducer(Mlt::Profile &profile, const GraphicSettings &settings, int length)
{
    auto producer = std::make_unique<Mlt::Producer>(profile, "vedit.graphic");
    auto *state = new GraphicState;
    state->params = graphicParams(settings);
    state->canvas = QSize(profile.width(), profile.height());
    state->glyphs = makeGraphicGlyphs(settings, profile.height());
    state->length = std::max(1, length);
    state->fps = profile.fps() > 0 ? profile.fps() : 30.0;
    producer->set(kSettings, state, 0, [](void *p) { delete static_cast<GraphicState *>(p); });
    return producer;
}

fx::VisualizerSettings visualizerSettings(const AudioVisualizerSettings &settings)
{
    const auto color = [](const Color &c) { return QColor(c.r, c.g, c.b, c.a); };
    fx::VisualizerSettings result;
    result.style = static_cast<fx::VisualizerStyle>(settings.style);
    result.barCount = settings.barCount;
    result.primary = color(settings.primaryColor);
    result.secondary = color(settings.secondaryColor);
    result.sensitivity = settings.sensitivity;
    result.mirror = settings.mirror;
    result.roundness = settings.roundness;
    result.thickness = settings.thickness;
    return result;
}

QByteArray BeatEffectSettings::key() const
{
    QByteArray bytes;
    QDataStream stream(&bytes, QIODevice::WriteOnly);
    stream << static_cast<int>(kind) << amount << decay << firstFrame << static_cast<qint64>(frameRate.num())
           << static_cast<qint64>(frameRate.den())
           << static_cast<quint32>(beats.size());
    for (const double beat : beats) {
        stream << beat;
    }
    return bytes;
}

// The parameters of a video effect: the preset's, overridden by the clip's own (docs/EFFECT_FORMAT.md §9).
fx::VideoEffectParams videoEffectParams(const QJsonObject &preset, const std::map<QString, Param> &own)
{
    fx::VideoEffectParams params;
    const auto rgb = [](const QJsonValue &value, fx::EffectRgb fallback) {
        const QJsonArray array = value.toArray();
        return array.size() == 3 ? fx::EffectRgb{array[0].toDouble(), array[1].toDouble(), array[2].toDouble()} : fallback;
    };
    params.amount = preset.value(QStringLiteral("amount")).toDouble(params.amount);
    params.size = preset.value(QStringLiteral("size")).toDouble(params.size);
    params.speed = preset.value(QStringLiteral("speed")).toDouble(params.speed);
    params.angle = preset.value(QStringLiteral("angle")).toDouble(params.angle);
    params.count = preset.value(QStringLiteral("count")).toInt(params.count);
    params.color = rgb(preset.value(QStringLiteral("color")), params.color);
    params.color2 = rgb(preset.value(QStringLiteral("color2")), params.color2);
    for (const auto &[name, param] : own) {
        const ParamValue &value = param.staticValue();
        if (const Color *color = std::get_if<Color>(&value)) {
            const fx::EffectRgb c{color->r / 255.0, color->g / 255.0, color->b / 255.0};
            if (name == QStringLiteral("color")) {
                params.color = c;
            } else if (name == QStringLiteral("color2")) {
                params.color2 = c;
            }
            continue;
        }
        const double *numberPtr = std::get_if<double>(&value);
        const double number = numberPtr ? *numberPtr : 0.0;
        if (name == QStringLiteral("amount")) {
            params.amount = number;
        } else if (name == QStringLiteral("size")) {
            params.size = number;
        } else if (name == QStringLiteral("speed")) {
            params.speed = number;
        } else if (name == QStringLiteral("angle")) {
            params.angle = number;
        } else if (name == QStringLiteral("count")) {
            params.count = static_cast<int>(std::lround(number));
        }
    }
    return params;
}

QByteArray VideoEffectSettings::key() const
{
    QByteArray bytes;
    QDataStream stream(&bytes, QIODevice::WriteOnly);
    stream << static_cast<int>(kernel) << params.amount << params.size << params.speed << params.angle << params.count
           << params.color.r << params.color.g << params.color.b << params.color2.r << params.color2.g << params.color2.b
           << mix << firstFrame << static_cast<qint64>(frameRate.num()) << static_cast<qint64>(frameRate.den());
    return bytes;
}

std::unique_ptr<Mlt::Filter> makeVideoEffectFilter(Mlt::Profile &profile, const VideoEffectSettings &settings)
{
    auto filter = std::make_unique<Mlt::Filter>(profile, "vedit.effect");
    attachSettings(*filter, settings);
    return filter;
}

std::unique_ptr<Mlt::Filter> makeBeatFilter(Mlt::Profile &profile, const BeatEffectSettings &settings)
{
    auto filter = std::make_unique<Mlt::Filter>(profile, "vedit.beat");
    attachSettings(*filter, settings);
    return filter;
}

} // namespace vedit::engine
