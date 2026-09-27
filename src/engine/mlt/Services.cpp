// SPDX-License-Identifier: GPL-3.0-or-later
#include "Services.h"

#include "core/project/ClipTime.h"
#include "engine/playback/AudioMeters.h"
#include "engine/text/TextRenderer.h"
#include "engine/timeline/ClipPlacement.h"
#include "fx/Animation.h"
#include "fx/Audio.h"
#include "fx/ChromaKey.h"
#include "fx/Composite.h"
#include "fx/Mask.h"
#include "fx/Transform.h"

#include <QCryptographicHash>
#include <QDataStream>
#include <QIODevice>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMutex>
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
            const double cycleSec = s->animations.loop->duration.toSecondsDouble() > 0.05 ? s->animations.loop->duration.toSecondsDouble() : 1.0;
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
    double gain = fx::dbToGain(s.gainDb);
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

// The layer is drawn once, by makeTextProducer() on the calling thread (the projection's): fonts are never used in
// MLT's threads. Besides the cost, Qt's per-thread FreeType data leaks when such a thread exits. Another size (a
// scaled text: vedit.transform asks for the size it shows; a reduced preview) is a smooth scaling of that layer.
struct TextState
{
    QImage layer; // at the profile size
    QMutex mutex;
    QImage scaled; // the last other size asked for
};

int textGetImage(mlt_frame frame, uint8_t **image, mlt_image_format *format, int *width, int *height, int)
{
    auto producer = static_cast<mlt_producer>(mlt_frame_pop_service(frame));
    auto *state = static_cast<TextState *>(mlt_properties_get_data(MLT_PRODUCER_PROPERTIES(producer), kSettings, nullptr));
    int w = *width;
    int h = *height;
    profileSize(MLT_PRODUCER_SERVICE(producer), w, h);
    QImage layer = state ? state->layer : QImage();
    if (!layer.isNull() && layer.size() != QSize(w, h)) {
        QMutexLocker lock(&state->mutex);
        if (state->scaled.size() != QSize(w, h)) {
            state->scaled = state->layer.scaled(w, h, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                                .convertToFormat(QImage::Format_RGBA8888);
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
        mlt_frame_set_position(*frame, mlt_producer_position(producer));
        mlt_properties_set_int(properties, "progressive", 1);
        mlt_properties_set_int(properties, "test_audio", 1); // no sound
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
    repository->register_service(mlt_service_filter_type, "vedit.mask", createFilter<maskProcess>);
    repository->register_service(mlt_service_filter_type, "vedit.gain", createFilter<gainProcess>);
    repository->register_service(mlt_service_transition_type, "vedit.transition", createTransitionService);
    repository->register_service(mlt_service_producer_type, "vedit.text", createText);
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
    state->layer = TextRenderer::render(text, QSize(profile.width(), profile.height()));
    producer->set(kSettings, state, 0, [](void *p) { delete static_cast<TextState *>(p); });
    return producer;
}

} // namespace vedit::engine
