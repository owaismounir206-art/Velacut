// SPDX-License-Identifier: GPL-3.0-or-later
#include "Services.h"

#include "engine/playback/AudioMeters.h"
#include "engine/text/TextRenderer.h"
#include "fx/Audio.h"
#include "fx/Composite.h"
#include "fx/Transform.h"

#include <QCryptographicHash>
#include <QDataStream>
#include <QIODevice>
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
    const fx::ImageView destination{*image, *width, *height, *width * 4};
    const fx::ConstImageView source{bImage, bWidth, bHeight, bWidth * 4};
    runSliced(*height, [&](int begin, int end) { fx::compositeOver(destination, source, opacity, begin, end); });
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

// Size of the source at scale 1 on a canvas, per fit mode.
void fittedSize(const TransformSettings &s, int canvasWidth, int canvasHeight, double &width, double &height)
{
    const double sourceAspect = s.sourceWidth / std::max(1.0, s.sourceHeight);
    const double canvasAspect = double(canvasWidth) / canvasHeight;
    switch (s.fit) {
    case FitMode::Contain:
    case FitMode::Cover: {
        const bool wider = sourceAspect > canvasAspect;
        const bool byWidth = s.fit == FitMode::Contain ? wider : !wider;
        width = byWidth ? canvasWidth : canvasHeight * sourceAspect;
        height = byWidth ? canvasWidth / sourceAspect : canvasHeight;
        return;
    }
    case FitMode::Stretch:
        width = canvasWidth;
        height = canvasHeight;
        return;
    case FitMode::None:
        // Pixels 1:1 relative to a 1080p canvas.
        width = s.sourceWidth * canvasHeight / 1080.0;
        height = s.sourceHeight * canvasHeight / 1080.0;
        return;
    }
}

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
    const TransformSettings *s = settingsOf<TransformSettings>(MLT_FILTER_PROPERTIES(filter));
    int w = *width;
    int h = *height;
    profileSize(MLT_FILTER_SERVICE(filter), w, h);
    if (!s) {
        *format = mlt_image_rgba;
        return mlt_frame_get_image(frame, image, format, width, height, 0);
    }
    double fitW = 0, fitH = 0;
    fittedSize(*s, w, h, fitW, fitH);
    const double shownW = fitW * std::abs(s->scaleX);
    const double shownH = fitH * std::abs(s->scaleY);
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
    const fx::Affine forward = fx::Affine::translation(w / 2.0 + s->x * w, h / 2.0 + s->y * h) * fx::Affine::rotation(s->rotation) *
                               fx::Affine::scaling(kx, ky) * fx::Affine::translation(-sw / 2.0, -sh / 2.0);
    const fx::Affine toSource = forward.inverted();
    const fx::SourceWindow window{s->cropLeft * sw, s->cropTop * sh, (1.0 - s->cropRight) * sw, (1.0 - s->cropBottom) * sh};
    const fx::ImageView target{canvas, w, h, w * 4};
    const fx::ConstImageView src{source, sw, sh, sw * 4};
    runSliced(h, [&](int begin, int end) { fx::drawAffine(target, src, toSource, window, s->opacity, begin, end); });
    mlt_frame_set_image(frame, canvas, size, mlt_pool_release);
    *image = canvas;
    *width = w;
    *height = h;
    *format = mlt_image_rgba;
    return 0;
}

mlt_frame transformProcess(mlt_filter filter, mlt_frame frame)
{
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
    const fx::ImageView view{*image, *width, *height, *width * 4};
    const double k = std::clamp(s.intensity, 0.0, 1.0);
    if (!s.look.isIdentity()) {
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

struct TextState
{
    TextClipData text;
    QMutex mutex;
    QImage cached; // at the last requested size
};

int textGetImage(mlt_frame frame, uint8_t **image, mlt_image_format *format, int *width, int *height, int)
{
    auto producer = static_cast<mlt_producer>(mlt_frame_pop_service(frame));
    auto *state = static_cast<TextState *>(mlt_properties_get_data(MLT_PRODUCER_PROPERTIES(producer), kSettings, nullptr));
    int w = *width;
    int h = *height;
    profileSize(MLT_PRODUCER_SERVICE(producer), w, h);
    QImage layer;
    if (state) {
        QMutexLocker lock(&state->mutex);
        if (state->cached.size() != QSize(w, h)) {
            state->cached = TextRenderer::render(state->text, QSize(w, h));
        }
        layer = state->cached;
    }
    const int size = w * h * 4;
    auto *buffer = static_cast<uint8_t *>(mlt_pool_alloc(size));
    if (layer.isNull()) {
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
    repository->register_service(mlt_service_filter_type, "vedit.gain", createFilter<gainProcess>);
    repository->register_service(mlt_service_transition_type, "vedit.transition", createTransitionService);
    repository->register_service(mlt_service_producer_type, "vedit.text", createText);
}

// ---- settings -----------------------------------------------------------------------------------------------------

bool TransformSettings::isIdentityLayer() const
{
    return fit == FitMode::Stretch && x == 0 && y == 0 && scaleX == 1 && scaleY == 1 && rotation == 0 && !flipH && !flipV &&
           cropLeft == 0 && cropTop == 0 && cropRight == 0 && cropBottom == 0 && opacity == 1 && !background;
}

QByteArray TransformSettings::key() const
{
    QByteArray bytes;
    QDataStream stream(&bytes, QIODevice::WriteOnly);
    stream << sourceWidth << sourceHeight << int(fit) << x << y << scaleX << scaleY << rotation << flipH << flipV << cropLeft
           << cropTop << cropRight << cropBottom << opacity << background.has_value();
    if (background) {
        stream << int(background->type) << background->color.toString() << background->amount;
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
    attachSettings(*filter, settings);
    return filter;
}

std::unique_ptr<Mlt::Filter> makeAdjustFilter(Mlt::Profile &profile, const AdjustSettings &settings)
{
    auto filter = std::make_unique<Mlt::Filter>(profile, "vedit.adjust");
    auto *state = new AdjustState{settings, fx::ColorLut(settings.look)};
    filter->set(kSettings, state, 0, [](void *p) { delete static_cast<AdjustState *>(p); });
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
    state->text = text;
    producer->set(kSettings, state, 0, [](void *p) { delete static_cast<TextState *>(p); });
    return producer;
}

} // namespace vedit::engine
