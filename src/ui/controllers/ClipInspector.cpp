// SPDX-License-Identifier: GPL-3.0-or-later
#include "ClipInspector.h"

#include "EditorController.h"
#include "core/edit/TimelineEditor.h"
#include "core/project/ClipTime.h"
#include "core/project/SpeedCurve.h"
#include "core/serialization/ProjectJson.h"
#include "engine/analysis/Decoding.h"
#include "engine/analysis/MediaAnalysis.h"
#include "engine/playback/TimelinePlayer.h"
#include "engine/timeline/ClipPlacement.h"
#include "fx/Enhance.h"
#include "fx/Grade.h"
#include "fx/Library.h"
#include "fx/Loudness.h"
#include "fx/VideoEffect.h"

#include <QColor>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRandomGenerator>

#include <algorithm>
#include <cmath>
#include <numbers>

using namespace Qt::StringLiterals;

namespace vedit::ui {

namespace {

const QString kFilterType = u"vedit.filter"_s;
const QString kAdjustType = u"vedit.adjust.basic"_s;
const QString kGradeType = u"vedit.grade"_s;
const QString kLutType = u"vedit.lut"_s;
const QString kDeflickerType = u"vedit.deflicker"_s;
const QString kChromaType = u"vedit.chroma_key"_s;
const QString kMotionBlurType = u"vedit.motion_blur"_s;
constexpr int kBlendModeCount = 17;

double numberOf(const Param &param, double fallback)
{
    const double *value = std::get_if<double>(&param.staticValue());
    return value ? *value : fallback;
}

Vec2 vectorOf(const Param &param, Vec2 fallback)
{
    const Vec2 *value = std::get_if<Vec2>(&param.staticValue());
    return value ? *value : fallback;
}

Color colorOf(const Param &param, Color fallback)
{
    const Color *value = std::get_if<Color>(&param.staticValue());
    return value ? *value : fallback;
}

QColor toQColor(const Color &c)
{
    return QColor(c.r, c.g, c.b, c.a);
}

Color toColor(const QVariant &value)
{
    const QColor c = value.value<QColor>();
    return Color{static_cast<std::uint8_t>(c.red()), static_cast<std::uint8_t>(c.green()),
                 static_cast<std::uint8_t>(c.blue()), static_cast<std::uint8_t>(c.alpha())};
}

const Effect *findEffect(const Clip &clip, const QString &type)
{
    const auto it = std::find_if(clip.effects.begin(), clip.effects.end(),
                                 [&type](const Effect &effect) { return effect.type == type; });
    return it == clip.effects.end() ? nullptr : &*it;
}

// The effect of `type`, created (filters before adjustments: a look, then the user's corrections) if missing.
Effect &ensureEffect(Clip &clip, const QString &type)
{
    const auto it = std::find_if(clip.effects.begin(), clip.effects.end(),
                                 [&type](const Effect &effect) { return effect.type == type; });
    if (it != clip.effects.end()) {
        return *it;
    }
    Effect effect;
    effect.id = EffectId::create();
    effect.type = type;
    if (type == kFilterType) {
        clip.effects.insert(clip.effects.begin(), std::move(effect));
        return clip.effects.front();
    }
    clip.effects.push_back(std::move(effect));
    return clip.effects.back();
}

void removeEffect(Clip &clip, const QString &type)
{
    std::erase_if(clip.effects, [&type](const Effect &effect) { return effect.type == type; });
}

// Copies the effect of `type` of `from` (or its absence) onto `to`, with a new id.
void copyEffect(const Clip &from, Clip &to, const QString &type)
{
    const Effect *source = findEffect(from, type);
    if (!source) {
        removeEffect(to, type);
        return;
    }
    const EffectId id = findEffect(to, type) ? findEffect(to, type)->id : EffectId::create();
    Effect &target = ensureEffect(to, type);
    target = *source;
    target.id = id;
}

AssetRef coreAsset(const QString &id, int version)
{
    return AssetRef{QString::fromLatin1(fx::Library::kCorePack), id, version};
}

// The parameters that have keyframes in the interface (the renderer animates them: vedit.transform, vedit.gain).
const QStringList kKeyframeKeys{u"position"_s, u"scale"_s, u"rotation"_s, u"opacity"_s, u"volume"_s};

Param *keyframeParam(Clip &clip, const QString &key)
{
    if (key == u"position"_s || key == u"x"_s || key == u"y"_s) {
        return &clip.transform.position;
    }
    if (key == u"scale"_s) {
        return &clip.transform.scale;
    }
    if (key == u"rotation"_s) {
        return &clip.transform.rotation;
    }
    if (key == u"opacity"_s) {
        return &clip.opacity;
    }
    if (key == u"volume"_s && clip.media()) {
        return &clip.media()->audio.gainDb;
    }
    return nullptr;
}

const Param *keyframeParam(const Clip &clip, const QString &key)
{
    return keyframeParam(const_cast<Clip &>(clip), key);
}

// Writes `value` at `time`: replaces the keyframe there, or inserts one (keeping the order).
void setKeyframeValue(Param &param, const RationalTime &time, const ParamValue &value)
{
    std::vector<Keyframe> keyframes = param.keyframes();
    const auto it = std::find_if(keyframes.begin(), keyframes.end(), [&time](const Keyframe &k) { return k.time == time; });
    if (it != keyframes.end()) {
        it->value = value;
    } else {
        Keyframe keyframe;
        keyframe.time = time;
        keyframe.value = value;
        keyframes.insert(std::upper_bound(keyframes.begin(), keyframes.end(), time,
                                          [](const RationalTime &t, const Keyframe &k) { return t < k.time; }),
                         keyframe);
    }
    param.setKeyframes(std::move(keyframes));
}

QString easingName(const Keyframe &keyframe)
{
    switch (keyframe.interpolation) {
    case Interpolation::Linear:
        return u"linear"_s;
    case Interpolation::Hold:
        return u"hold"_s;
    case Interpolation::Bezier:
        break;
    }
    return keyframe.easing.presetKind() == Easing::Preset::Custom ? u"custom"_s : keyframe.easing.name();
}

// "in", "out" or "loop": the kind of a preset animation.
QString animationKind(const QString &animationId)
{
    const fx::AnimationPreset *preset = fx::Library::core().animation(animationId);
    if (preset && !preset->category.isEmpty()) {
        return preset->category;
    }
    return animationId.section(u'/', 1, 1);
}

std::optional<ClipAnimation> &animationSlot(ClipAnimations &animations, const QString &kind)
{
    return kind == u"in"_s ? animations.in : kind == u"out"_s ? animations.out : animations.loop;
}

// The animation of a preset at its default length (kept when another preset of the same kind replaces it).
ClipAnimation animationOf(const fx::AnimationPreset &preset, const std::optional<ClipAnimation> &current, Rational rate)
{
    ClipAnimation animation;
    animation.type = coreAsset(preset.id, preset.version);
    animation.duration = current ? current->duration
                                 : RationalTime(std::max<std::int64_t>(1, std::llround(preset.defaultSeconds * rate.toDouble())), rate);
    if (const std::optional<Easing> easing = Easing::fromName(preset.easing)) {
        animation.easing = *easing;
    }
    animation.params = preset.params;
    return animation;
}

// The params of the adjust effect as the colour adjustments they describe (names of effects.json).
void writeAdjust(Effect &effect, const fx::ColorAdjust &adjust)
{
    const std::pair<const char *, double> values[] = {
        {"exposure", adjust.exposure},       {"contrast", adjust.contrast}, {"highlights", adjust.highlights},
        {"shadows", adjust.shadows},         {"temperature", adjust.temperature}, {"tint", adjust.tint},
        {"vibrance", adjust.vibrance},
    };
    for (const auto &[name, value] : values) {
        effect.params[QString::fromLatin1(name)] = Param(std::round(value * 100.0) / 100.0);
    }
}

} // namespace

ClipInspector::ClipInspector(EditorController &editor)
    : QObject(&editor)
    , m_editor(editor)
{
    connect(&editor, &EditorController::selectionChanged, this, &ClipInspector::changed);
    connect(&editor, &EditorController::modelChanged, this, &ClipInspector::changed);
    // The values at the playhead (keyframes) and whether the playhead is on the clip: while paused (moving the
    // playhead, stepping, skimming), and once playback stops. Not at every frame of playback: the whole panel would be
    // recomputed 30 times a second for nothing.
    connect(editor.player(), &engine::TimelinePlayer::positionChanged, this, [this] {
        if (!m_editor.player()->playing() && playheadMatters()) {
            emit changed();
        }
    });
    connect(editor.player(), &engine::TimelinePlayer::stateChanged, this, [this] {
        if (!m_editor.player()->playing() && focus()) {
            emit changed();
        }
    });
}

TextStyle ClipInspector::defaultTextStyle()
{
    const fx::TextStylePreset *preset = fx::Library::core().textStyle(u"text/outline"_s);
    return preset ? projectjson::textStyleFromJson(preset->style) : TextStyle{};
}

const Clip *ClipInspector::focus() const
{
    const std::optional<ClipId> id = m_editor.focusClip();
    return id ? m_editor.data().findClip(*id) : nullptr;
}

const Clip *ClipInspector::libraryClip() const
{
    const std::optional<ClipId> id = m_editor.clipForLibrary();
    return id ? m_editor.data().findClip(*id) : nullptr;
}

const vedit::Transition *ClipInspector::focusTransition(const Track **track) const
{
    const std::optional<TransitionId> id = m_editor.focusTransition();
    const Sequence *sequence = m_editor.data().mainSequence();
    if (!id || !sequence) {
        return nullptr;
    }
    for (const Track &candidate : sequence->visualTracks) {
        for (const vedit::Transition &transition : candidate.transitions) {
            if (transition.id == *id) {
                if (track) {
                    *track = &candidate;
                }
                return &transition;
            }
        }
    }
    return nullptr;
}

bool ClipInspector::playheadMatters()
{
    const Clip *clip = focus();
    if (!clip) {
        m_playheadOnClip = false;
        return false;
    }
    const bool on = playheadKeyTime(*clip).has_value();
    const bool crossed = on != m_playheadOnClip;
    m_playheadOnClip = on;
    const bool animated = std::any_of(kKeyframeKeys.begin(), kKeyframeKeys.end(), [clip](const QString &key) {
        const Param *p = keyframeParam(*clip, key);
        return p && p->isAnimated();
    });
    return crossed || animated;
}

std::optional<RationalTime> ClipInspector::playheadKeyTime(const Clip &clip) const
{
    const Rational rate = m_editor.data().settings.frameRate;
    const RationalTime at(m_editor.player()->position(), rate);
    if (at < clip.start || !(at < clip.end())) {
        return std::nullopt;
    }
    return keyframeTime(clip, at - clip.start);
}

void ClipInspector::setCanvasMode(const QString &mode)
{
    if (mode != m_canvasMode) {
        m_canvasMode = mode;
        emit canvasModeChanged();
    }
}

QVariantMap ClipInspector::maskBox() const
{
    const Clip *clip = focus();
    if (!clip || clip->masks.empty() || !supports(*clip, u"cutout"_s)) {
        return {};
    }
    const Mask &mask = clip->masks.front();
    const engine::CanvasBox box = engine::canvasBox(*clip, m_editor.data().findMedia(clip->media()->mediaId), m_editor.canvasSize());
    // Picture coordinates (−0.5…0.5, scaled to the clip's box) → canvas, through the clip's rotation.
    const Vec2 centre = vectorOf(mask.center, {0, 0});
    const Vec2 size = vectorOf(mask.size, {0.5, 0.5});
    const double radians = box.rotation * std::numbers::pi / 180.0;
    const double dx = centre.x * box.size.width();
    const double dy = centre.y * box.size.height();
    return {{u"x"_s, box.centre.x() + dx * std::cos(radians) - dy * std::sin(radians)},
            {u"y"_s, box.centre.y() + dx * std::sin(radians) + dy * std::cos(radians)},
            {u"width"_s, size.x * box.size.width()},
            {u"height"_s, size.y * box.size.height()},
            {u"rotation"_s, box.rotation + numberOf(mask.rotation, 0.0)},
            {u"shape"_s, static_cast<int>(mask.shape)}};
}

bool ClipInspector::setMaskGeometry(double centreX, double centreY, double width, double height)
{
    const Clip *clip = focus();
    if (!clip || clip->masks.empty() || !supports(*clip, u"cutout"_s)) {
        return false;
    }
    const engine::CanvasBox box = engine::canvasBox(*clip, m_editor.data().findMedia(clip->media()->mediaId), m_editor.canvasSize());
    if (box.size.isEmpty()) {
        return false;
    }
    const double radians = -box.rotation * std::numbers::pi / 180.0;
    const double dx = centreX - box.centre.x();
    const double dy = centreY - box.centre.y();
    const Vec2 centre{std::clamp((dx * std::cos(radians) - dy * std::sin(radians)) / box.size.width(), -1.0, 1.0),
                      std::clamp((dx * std::sin(radians) + dy * std::cos(radians)) / box.size.height(), -1.0, 1.0)};
    const Vec2 size{std::clamp(width / box.size.width(), 0.01, 4.0), std::clamp(height / box.size.height(), 0.01, 4.0)};
    return update({clip->id}, [&](Clip &c) {
        c.masks.front().center = Param(centre);
        c.masks.front().size = Param(size);
    }, tr("Change mask"), u"mask-geometry:"_s + clip->id.toString());
}

bool ClipInspector::pickKeyColor(double canvasX, double canvasY)
{
    setCanvasMode({});
    const Clip *clip = focus();
    const MediaClipData *media = clip ? clip->media() : nullptr;
    const Media *source = media ? m_editor.data().findMedia(media->mediaId) : nullptr;
    if (!source || !supports(*clip, u"cutout"_s)) {
        return false;
    }
    // The canvas point in the clip's picture (0…1), through its box, rotation and mirrors.
    const engine::CanvasBox box = engine::canvasBox(*clip, source, m_editor.canvasSize());
    const double radians = -box.rotation * std::numbers::pi / 180.0;
    const double dx = canvasX - box.centre.x();
    const double dy = canvasY - box.centre.y();
    double u = (dx * std::cos(radians) - dy * std::sin(radians)) / box.size.width() + 0.5;
    double v = (dx * std::sin(radians) + dy * std::cos(radians)) / box.size.height() + 0.5;
    if (clip->transform.flipH) {
        u = 1.0 - u;
    }
    if (clip->transform.flipV) {
        v = 1.0 - v;
    }
    if (u < 0 || u > 1 || v < 0 || v > 1) {
        emit m_editor.message(tr("Click on the clip to pick the colour to remove."), false);
        return false;
    }
    // The clip's own picture there: the source frame at the playhead (not the result, which may already be keyed).
    const std::optional<RationalTime> time = playheadKeyTime(*clip);
    const double seconds = source->kind == MediaKind::Image ? 0.0 : (time ? *time : media->sourceIn).toSecondsDouble();
    const QImage frame = engine::extractFrame(source->path, seconds, 720);
    if (frame.isNull()) {
        emit m_editor.message(tr("This frame cannot be read from the file."), false);
        return false;
    }
    const QColor colour = frame.pixelColor(std::clamp(static_cast<int>(u * frame.width()), 0, frame.width() - 1),
                                           std::clamp(static_cast<int>(v * frame.height()), 0, frame.height() - 1));
    endGesture();
    return update({clip->id}, [&](Clip &c) {
        Effect &effect = ensureEffect(c, kChromaType);
        effect.params[u"keyColor"_s] = Param(toColor(colour));
        for (const auto &[name, fallback] : {std::pair{u"similarity"_s, 0.4}, {u"smoothness"_s, 0.1}, {u"spill"_s, 0.5}}) {
            if (!effect.params.contains(name)) {
                effect.params[name] = Param(fallback);
            }
        }
    }, tr("Remove a colour"), {});
}

QVariantList ClipInspector::keyframes() const
{
    const Clip *clip = focus();
    QVariantList frames;
    if (!clip) {
        return frames;
    }
    const Rational rate = m_editor.data().settings.frameRate;
    QList<int> list;
    for (const QString &key : kKeyframeKeys) {
        const Param *param = keyframeParam(*clip, key);
        if (!param) {
            continue;
        }
        for (const Keyframe &keyframe : param->keyframes()) {
            const int frame = static_cast<int>(offsetOfKeyframeTime(*clip, keyframe.time).rescaled(rate, Rounding::NearestEven).value());
            if (!list.contains(frame)) {
                list << frame;
            }
        }
    }
    std::sort(list.begin(), list.end());
    for (const int frame : list) {
        frames << frame;
    }
    return frames;
}

bool ClipInspector::toggleKeyframe(const QString &key)
{
    const Clip *clip = focus();
    const std::optional<RationalTime> time = clip ? playheadKeyTime(*clip) : std::nullopt;
    const Param *p = clip ? keyframeParam(*clip, key) : nullptr;
    if (!time || !kKeyframeKeys.contains(key) || !p) {
        return false;
    }
    const Param &current = *p;
    const bool here = std::any_of(current.keyframes().begin(), current.keyframes().end(),
                                  [&time](const Keyframe &k) { return k.time == *time; });
    endGesture();
    return update({clip->id}, [&](Clip &c) {
        Param *param = keyframeParam(c, key);
        if (!param) {
            return;
        }
        const ParamValue value = param->valueAt(*time);
        if (!here) {
            setKeyframeValue(*param, *time, value);
            return;
        }
        std::vector<Keyframe> keyframes = param->keyframes();
        std::erase_if(keyframes, [&time](const Keyframe &k) { return k.time == *time; });
        if (keyframes.empty()) {
            param->setKeyframes({});
            param->setStaticValue(value); // the last one leaves its value
        } else {
            param->setKeyframes(std::move(keyframes));
        }
    }, here ? tr("Remove keyframe") : tr("Add keyframe"), {});
}

bool ClipInspector::setKeyframeEasing(const QString &easing)
{
    const Clip *clip = focus();
    const std::optional<RationalTime> time = clip ? playheadKeyTime(*clip) : std::nullopt;
    const std::optional<Easing> preset = Easing::fromName(easing);
    if (!time || (!preset && easing != u"hold"_s)) {
        return false;
    }
    endGesture();
    return update({clip->id}, [&](Clip &c) {
        for (const QString &key : kKeyframeKeys) {
            Param *param = keyframeParam(c, key);
            if (!param) {
                continue;
            }
            std::vector<Keyframe> keyframes = param->keyframes();
            for (Keyframe &keyframe : keyframes) {
                if (keyframe.time == *time) {
                    keyframe.interpolation = easing == u"hold"_s     ? Interpolation::Hold
                                             : easing == u"linear"_s ? Interpolation::Linear
                                                                      : Interpolation::Bezier;
                    if (preset) {
                        keyframe.easing = *preset;
                    }
                }
            }
            param->setKeyframes(std::move(keyframes));
        }
    }, tr("Change keyframe easing"), {});
}

bool ClipInspector::setKeyframeCurve(double x1, double y1, double x2, double y2)
{
    const Clip *clip = focus();
    const std::optional<RationalTime> time = clip ? playheadKeyTime(*clip) : std::nullopt;
    const std::optional<Easing> curve = Easing::cubicBezier(std::clamp(x1, 0.0, 1.0), std::clamp(y1, -1.0, 2.0),
                                                            std::clamp(x2, 0.0, 1.0), std::clamp(y2, -1.0, 2.0));
    if (!time || !curve) {
        return false;
    }
    return update({clip->id}, [&](Clip &c) {
        for (const QString &key : kKeyframeKeys) {
            Param *param = keyframeParam(c, key);
            if (!param) {
                continue;
            }
            std::vector<Keyframe> keyframes = param->keyframes();
            for (Keyframe &keyframe : keyframes) {
                if (keyframe.time == *time) {
                    keyframe.interpolation = Interpolation::Bezier;
                    keyframe.easing = *curve;
                }
            }
            param->setKeyframes(std::move(keyframes));
        }
    }, tr("Change keyframe curve"), u"keyframe-curve:"_s + clip->id.toString());
}

void ClipInspector::jumpKeyframe(int direction)
{
    const Clip *clip = focus();
    if (!clip) {
        return;
    }
    const Rational rate = m_editor.data().settings.frameRate;
    const int start = static_cast<int>(clip->start.rescaled(rate, Rounding::NearestEven).value());
    const int position = m_editor.player()->position();
    std::optional<int> target;
    for (const QVariant &frame : keyframes()) {
        const int at = start + frame.toInt();
        if (direction > 0 && at > position && (!target || at < *target)) {
            target = at;
        } else if (direction < 0 && at < position && (!target || at > *target)) {
            target = at;
        }
    }
    if (target) {
        m_editor.player()->seek(*target);
    }
}

bool ClipInspector::active() const
{
    return focus() != nullptr || focusTransition() != nullptr;
}

QString ClipInspector::clipId() const
{
    const Clip *clip = focus();
    return clip ? clip->id.toString() : QString();
}

int ClipInspector::selectedCount() const
{
    return static_cast<int>(m_editor.selectedClips().size());
}

namespace {

// The visualizer settings of a sticker as its library item defines them (none for a picture sticker).
std::optional<AudioVisualizerSettings> presetVisualizer(const StickerClipData &sticker)
{
    if (!sticker.visualizer) {
        return std::nullopt;
    }
    const fx::StickerPreset *preset = sticker.source ? fx::Library::core().sticker(sticker.source->id) : nullptr;
    if (!preset || preset->visualizer.isEmpty()) {
        return AudioVisualizerSettings{};
    }
    return projectjson::visualizerFromJson(preset->visualizer);
}

// The graphic element of a sticker as its library item defines it.
std::optional<GraphicSettings> presetGraphic(const StickerClipData &sticker)
{
    if (!sticker.graphic) {
        return std::nullopt;
    }
    const fx::StickerPreset *preset = sticker.source ? fx::Library::core().sticker(sticker.source->id) : nullptr;
    if (!preset || preset->graphic.isEmpty()) {
        return sticker.graphic;
    }
    return projectjson::graphicFromJson(preset->graphic);
}

bool isLibraryEffect(const Effect &effect)
{
    return effect.type == u"vedit.effect"_s || effect.type.startsWith(u"vedit.beat."_s);
}

// The effects of the library on a clip, in order.
std::vector<const Effect *> libraryEffects(const Clip &clip)
{
    std::vector<const Effect *> result;
    for (const Effect &effect : clip.effects) {
        if (isLibraryEffect(effect)) {
            result.push_back(&effect);
        }
    }
    return result;
}

Effect effectOf(const fx::VideoEffectPreset &preset)
{
    Effect effect;
    effect.id = EffectId::create();
    effect.type = preset.type;
    effect.preset = AssetRef{QString::fromLatin1(fx::Library::kCorePack), preset.id, preset.version};
    if (preset.type != u"vedit.effect"_s) {
        // Effects of other types keep their parameters in the clip (the renderer does not read the preset).
        for (auto it = preset.params.begin(); it != preset.params.end(); ++it) {
            effect.params[it.key()] = Param(it.value().toDouble());
        }
    }
    return effect;
}

// The range of a control of the properties panel: [from, to] and its name.
struct ControlRange
{
    double from = 0;
    double to = 1;
};

ControlRange controlRange(const QString &type, const QString &name, double preset)
{
    if (name == u"amount"_s) {
        if (type == u"vedit.beat.zoom"_s) {
            return {0, 0.5};
        }
        if (type == u"vedit.beat.shake"_s) {
            return {0, 30};
        }
        return {preset < 0 ? -1.0 : 0.0, 1.0};
    }
    if (name == u"speed"_s) {
        return {0, 4};
    }
    if (name == u"angle"_s) {
        return {0, 180};
    }
    return {0, 1};
}

} // namespace

int ClipInspector::kind() const
{
    const Clip *clip = focus();
    if (!clip) {
        return focusTransition() ? Transition : None;
    }
    if (clip->text()) {
        return Text;
    }
    if (clip->adjustment()) {
        return Adjustment;
    }
    if (clip->sticker()) {
        return Sticker;
    }
    if (clip->compound()) {
        return Video;
    }
    if (const MediaClipData *media = clip->media()) {
        const Media *source = m_editor.data().findMedia(media->mediaId);
        if (!source || source->kind == MediaKind::Audio || media->streams == Streams::AudioOnly) {
            return Audio;
        }
        return source->kind == MediaKind::Image ? Image : Video;
    }
    return Other;
}

bool ClipInspector::supports(const Clip &clip, const QString &section) const
{
    const MediaClipData *media = clip.media();
    const Media *source = media ? m_editor.data().findMedia(media->mediaId) : nullptr;
    const bool audioOnly = media && (!source || source->kind == MediaKind::Audio || media->streams == Streams::AudioOnly);
    const bool image = source && source->kind == MediaKind::Image;
    const bool visualMedia = (media && !audioOnly) || clip.compound() != nullptr;
    // An adjustment layer has no picture of its own: only the looks it gives to what is under it.
    const bool adjustmentLayer = clip.adjustment() != nullptr;
    if (adjustmentLayer) {
        return section == u"filter"_s || section == u"adjust"_s || section == u"grade"_s || section == u"lut"_s ||
               section == u"deflicker"_s || section == u"effects"_s;
    }
    if (section == u"effects"_s) {
        return !audioOnly;
    }
    if (section == u"video"_s) {
        return !audioOnly;
    }
    if (section == u"background"_s) {
        const Sequence *sequence = m_editor.data().mainSequence();
        return visualMedia && sequence && !sequence->visualTracks.empty() &&
               sequence->visualTracks.front().findClip(clip.id) != nullptr;
    }
    if (section == u"audio"_s) {
        return media && !image && media->streams != Streams::VideoOnly && source && source->info.audio.has_value();
    }
    if (section == u"speed"_s) {
        return media && !image;
    }
    if (section == u"filter"_s || section == u"adjust"_s || section == u"grade"_s || section == u"lut"_s || section == u"deflicker"_s) {
        return visualMedia;
    }
    if (section == u"text"_s || section == u"textAnimation"_s) {
        return clip.text() != nullptr;
    }
    if (section == u"sticker"_s) {
        return clip.sticker() != nullptr;
    }
    if (section == u"animation"_s) {
        return !audioOnly;
    }
    if (section == u"cutout"_s) {
        return visualMedia;
    }
    return false;
}

QStringList ClipInspector::sections() const
{
    const Clip *clip = focus();
    QStringList result;
    if (!clip) {
        if (focusTransition()) {
            result << u"transition"_s;
        }
        return result;
    }
    for (const QString &section : {u"text"_s, u"sticker"_s, u"video"_s, u"background"_s, u"audio"_s, u"speed"_s, u"animation"_s, u"cutout"_s,
                                   u"filter"_s, u"effects"_s, u"adjust"_s}) {
        if (supports(*clip, section)) {
            result << section;
        }
    }
    return result;
}

QStringList ClipInspector::modifiedSections() const
{
    const Clip *clip = focus();
    QStringList result;
    if (!clip) {
        return result;
    }
    Transform plainTransform;
    plainTransform.fit = clip->transform.fit;
    if (clip->transform != plainTransform || numberOf(clip->opacity, 1.0) != 1.0) {
        result << u"video"_s;
    }
    if (clip->background) {
        result << u"background"_s;
    }
    if (const MediaClipData *media = clip->media()) {
        if (media->audio != ClipAudio{}) {
            result << u"audio"_s;
        }
        if (media->speed != 1.0 || media->reversed || !media->preservePitch || media->curve.has_value() || findEffect(*clip, kMotionBlurType)) {
            result << u"speed"_s;
        }
    }
    if (!clip->animations.isEmpty()) {
        result << u"animation"_s;
    }
    if (!clip->masks.empty() || findEffect(*clip, kChromaType)) {
        result << u"cutout"_s;
    }
    if (findEffect(*clip, kFilterType)) {
        result << u"filter"_s;
    }
    if (const Effect *adjust = findEffect(*clip, kAdjustType)) {
        if (std::any_of(adjust->params.begin(), adjust->params.end(),
                        [](const auto &entry) { return numberOf(entry.second, 0.0) != 0.0; })) {
            result << u"adjust"_s;
        }
    }
    if (findEffect(*clip, kGradeType) || findEffect(*clip, kLutType) || findEffect(*clip, kDeflickerType)) {
        if (!result.contains(u"adjust"_s)) {
            result << u"adjust"_s;
        }
    }
    if (const StickerClipData *sticker = clip->sticker()) {
        if (sticker->tint.a > 0 || sticker->speed != 1.0 || !sticker->loop || sticker->visualizer != presetVisualizer(*sticker) ||
            sticker->graphic != presetGraphic(*sticker)) {
            result << u"sticker"_s;
        }
    }
    if (const TextClipData *text = clip->text()) {
        if (text->style != defaultTextStyle()) {
            result << u"text"_s;
        }
        if (text->animation.has_value()) {
            result << u"textAnimation"_s;
        }
    }
    return result;
}

double ClipInspector::durationSeconds() const
{
    const Clip *clip = focus();
    return clip ? clip->duration.toSecondsDouble() : 0.0;
}

QVariantList ClipInspector::adjustParams() const
{
    QVariantList list;
    if (const fx::EffectSpec *spec = fx::Library::core().effect(kAdjustType)) {
        for (const fx::ParamSpec &param : spec->params) {
            list << QVariantMap{{u"name"_s, param.name},
                                {u"label"_s, param.label.text()},
                                {u"min"_s, param.min},
                                {u"max"_s, param.max},
                                {u"default"_s, param.defaultValue},
                                {u"advanced"_s, param.advanced}};
        }
    }
    return list;
}

QVariantMap ClipInspector::canvasBox() const
{
    const Clip *clip = focus();
    if (!clip || (!supports(*clip, u"video"_s) && !supports(*clip, u"text"_s))) {
        return {};
    }
    const MediaClipData *media = clip->media();
    const QSize canvas = m_editor.canvasSize();
    const engine::CanvasBox box =
        engine::canvasBox(*clip, media ? m_editor.data().findMedia(media->mediaId) : nullptr, canvas);
    const Rational rate = m_editor.data().settings.frameRate;
    return {{u"x"_s, box.centre.x()},
            {u"y"_s, box.centre.y()},
            {u"width"_s, box.size.width()},
            {u"height"_s, box.size.height()},
            {u"rotation"_s, box.rotation},
            {u"start"_s, static_cast<int>(clip->start.rescaled(rate, Rounding::NearestEven).value())},
            {u"end"_s, static_cast<int>(clip->end().rescaled(rate, Rounding::NearestEven).value())}};
}

QVariantList ClipInspector::swatches() const
{
    QVariantList list;
    for (const char *name : {"#FFFFFF", "#000000", "#9E9E9E", "#F44336", "#FF9800", "#FFD54F", "#8BC34A", "#26C6DA",
                             "#2196F3", "#7E57C2", "#EC407A", "#795548"}) {
        list << QColor(QLatin1StringView(name));
    }
    return list;
}

QVariantList ClipInspector::speedPresets() const
{
    QVariantList list;
    for (const QString &id : SpeedCurveUtil::presetIds()) {
        QVariantMap item;
        item[u"id"_s] = id;
        item[u"label"_s] = SpeedCurveUtil::presetTitle(id, u"it"_s);
        list.append(item);
    }
    return list;
}

QVariantList ClipInspector::speedPresetPoints(const QString &presetId) const
{
    QVariantList list;
    const SpeedCurve curve = SpeedCurveUtil::preset(presetId);
    for (const auto &pt : curve.points) {
        list.append(QVariantList{QVariant(pt.first), QVariant(pt.second)});
    }
    return list;
}

QVariantMap ClipInspector::values() const
{
    QVariantMap map;
    const Clip *clip = focus();
    const Track *track = nullptr;
    if (const vedit::Transition *transition = clip ? nullptr : focusTransition(&track)) {
        const fx::TransitionPreset *preset = fx::Library::core().transition(transition->type.id);
        const Clip *from = track->findClip(transition->from);
        const Clip *to = track->findClip(transition->to);
        map[u"transition.type"_s] = transition->type.id;
        map[u"transition.name"_s] = preset ? preset->name.text() : transition->type.id;
        map[u"transition.duration"_s] = transition->duration.toSecondsDouble();
        map[u"transition.maxDuration"_s] =
            from && to ? std::min(from->duration, to->duration).toSecondsDouble() : transition->duration.toSecondsDouble();
        return map;
    }
    if (!clip) {
        return map;
    }
    // Animated parameters show their value at the playhead (at the clip's start when the playhead is elsewhere).
    const std::optional<RationalTime> keyTime = playheadKeyTime(*clip);
    const RationalTime at = keyTime ? *keyTime : keyframeTime(*clip, RationalTime(0, clip->duration.rate()));
    const auto valueOf = [&at](const Param &param) { return param.isAnimated() ? Param(param.valueAt(at)) : param; };
    const Vec2 position = vectorOf(valueOf(clip->transform.position), {0, 0});
    map[u"x"_s] = position.x;
    map[u"y"_s] = position.y;
    map[u"scale"_s] = vectorOf(valueOf(clip->transform.scale), {1, 1}).x;
    map[u"rotation"_s] = numberOf(valueOf(clip->transform.rotation), 0.0);
    map[u"opacity"_s] = numberOf(valueOf(clip->opacity), 1.0);
    map[u"kf.available"_s] = keyTime.has_value();
    QString easing;
    QVariantList curve{0.0, 0.0, 1.0, 1.0}; // the control points of the movement from the keyframe here
    for (const QString &key : kKeyframeKeys) {
        const Param *param = keyframeParam(*clip, key);
        if (!param) {
            continue;
        }
        int state = param->isAnimated() ? 1 : 0;
        for (const Keyframe &keyframe : param->keyframes()) {
            if (keyTime && keyframe.time == *keyTime) {
                state = 2;
                if (easing.isEmpty()) {
                    easing = easingName(keyframe);
                    if (keyframe.interpolation == Interpolation::Bezier) {
                        const std::array<double, 4> &b = keyframe.easing.bezier();
                        if (!(b[0] == 0 && b[1] == 0 && b[2] == 0 && b[3] == 0)) {
                            curve = {b[0], b[1], b[2], b[3]};
                        }
                    }
                }
            }
        }
        map[u"kf."_s + key] = state;
    }
    map[u"kf.easing"_s] = easing;
    map[u"kf.here"_s] = !easing.isEmpty();
    map[u"kf.curve"_s] = curve;
    map[u"flipH"_s] = clip->transform.flipH;
    map[u"flipV"_s] = clip->transform.flipV;
    map[u"fit"_s] = clip->transform.fit == FitMode::Cover ? 1 : 0;
    map[u"blend"_s] = static_cast<int>(clip->blendMode);
    // The first mask (the interface edits one; more are kept).
    const Mask *mask = clip->masks.empty() ? nullptr : &clip->masks.front();
    map[u"mask.shape"_s] = mask && mask->shape != MaskShape::Path ? static_cast<int>(mask->shape) : -1;
    const Vec2 maskCentre = mask ? vectorOf(mask->center, {0, 0}) : Vec2{0, 0};
    const Vec2 maskSize = mask ? vectorOf(mask->size, {0.5, 0.5}) : Vec2{0.5, 0.5};
    map[u"mask.x"_s] = maskCentre.x;
    map[u"mask.y"_s] = maskCentre.y;
    map[u"mask.width"_s] = maskSize.x;
    map[u"mask.height"_s] = maskSize.y;
    map[u"mask.rotation"_s] = mask ? numberOf(mask->rotation, 0.0) : 0.0;
    map[u"mask.roundness"_s] = mask ? numberOf(mask->roundness, 0.0) : 0.0;
    map[u"mask.feather"_s] = mask ? numberOf(mask->feather, 0.0) : 0.0;
    map[u"mask.invert"_s] = mask && mask->invert;
    const Effect *chroma = findEffect(*clip, kChromaType);
    const auto chromaParam = [chroma](const QString &name, double fallback) {
        const auto it = chroma ? chroma->params.find(name) : std::map<QString, Param>::const_iterator{};
        return chroma && it != chroma->params.end() ? numberOf(it->second, fallback) : fallback;
    };
    map[u"chroma.enabled"_s] = chroma != nullptr;
    map[u"chroma.color"_s] = toQColor(chroma && chroma->params.contains(u"keyColor"_s)
                                          ? colorOf(chroma->params.at(u"keyColor"_s), Color{0, 255, 0, 255})
                                          : Color{0, 255, 0, 255});
    map[u"chroma.similarity"_s] = chromaParam(u"similarity"_s, 0.4);
    map[u"chroma.smoothness"_s] = chromaParam(u"smoothness"_s, 0.1);
    map[u"chroma.spill"_s] = chromaParam(u"spill"_s, 0.5);

    const Sequence *sequence = m_editor.data().mainSequence();
    const CanvasBackground background = clip->background ? *clip->background
                                        : sequence && sequence->defaultBackground ? *sequence->defaultBackground
                                                                                  : CanvasBackground{};
    map[u"background.type"_s] = background.type == BackgroundType::Blur ? 1 : 0;
    map[u"background.color"_s] = toQColor(background.color);
    map[u"background.blur"_s] = background.amount;

    if (const MediaClipData *media = clip->media()) {
        map[u"volume"_s] = numberOf(media->audio.gainDb, 0.0);
        map[u"fadeIn"_s] = media->audio.fadeIn ? media->audio.fadeIn->toSecondsDouble() : 0.0;
        map[u"fadeOut"_s] = media->audio.fadeOut ? media->audio.fadeOut->toSecondsDouble() : 0.0;
        map[u"speed"_s] = media->speed;
        map[u"reversed"_s] = media->reversed;
        map[u"preservePitch"_s] = media->preservePitch;
        map[u"speed.isCurve"_s] = media->curve.has_value();
        map[u"speed.curvePreset"_s] = media->curve ? media->curve->preset : QString();
        QVariantList pointsList;
        if (media->curve) {
            for (const auto &pt : media->curve->points) {
                pointsList.append(QVariantList{QVariant(pt.first), QVariant(pt.second)});
            }
        }
        map[u"speed.curvePoints"_s] = pointsList;
    }
    const Effect *motionBlurFx = findEffect(*clip, kMotionBlurType);
    map[u"speed.motionBlur"_s] = (motionBlurFx != nullptr && motionBlurFx->enabled);
    map[u"speed.motionBlurIntensity"_s] = motionBlurFx && motionBlurFx->params.count(u"intensity"_s)
        ? numberOf(motionBlurFx->params.at(u"intensity"_s), 0.5) : 0.5;
    const Effect *denoise = findEffect(*clip, u"vedit.denoise"_s);
    map[u"audio.denoise"_s] = denoise != nullptr && denoise->enabled;
    map[u"audio.denoiseAmount"_s] = denoise && denoise->params.count(u"amount"_s) ? numberOf(denoise->params.at(u"amount"_s), 1.0) : 1.0;

    const Effect *voiceFx = findEffect(*clip, u"vedit.voice_effect"_s);
    QString vPreset = u"none"_s;
    if (voiceFx && voiceFx->params.count(u"preset"_s)) {
        if (const auto *s = std::get_if<QString>(&voiceFx->params.at(u"preset"_s).staticValue())) {
            vPreset = *s;
        }
    }
    map[u"audio.voiceEffect"_s] = vPreset;
    map[u"audio.enhanceVoice"_s] = (vPreset == u"enhance"_s);

    const Effect *eq = findEffect(*clip, u"vedit.eq"_s);
    map[u"audio.eq.low"_s] = eq && eq->params.count(u"low"_s) ? numberOf(eq->params.at(u"low"_s), 0.0) : 0.0;
    map[u"audio.eq.mid"_s] = eq && eq->params.count(u"mid"_s) ? numberOf(eq->params.at(u"mid"_s), 0.0) : 0.0;
    map[u"audio.eq.high"_s] = eq && eq->params.count(u"high"_s) ? numberOf(eq->params.at(u"high"_s), 0.0) : 0.0;

    const Effect *comp = findEffect(*clip, u"vedit.compressor"_s);
    map[u"audio.compressor.enabled"_s] = comp != nullptr && comp->enabled;
    map[u"audio.compressor.threshold"_s] = comp && comp->params.count(u"threshold"_s) ? numberOf(comp->params.at(u"threshold"_s), -18.0) : -18.0;
    map[u"audio.compressor.ratio"_s] = comp && comp->params.count(u"ratio"_s) ? numberOf(comp->params.at(u"ratio"_s), 3.0) : 3.0;
    ClipAnimations animations = clip->animations;
    for (const QString &kind : {u"in"_s, u"out"_s, u"loop"_s}) {
        const std::optional<ClipAnimation> &animation = animationSlot(animations, kind);
        const fx::AnimationPreset *preset = animation ? fx::Library::core().animation(animation->type.id) : nullptr;
        map[u"animation."_s + kind] = animation ? animation->type.id : QString();
        map[u"animation."_s + kind + u".name"_s] = preset ? preset->name.text() : QString();
        map[u"animation."_s + kind + u".duration"_s] = animation ? animation->duration.toSecondsDouble() : 0.0;
    }
    const Effect *filter = findEffect(*clip, kFilterType);
    map[u"filter"_s] = filter && filter->preset ? filter->preset->id : QString();
    const fx::FilterPreset *preset = filter && filter->preset ? fx::Library::core().filter(filter->preset->id) : nullptr;
    map[u"filter.name"_s] = preset ? preset->name.text() : QString();
    map[u"filter.intensity"_s] = filter ? numberOf(filter->intensity, 1.0) : 1.0;
    const Effect *adjust = findEffect(*clip, kAdjustType);
    for (const QVariant &spec : adjustParams()) {
        const QVariantMap param = spec.toMap();
        const QString name = param.value(u"name"_s).toString();
        const auto it = adjust ? adjust->params.find(name) : std::map<QString, Param>::const_iterator{};
        map[u"adjust."_s + name] = adjust && it != adjust->params.end() ? numberOf(it->second, 0.0)
                                                                         : param.value(u"default"_s).toDouble();
    }

    const Effect *lut = findEffect(*clip, kLutType);
    QString lutPath;
    if (lut && lut->params.count(u"path"_s)) {
        if (const auto *s = std::get_if<QString>(&lut->params.at(u"path"_s).staticValue())) {
            lutPath = *s;
        }
    }
    map[u"lut.path"_s] = lutPath;
    map[u"lut.name"_s] = lutPath.isEmpty() ? QString() : QFileInfo(lutPath).fileName();
    map[u"lut.intensity"_s] = lut ? numberOf(lut->intensity, 1.0) : 1.0;

    const Effect *deflicker = findEffect(*clip, kDeflickerType);
    map[u"deflicker.enabled"_s] = deflicker != nullptr && deflicker->enabled;
    map[u"deflicker.size"_s] = deflicker && deflicker->params.count(u"size"_s) ? numberOf(deflicker->params.at(u"size"_s), 5.0) : 5.0;
    QString defMode = u"pm"_s;
    if (deflicker && deflicker->params.count(u"mode"_s)) {
        if (const auto *s = std::get_if<QString>(&deflicker->params.at(u"mode"_s).staticValue())) {
            defMode = *s;
        }
    }
    map[u"deflicker.mode"_s] = defMode;

    const Effect *grade = findEffect(*clip, kGradeType);
    map[u"grade.balance.r"_s] = grade && grade->params.count(u"balance.r"_s) ? numberOf(grade->params.at(u"balance.r"_s), 1.0) : 1.0;
    map[u"grade.balance.g"_s] = grade && grade->params.count(u"balance.g"_s) ? numberOf(grade->params.at(u"balance.g"_s), 1.0) : 1.0;
    map[u"grade.balance.b"_s] = grade && grade->params.count(u"balance.b"_s) ? numberOf(grade->params.at(u"balance.b"_s), 1.0) : 1.0;

    for (const QString &zone : {u"shadows"_s, u"midtones"_s, u"highlights"_s}) {
        map[u"grade."_s + zone + u".r"_s] = grade && grade->params.count(zone + u".r"_s) ? numberOf(grade->params.at(zone + u".r"_s), 0.0) : 0.0;
        map[u"grade."_s + zone + u".g"_s] = grade && grade->params.count(zone + u".g"_s) ? numberOf(grade->params.at(zone + u".g"_s), 0.0) : 0.0;
        map[u"grade."_s + zone + u".b"_s] = grade && grade->params.count(zone + u".b"_s) ? numberOf(grade->params.at(zone + u".b"_s), 0.0) : 0.0;
        map[u"grade."_s + zone + u".level"_s] = grade && grade->params.count(zone + u".level"_s) ? numberOf(grade->params.at(zone + u".level"_s), 0.0) : 0.0;
    }

    for (int r = 0; r < fx::Grade::kRanges; ++r) {
        const QString rangeName = fx::Grade::rangeName(r);
        map[u"grade.hsl."_s + rangeName + u".hue"_s] = grade && grade->params.count(u"hsl."_s + rangeName + u".hue"_s) ? numberOf(grade->params.at(u"hsl."_s + rangeName + u".hue"_s), 0.0) : 0.0;
        map[u"grade.hsl."_s + rangeName + u".saturation"_s] = grade && grade->params.count(u"hsl."_s + rangeName + u".saturation"_s) ? numberOf(grade->params.at(u"hsl."_s + rangeName + u".saturation"_s), 0.0) : 0.0;
        map[u"grade.hsl."_s + rangeName + u".lightness"_s] = grade && grade->params.count(u"hsl."_s + rangeName + u".lightness"_s) ? numberOf(grade->params.at(u"hsl."_s + rangeName + u".lightness"_s), 0.0) : 0.0;
    }

    for (const QString &ch : {u"master"_s, u"r"_s, u"g"_s, u"b"_s}) {
        const QString curveKey = u"curve."_s + ch;
        QVariantList curvePoints;
        if (grade && grade->params.count(curveKey)) {
            const ParamValue &pv = grade->params.at(curveKey).staticValue();
            if (const auto *s = std::get_if<QString>(&pv)) {
                QJsonDocument doc = QJsonDocument::fromJson(s->toUtf8());
                if (doc.isArray()) {
                    for (const auto &val : doc.array()) {
                        if (val.isArray()) {
                            const auto pt = val.toArray();
                            if (pt.size() >= 2) {
                                curvePoints << QVariant(QVariantList{pt.at(0).toDouble(), pt.at(1).toDouble()});
                            }
                        }
                    }
                }
            } else if (const auto *jv = std::get_if<QJsonValue>(&pv)) {
                if (jv->isArray()) {
                    for (const auto &val : jv->toArray()) {
                        if (val.isArray()) {
                            const auto pt = val.toArray();
                            if (pt.size() >= 2) {
                                curvePoints << QVariant(QVariantList{pt.at(0).toDouble(), pt.at(1).toDouble()});
                            }
                        }
                    }
                }
            }
        }
        map[u"grade.curve."_s + ch] = curvePoints;
    }

    {
        QVariantList effects;
        int index = 0;
        for (const Effect *effect : libraryEffects(*clip)) {
            const fx::VideoEffectPreset *preset = effect->preset ? fx::Library::core().videoEffect(effect->preset->id) : nullptr;
            QVariantList controls;
            if (preset) {
                for (const QString &name : preset->controls) {
                    const auto own = effect->params.find(name);
                    if (name == u"color"_s || name == u"color2"_s) {
                        QColor value;
                        if (own != effect->params.end()) {
                            value = toQColor(colorOf(own->second, Color{255, 255, 255, 255}));
                        } else {
                            const QJsonArray rgb = preset->params.value(name).toArray();
                            value = QColor::fromRgbF(static_cast<float>(rgb.at(0).toDouble(1)), static_cast<float>(rgb.at(1).toDouble(1)),
                                                     static_cast<float>(rgb.at(2).toDouble(1)));
                        }
                        controls << QVariantMap{{u"name"_s, name}, {u"color"_s, true}, {u"value"_s, value},
                                                {u"label"_s, name == u"color"_s ? tr("Colour") : tr("Second colour")}};
                        continue;
                    }
                    const double presetValue = preset->params.value(name).toDouble(name == u"speed"_s ? 1.0 : 0.5);
                    const double value = own != effect->params.end() ? numberOf(own->second, presetValue) : presetValue;
                    const ControlRange range = controlRange(effect->type, name, presetValue);
                    const QString label = name == u"amount"_s ? tr("Intensity")
                                          : name == u"size"_s ? tr("Size")
                                          : name == u"speed"_s ? tr("Speed")
                                          : name == u"angle"_s ? tr("Angle")
                                                               : name;
                    controls << QVariantMap{{u"name"_s, name}, {u"color"_s, false}, {u"value"_s, value}, {u"from"_s, range.from},
                                            {u"to"_s, range.to}, {u"neutral"_s, presetValue}, {u"label"_s, label}};
                }
            }
            effects << QVariantMap{{u"index"_s, index++},
                                   {u"preset"_s, effect->preset ? effect->preset->id : QString()},
                                   {u"name"_s, preset ? preset->name.text() : effect->type},
                                   {u"mix"_s, numberOf(effect->intensity, 1.0)},
                                   {u"controls"_s, controls}};
        }
        map[u"effects"_s] = effects;
    }
    if (const StickerClipData *sticker = clip->sticker()) {
        map[u"sticker.visualizer"_s] = sticker->visualizer.has_value();
        const AudioVisualizerSettings visualizer = sticker->visualizer.value_or(AudioVisualizerSettings{});
        map[u"sticker.style"_s] = static_cast<int>(visualizer.style);
        map[u"sticker.barCount"_s] = visualizer.barCount;
        map[u"sticker.primaryColor"_s] = toQColor(visualizer.primaryColor);
        map[u"sticker.secondaryColor"_s] = toQColor(visualizer.secondaryColor);
        map[u"sticker.sensitivity"_s] = visualizer.sensitivity;
        map[u"sticker.smoothing"_s] = visualizer.smoothing;
        map[u"sticker.mirror"_s] = visualizer.mirror;
        map[u"sticker.tinted"_s] = sticker->tint.a > 0;
        map[u"sticker.tint"_s] = toQColor(sticker->tint.a > 0 ? sticker->tint : Color{255, 255, 255, 255});
        const fx::StickerPreset *preset = sticker->source ? fx::Library::core().sticker(sticker->source->id) : nullptr;
        const Media *media = sticker->mediaId.isNull() ? nullptr : m_editor.data().findMedia(sticker->mediaId);
        map[u"sticker.animated"_s] = (preset && preset->animated) ||
                                     (media && media->path.endsWith(u".gif"_s, Qt::CaseInsensitive)) ||
                                     (media && media->path.endsWith(u".webp"_s, Qt::CaseInsensitive));
        map[u"sticker.speed"_s] = sticker->speed;
        map[u"sticker.loop"_s] = sticker->loop;
        map[u"sticker.graphic"_s] = sticker->graphic.has_value();
        const GraphicSettings graphic = sticker->graphic.value_or(GraphicSettings{});
        map[u"sticker.graphicKind"_s] = static_cast<int>(graphic.kind);
        map[u"sticker.from"_s] = graphic.from;
        map[u"sticker.to"_s] = graphic.to;
        map[u"sticker.decimals"_s] = graphic.decimals;
        map[u"sticker.prefix"_s] = graphic.prefix;
        map[u"sticker.suffix"_s] = graphic.suffix;
        map[u"sticker.color"_s] = toQColor(graphic.color);
        map[u"sticker.color2"_s] = toQColor(graphic.color2);
        map[u"sticker.thickness"_s] = graphic.thickness;
        map[u"sticker.drawSeconds"_s] = graphic.drawSeconds;
    }
    if (const TextClipData *text = clip->text()) {
        const TextStyle &style = text->style;
        map[u"text.content"_s] = text->text;
        map[u"text.font"_s] = style.fontFamily;
        map[u"text.size"_s] = numberOf(style.size, 0.06);
        map[u"text.color"_s] = toQColor(colorOf(style.color, Color{255, 255, 255, 255}));
        map[u"text.bold"_s] = style.fontWeight >= 600;
        map[u"text.italic"_s] = style.italic;
        map[u"text.underline"_s] = style.underline;
        map[u"text.align"_s] = static_cast<int>(style.align);
        map[u"text.stroke"_s] = style.stroke.has_value();
        map[u"text.strokeColor"_s] = toQColor(style.stroke ? colorOf(style.stroke->color, Color{}) : Color{});
        map[u"text.strokeWidth"_s] = style.stroke ? style.stroke->width : TextStroke{}.width;
        map[u"text.shadow"_s] = style.shadow.has_value();
        map[u"text.background"_s] = style.background.has_value();
        map[u"text.backgroundColor"_s] = toQColor(style.background ? style.background->color : TextBackground{}.color);
        map[u"text.bubbleShape"_s] = style.background ? static_cast<int>(style.background->shape) : 0;
        map[u"text.bubbleTail"_s] = style.background ? static_cast<int>(style.background->tail) : 0;
        map[u"text.bubbleTailSize"_s] = style.background ? style.background->tailSize : 0.4;
        map[u"text.bubbleBorder"_s] = style.background ? (style.background->borderWidth > 0.0) : false;
        map[u"text.bubbleBorderColor"_s] = toQColor(style.background ? style.background->borderColor : Color{});
        map[u"text.bubbleBorderWidth"_s] = style.background ? style.background->borderWidth : 0.03;
        map[u"text.accentColor"_s] = toQColor(style.background ? style.background->accentColor : Color{255, 180, 0, 255});
        map[u"text.animation.enabled"_s] = text->animation.has_value() && (text->animation->type != TextAnimationType::None);
        map[u"text.animation.type"_s] = text->animation ? static_cast<int>(text->animation->type) : 0;
        map[u"text.animation.scope"_s] = text->animation ? static_cast<int>(text->animation->scope) : 0;
        map[u"text.animation.duration"_s] = text->animation ? text->animation->duration.toSecondsDouble() : 1.5;
        map[u"text.animation.cursor"_s] = text->animation ? text->animation->cursor : true;
        map[u"text.letterSpacing"_s] = numberOf(style.letterSpacing, 0.0);
        map[u"text.lineHeight"_s] = style.lineHeight;
        map[u"text.preset"_s] = text->stylePreset ? text->stylePreset->id : QString();
    }
    return map;
}

QString ClipInspector::sectionOf(const QString &key) const
{
    static const QStringList video{u"x"_s,     u"y"_s,     u"scale"_s, u"rotation"_s, u"opacity"_s,
                                   u"flipH"_s, u"flipV"_s, u"fit"_s,   u"blend"_s};
    static const QStringList audio{u"volume"_s, u"fadeIn"_s, u"fadeOut"_s};
    static const QStringList speed{u"speed"_s, u"reversed"_s, u"preservePitch"_s};
    if (video.contains(key)) {
        return u"video"_s;
    }
    if (audio.contains(key) || key.startsWith(u"audio."_s)) {
        return u"audio"_s;
    }
    if (speed.contains(key) || key.startsWith(u"speed."_s)) {
        return u"speed"_s;
    }
    if (key.startsWith(u"mask."_s) || key.startsWith(u"chroma."_s)) {
        return u"cutout"_s;
    }
    if (key.startsWith(u"grade."_s) || key.startsWith(u"lut."_s) || key.startsWith(u"deflicker."_s)) {
        return u"adjust"_s;
    }
    const qsizetype dot = key.indexOf(u'.');
    return dot > 0 ? key.left(dot) : key; // background.*, filter.*, adjust.*, text.*
}

std::vector<ClipId> ClipInspector::targets(const QString &section) const
{
    std::vector<ClipId> result;
    for (const ClipId &id : m_editor.selectedClips()) {
        const Clip *clip = m_editor.data().findClip(id);
        if (clip && supports(*clip, section)) {
            result.push_back(id);
        }
    }
    return result;
}

MergeKey ClipInspector::gestureKey(const QString &target)
{
    return MergeKey{u"inspector:"_s + target, m_gesture};
}

bool ClipInspector::update(const std::vector<ClipId> &clips, const std::function<void(Clip &)> &change,
                           const QString &text, const QString &mergeTarget)
{
    if (clips.empty()) {
        return false;
    }
    EditResult result = TimelineEditor(m_editor.data(), m_editor.data().mainSequenceId).updateClips(clips, change, text);
    result.primaryClip = {}; // the selection stays as it is
    return m_editor.push(std::move(result), mergeTarget.isEmpty() ? MergeKey{} : gestureKey(mergeTarget));
}

void ClipInspector::endGesture()
{
    ++m_gesture;
}

bool ClipInspector::set(const QString &key, const QVariant &value)
{
    if (key == u"transition.duration"_s) {
        const vedit::Transition *transition = focusTransition();
        if (!transition) {
            return false;
        }
        const Rational rate = m_editor.data().settings.frameRate;
        const RationalTime duration(std::max<std::int64_t>(1, std::llround(value.toDouble() * rate.toDouble())), rate);
        EditResult result = TimelineEditor(m_editor.data(), m_editor.data().mainSequenceId)
                                .updateTransition(transition->id, [&duration](vedit::Transition &t) { t.duration = duration; });
        result.text = tr("Change transition duration");
        return m_editor.push(std::move(result), gestureKey(u"transition:"_s + transition->id.toString()));
    }
    const Clip *clip = focus();
    if (!clip) {
        return false;
    }
    const std::vector<ClipId> clips = targets(sectionOf(key));
    QString mergeTarget = (key == u"x"_s || key == u"y"_s) ? u"position"_s : key;
    for (const ClipId &id : clips) {
        mergeTarget += u':' + id.toString();
    }
    const double number = value.toDouble();
    const bool flag = value.toBool();

    // Transform and opacity: a static value, or, once animated, a keyframe at the playhead (auto keyframe; nothing on a
    // clip the playhead is not on).
    const RationalTime playhead(m_editor.player()->position(), m_editor.data().settings.frameRate);
    const auto write = [&playhead](Clip &c, Param &param, const std::function<ParamValue(const ParamValue &)> &change) {
        if (!param.isAnimated()) {
            param = Param(change(param.staticValue()));
            return;
        }
        if (playhead < c.start || !(playhead < c.end())) {
            return;
        }
        const RationalTime time = keyframeTime(c, playhead - c.start);
        setKeyframeValue(param, time, change(param.valueAt(time)));
    };
    if (key == u"x"_s || key == u"y"_s) {
        return update(clips, [&](Clip &c) {
            write(c, c.transform.position, [&](const ParamValue &current) {
                const Vec2 *v = std::get_if<Vec2>(&current);
                Vec2 position = v ? *v : Vec2{0, 0};
                (key == u"x"_s ? position.x : position.y) = std::clamp(number, -4.0, 4.0);
                return ParamValue(position);
            });
        }, tr("Move clip"), mergeTarget);
    }
    if (key == u"scale"_s) {
        return update(clips, [&](Clip &c) {
            const double scale = std::clamp(number, 0.01, 20.0);
            write(c, c.transform.scale, [scale](const ParamValue &) { return ParamValue(Vec2{scale, scale}); });
            c.transform.uniformScale = true;
        }, tr("Resize clip"), mergeTarget);
    }
    if (key == u"rotation"_s) {
        return update(clips, [&](Clip &c) {
            write(c, c.transform.rotation, [&](const ParamValue &) { return ParamValue(std::fmod(number, 360.0)); });
        }, tr("Rotate clip"), mergeTarget);
    }
    if (key == u"opacity"_s) {
        return update(clips, [&](Clip &c) {
            write(c, c.opacity, [&](const ParamValue &) { return ParamValue(std::clamp(number, 0.0, 1.0)); });
        }, tr("Change opacity"), mergeTarget);
    }
    if (key == u"flipH"_s || key == u"flipV"_s) {
        return update(clips, [&](Clip &c) { (key == u"flipH"_s ? c.transform.flipH : c.transform.flipV) = flag; },
                      tr("Mirror clip"), {});
    }
    if (key == u"fit"_s) {
        return update(clips, [&](Clip &c) { c.transform.fit = value.toInt() == 1 ? FitMode::Cover : FitMode::Contain; },
                      value.toInt() == 1 ? tr("Fill the canvas") : tr("Show the whole picture"), {});
    }

    if (key == u"blend"_s) {
        return update(clips, [&](Clip &c) { c.blendMode = static_cast<BlendMode>(std::clamp(value.toInt(), 0, kBlendModeCount - 1)); },
                      tr("Change blend mode"), {});
    }

    // Mask (the first one)
    if (key == u"mask.shape"_s) {
        const int shape = value.toInt();
        return update(clips, [&](Clip &c) {
            if (shape < 0 || shape > static_cast<int>(MaskShape::Star)) {
                c.masks.clear();
                return;
            }
            if (c.masks.empty()) {
                Mask mask;
                mask.id = MaskId::create();
                c.masks.push_back(mask);
            }
            c.masks.front().shape = static_cast<MaskShape>(shape);
        }, shape < 0 ? tr("Remove mask") : tr("Change mask"), {});
    }
    if (key.startsWith(u"mask."_s)) {
        const QString field = key.mid(5);
        return update(clips, [&](Clip &c) {
            if (c.masks.empty()) {
                return;
            }
            Mask &mask = c.masks.front();
            if (field == u"x"_s || field == u"y"_s) {
                Vec2 centre = vectorOf(mask.center, {0, 0});
                (field == u"x"_s ? centre.x : centre.y) = std::clamp(number, -1.0, 1.0);
                mask.center = Param(centre);
            } else if (field == u"width"_s || field == u"height"_s) {
                Vec2 size = vectorOf(mask.size, {0.5, 0.5});
                (field == u"width"_s ? size.x : size.y) = std::clamp(number, 0.01, 4.0);
                mask.size = Param(size);
            } else if (field == u"rotation"_s) {
                mask.rotation = Param(std::fmod(number, 360.0));
            } else if (field == u"roundness"_s) {
                mask.roundness = Param(std::clamp(number, 0.0, 1.0));
            } else if (field == u"feather"_s) {
                mask.feather = Param(std::clamp(number, 0.0, 1.0));
            } else if (field == u"invert"_s) {
                mask.invert = flag;
            }
        }, tr("Change mask"), field == u"invert"_s ? QString() : mergeTarget);
    }

    // Chroma key
    if (key == u"chroma.enabled"_s) {
        return update(clips, [&](Clip &c) {
            if (!flag) {
                removeEffect(c, kChromaType);
                return;
            }
            Effect &effect = ensureEffect(c, kChromaType);
            for (const auto &[name, fallback] : {std::pair{u"similarity"_s, 0.4}, {u"smoothness"_s, 0.1}, {u"spill"_s, 0.5}}) {
                if (!effect.params.contains(name)) {
                    effect.params[name] = Param(fallback);
                }
            }
            if (!effect.params.contains(u"keyColor"_s)) {
                effect.params[u"keyColor"_s] = Param(Color{0, 255, 0, 255});
            }
        }, flag ? tr("Remove the background colour") : tr("Keep the background colour"), {});
    }
    if (key.startsWith(u"chroma."_s)) {
        const QString field = key.mid(7);
        return update(clips, [&](Clip &c) {
            Effect &effect = ensureEffect(c, kChromaType);
            if (field == u"color"_s) {
                effect.params[u"keyColor"_s] = Param(toColor(value));
            } else {
                effect.params[field] = Param(std::clamp(number, 0.0, 1.0));
            }
        }, tr("Adjust the background removal"), field == u"color"_s ? QString() : mergeTarget);
    }

    // Background
    if (key.startsWith(u"background."_s)) {
        const Sequence *sequence = m_editor.data().mainSequence();
        const std::optional<CanvasBackground> fallback = sequence ? sequence->defaultBackground : std::nullopt;
        return update(clips, [&](Clip &c) {
            CanvasBackground background = c.background ? *c.background : fallback.value_or(CanvasBackground{});
            if (key == u"background.type"_s) {
                background.type = value.toInt() == 1 ? BackgroundType::Blur : BackgroundType::Color;
            } else if (key == u"background.color"_s) {
                background.type = BackgroundType::Color;
                background.color = toColor(value);
            } else if (key == u"background.blur"_s) {
                background.type = BackgroundType::Blur;
                background.amount = std::clamp(number, 0.0, 1.0);
            }
            c.background = background;
        }, tr("Change background"), mergeTarget);
    }

    // Audio
    if (key == u"volume"_s) {
        return update(clips, [&](Clip &c) {
            if (c.media()) {
                write(c, c.media()->audio.gainDb, [&](const ParamValue &) {
                    return ParamValue(std::clamp(number, -60.0, 20.0));
                });
            }
        }, tr("Change volume"), mergeTarget);
    }
    if (key == u"audio.denoise"_s) {
        const bool on = value.toBool();
        return update(clips, [on](Clip &c) {
            if (on) {
                ensureEffect(c, u"vedit.denoise"_s).params[u"amount"_s] = Param(1.0);
            } else {
                removeEffect(c, u"vedit.denoise"_s);
            }
        }, tr("Toggle noise reduction"), mergeTarget);
    }
    if (key == u"audio.denoiseAmount"_s) {
        return update(clips, [&](Clip &c) {
            ensureEffect(c, u"vedit.denoise"_s).params[u"amount"_s] = Param(std::clamp(number, 0.0, 1.0));
        }, tr("Change noise reduction amount"), mergeTarget);
    }
    if (key == u"audio.voiceEffect"_s) {
        const QString preset = value.toString();
        return update(clips, [preset](Clip &c) {
            if (preset.isEmpty() || preset == u"none"_s) {
                removeEffect(c, u"vedit.voice_effect"_s);
            } else {
                ensureEffect(c, u"vedit.voice_effect"_s).params[u"preset"_s] = Param(preset);
            }
        }, tr("Change voice effect"), mergeTarget);
    }
    if (key == u"audio.enhanceVoice"_s) {
        const bool on = value.toBool();
        return update(clips, [on](Clip &c) {
            if (on) {
                ensureEffect(c, u"vedit.voice_effect"_s).params[u"preset"_s] = Param(u"enhance"_s);
                ensureEffect(c, u"vedit.denoise"_s).params[u"amount"_s] = Param(0.9);
            } else {
                removeEffect(c, u"vedit.voice_effect"_s);
            }
        }, tr("Enhance voice"), mergeTarget);
    }
    if (key.startsWith(u"audio.eq."_s)) {
        const QString band = key.mid(9);
        return update(clips, [&](Clip &c) {
            ensureEffect(c, u"vedit.eq"_s).params[band] = Param(std::clamp(number, -24.0, 24.0));
        }, tr("Change equalizer"), mergeTarget);
    }
    if (key == u"audio.compressor.enabled"_s) {
        const bool on = value.toBool();
        return update(clips, [on](Clip &c) {
            if (on) {
                ensureEffect(c, u"vedit.compressor"_s);
            } else {
                removeEffect(c, u"vedit.compressor"_s);
            }
        }, tr("Toggle compressor"), mergeTarget);
    }
    if (key.startsWith(u"audio.compressor."_s)) {
        const QString param = key.mid(17);
        return update(clips, [&](Clip &c) {
            ensureEffect(c, u"vedit.compressor"_s).params[param] = Param(number);
        }, tr("Change compressor"), mergeTarget);
    }
    if (key == u"fadeIn"_s || key == u"fadeOut"_s) {
        const Rational rate = m_editor.data().settings.frameRate;
        return update(clips, [&](Clip &c) {
            std::optional<RationalTime> fade;
            const double seconds = std::clamp(number, 0.0, c.duration.toSecondsDouble());
            if (seconds > 0.0) {
                fade = RationalTime(std::llround(seconds * rate.toDouble()), rate);
            }
            (key == u"fadeIn"_s ? c.media()->audio.fadeIn : c.media()->audio.fadeOut) = fade;
        }, key == u"fadeIn"_s ? tr("Change fade in") : tr("Change fade out"), mergeTarget);
    }

    // Speed: changes the length of the clip, so only the focused one.
    if (key == u"speed"_s) {
        if (!supports(*clip, u"speed"_s)) {
            return false;
        }
        EditResult result = TimelineEditor(m_editor.data(), m_editor.data().mainSequenceId)
                                .setSpeed(clip->id, std::clamp(number, 0.1, 100.0));
        result.primaryClip = {};
        return m_editor.push(std::move(result), gestureKey(u"speed:"_s + clip->id.toString()));
    }
    if (key == u"reversed"_s) {
        return update(clips, [&](Clip &c) { c.media()->reversed = flag; }, flag ? tr("Reverse clip") : tr("Play forwards"), {});
    }
    if (key == u"preservePitch"_s) {
        return update(clips, [&](Clip &c) { c.media()->preservePitch = flag; }, tr("Change pitch"), {});
    }
    if (key == u"speed.isCurve"_s) {
        if (!supports(*clip, u"speed"_s)) {
            return false;
        }
        if (flag) {
            auto curve = SpeedCurveUtil::preset(u"montage"_s);
            EditResult result = TimelineEditor(m_editor.data(), m_editor.data().mainSequenceId)
                                    .setSpeedCurve(clip->id, curve);
            result.primaryClip = {};
            return m_editor.push(std::move(result), gestureKey(u"speed-curve:"_s + clip->id.toString()));
        } else {
            EditResult result = TimelineEditor(m_editor.data(), m_editor.data().mainSequenceId)
                                    .removeSpeedCurve(clip->id);
            result.primaryClip = {};
            return m_editor.push(std::move(result), gestureKey(u"speed-curve:"_s + clip->id.toString()));
        }
    }
    if (key == u"speed.curvePreset"_s) {
        if (!supports(*clip, u"speed"_s)) {
            return false;
        }
        const QString presetName = value.toString();
        auto curve = SpeedCurveUtil::preset(presetName);
        EditResult result = TimelineEditor(m_editor.data(), m_editor.data().mainSequenceId)
                                .setSpeedCurve(clip->id, curve);
        result.primaryClip = {};
        return m_editor.push(std::move(result), gestureKey(u"speed-curve:"_s + clip->id.toString()));
    }
    if (key == u"speed.curvePoints"_s) {
        if (!supports(*clip, u"speed"_s)) {
            return false;
        }
        SpeedCurve curve;
        curve.preset = u"custom"_s;
        const QVariantList list = value.toList();
        for (const auto &item : list) {
            if (item.canConvert<QVariantList>()) {
                const QVariantList pt = item.toList();
                if (pt.size() >= 2) {
                    curve.points.push_back({pt[0].toDouble(), pt[1].toDouble()});
                }
            } else if (item.canConvert<QVariantMap>()) {
                const QVariantMap pt = item.toMap();
                curve.points.push_back({pt.value(u"x"_s).toDouble(), pt.value(u"y"_s).toDouble()});
            }
        }
        if (curve.points.size() < 2) {
            return false;
        }
        EditResult result = TimelineEditor(m_editor.data(), m_editor.data().mainSequenceId)
                                .setSpeedCurve(clip->id, curve);
        result.primaryClip = {};
        return m_editor.push(std::move(result), gestureKey(u"speed-curve-points:"_s + clip->id.toString()));
    }
    if (key == u"speed.motionBlur"_s) {
        const bool on = value.toBool();
        return update(clips, [on](Clip &c) {
            if (on) {
                ensureEffect(c, kMotionBlurType);
            } else {
                removeEffect(c, kMotionBlurType);
            }
        }, tr("Toggle motion blur"), mergeTarget);
    }
    if (key == u"speed.motionBlurIntensity"_s) {
        return update(clips, [&](Clip &c) {
            ensureEffect(c, kMotionBlurType).params[u"intensity"_s] = Param(std::clamp(number, 0.0, 1.0));
        }, tr("Change motion blur intensity"), mergeTarget);
    }

    // Preset animations: their length (at most the clip's)
    if (key.startsWith(u"animation."_s) && key.endsWith(u".duration"_s)) {
        const QString kind = key.section(u'.', 1, 1);
        const Rational rate = m_editor.data().settings.frameRate;
        return update(clips, [&](Clip &c) {
            if (std::optional<ClipAnimation> &animation = animationSlot(c.animations, kind)) {
                const std::int64_t frames = std::llround(number * rate.toDouble());
                animation->duration = RationalTime(std::clamp<std::int64_t>(frames, 1, c.duration.rescaled(rate, Rounding::NearestEven).value()), rate);
            }
        }, tr("Change animation length"), mergeTarget);
    }

    // Filter and adjustments
    if (key == u"filter"_s) {
        return toggleFilter(value.toString());
    }
    if (key == u"filter.intensity"_s) {
        return update(clips, [&](Clip &c) {
            if (findEffect(c, kFilterType)) {
                ensureEffect(c, kFilterType).intensity = Param(std::clamp(number, 0.0, 1.0));
            }
        }, tr("Change filter intensity"), mergeTarget);
    }
    if (key.startsWith(u"adjust."_s)) {
        const QString name = key.mid(7);
        return update(clips, [&](Clip &c) { ensureEffect(c, kAdjustType).params[name] = Param(number); },
                      tr("Adjust colour"), mergeTarget);
    }
    if (key == u"lut.path"_s) {
        const QString path = value.toString();
        return update(clips, [path](Clip &c) {
            if (path.isEmpty()) {
                removeEffect(c, kLutType);
            } else {
                ensureEffect(c, kLutType).params[u"path"_s] = Param(path);
            }
        }, tr("Change LUT"), mergeTarget);
    }
    if (key == u"lut.intensity"_s) {
        return update(clips, [&](Clip &c) {
            if (findEffect(c, kLutType)) {
                ensureEffect(c, kLutType).intensity = Param(std::clamp(number, 0.0, 1.0));
            }
        }, tr("Change LUT intensity"), mergeTarget);
    }
    if (key == u"deflicker.enabled"_s) {
        const bool on = value.toBool();
        return update(clips, [on](Clip &c) {
            if (on) {
                ensureEffect(c, kDeflickerType);
            } else {
                removeEffect(c, kDeflickerType);
            }
        }, tr("Toggle deflicker"), mergeTarget);
    }
    if (key == u"deflicker.size"_s) {
        return update(clips, [&](Clip &c) {
            ensureEffect(c, kDeflickerType).params[u"size"_s] = Param(std::clamp(static_cast<double>(static_cast<int>(number)), 2.0, 129.0));
        }, tr("Change deflicker size"), mergeTarget);
    }
    if (key == u"deflicker.mode"_s) {
        const QString mode = value.toString();
        return update(clips, [mode](Clip &c) {
            ensureEffect(c, kDeflickerType).params[u"mode"_s] = Param(mode);
        }, tr("Change deflicker mode"), mergeTarget);
    }
    if (key.startsWith(u"grade.curve."_s)) {
        const QString channel = key.mid(12);
        QJsonArray arr;
        for (const QVariant &item : value.toList()) {
            const QVariantList pt = item.toList();
            if (pt.size() >= 2) {
                arr.append(QJsonArray{pt.at(0).toDouble(), pt.at(1).toDouble()});
            }
        }
        const QString jsonStr = QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact));
        return update(clips, [channel, jsonStr](Clip &c) {
            ensureEffect(c, kGradeType).params[u"curve."_s + channel] = Param(jsonStr);
        }, tr("Change color curve"), mergeTarget);
    }
    if (key.startsWith(u"grade."_s)) {
        const QString paramName = key.mid(6);
        return update(clips, [&](Clip &c) {
            ensureEffect(c, kGradeType).params[paramName] = Param(number);
        }, tr("Grade colour"), mergeTarget);
    }

    // Sticker
    if (key.startsWith(u"sticker."_s)) {
        const QString field = key.mid(8);
        return update(clips, [&](Clip &c) {
            StickerClipData &sticker = *c.sticker();
            if (field == u"tinted"_s) {
                sticker.tint = flag ? Color{255, 64, 129, 255} : Color{0, 0, 0, 0};
            } else if (field == u"tint"_s) {
                sticker.tint = toColor(value);
                sticker.tint.a = 255;
            } else if (field == u"speed"_s) {
                sticker.speed = std::clamp(number, 0.1, 10.0);
            } else if (field == u"loop"_s) {
                sticker.loop = flag;
            } else if (sticker.graphic) {
                GraphicSettings &g = *sticker.graphic;
                if (field == u"from"_s) {
                    g.from = number;
                } else if (field == u"to"_s) {
                    g.to = number;
                } else if (field == u"decimals"_s) {
                    g.decimals = std::clamp(value.toInt(), 0, 4);
                } else if (field == u"prefix"_s) {
                    g.prefix = value.toString().left(12);
                } else if (field == u"suffix"_s) {
                    g.suffix = value.toString().left(12);
                } else if (field == u"color"_s) {
                    g.color = toColor(value);
                } else if (field == u"color2"_s) {
                    g.color2 = toColor(value);
                } else if (field == u"thickness"_s) {
                    g.thickness = std::clamp(number, 0.0, 1.0);
                } else if (field == u"drawSeconds"_s) {
                    g.drawSeconds = std::clamp(number, 0.05, 10.0);
                }
            } else if (sticker.visualizer) {
                AudioVisualizerSettings &v = *sticker.visualizer;
                if (field == u"style"_s) {
                    v.style = static_cast<VisualizerStyle>(std::clamp(value.toInt(), 0, 3));
                } else if (field == u"barCount"_s) {
                    v.barCount = std::clamp(value.toInt(), 4, 128);
                } else if (field == u"primaryColor"_s) {
                    v.primaryColor = toColor(value);
                } else if (field == u"secondaryColor"_s) {
                    v.secondaryColor = toColor(value);
                } else if (field == u"sensitivity"_s) {
                    v.sensitivity = std::clamp(number, 0.1, 10.0);
                } else if (field == u"smoothing"_s) {
                    v.smoothing = std::clamp(number, 0.0, 1.0);
                } else if (field == u"mirror"_s) {
                    v.mirror = flag;
                }
            }
        }, tr("Edit sticker"), mergeTarget);
    }

    // Text
    if (key == u"text.preset"_s) {
        return applyTextStyle(value.toString());
    }
    if (key.startsWith(u"text."_s)) {
        const QString field = key.mid(5);
        return update(clips, [&](Clip &c) {
            TextClipData &text = *c.text();
            TextStyle &style = text.style;
            if (field == u"content"_s) {
                text.text = value.toString();
            } else if (field == u"font"_s) {
                style.fontFamily = value.toString();
            } else if (field == u"size"_s) {
                style.size = Param(std::clamp(number, 0.01, 1.0));
            } else if (field == u"color"_s) {
                style.color = Param(toColor(value));
            } else if (field == u"bold"_s) {
                style.fontWeight = flag ? 700 : 400;
            } else if (field == u"italic"_s) {
                style.italic = flag;
            } else if (field == u"underline"_s) {
                style.underline = flag;
            } else if (field == u"align"_s) {
                style.align = static_cast<TextAlign>(std::clamp(value.toInt(), 0, 2));
            } else if (field == u"stroke"_s) {
                style.stroke = flag ? std::optional<TextStroke>(TextStroke{}) : std::nullopt;
            } else if (field == u"strokeColor"_s) {
                style.stroke = style.stroke.value_or(TextStroke{});
                style.stroke->color = Param(toColor(value));
            } else if (field == u"strokeWidth"_s) {
                style.stroke = style.stroke.value_or(TextStroke{});
                style.stroke->width = std::clamp(number, 0.0, 0.5);
            } else if (field == u"shadow"_s) {
                style.shadow = flag ? std::optional<TextShadow>(TextShadow{}) : std::nullopt;
            } else if (field == u"background"_s) {
                style.background = flag ? std::optional<TextBackground>(TextBackground{}) : std::nullopt;
            } else if (field == u"backgroundColor"_s) {
                style.background = style.background.value_or(TextBackground{});
                style.background->color = toColor(value);
            } else if (field == u"bubbleShape"_s) {
                style.background = style.background.value_or(TextBackground{});
                style.background->shape = static_cast<BubbleShape>(std::clamp(value.toInt(), 0, 8));
            } else if (field == u"bubbleTail"_s) {
                style.background = style.background.value_or(TextBackground{});
                style.background->tail = static_cast<BubbleTail>(std::clamp(value.toInt(), 0, 7));
            } else if (field == u"bubbleTailSize"_s) {
                style.background = style.background.value_or(TextBackground{});
                style.background->tailSize = std::clamp(number, 0.05, 2.0);
            } else if (field == u"bubbleBorder"_s) {
                style.background = style.background.value_or(TextBackground{});
                style.background->borderWidth = flag ? 0.03 : 0.0;
            } else if (field == u"bubbleBorderColor"_s) {
                style.background = style.background.value_or(TextBackground{});
                style.background->borderColor = toColor(value);
            } else if (field == u"bubbleBorderWidth"_s) {
                style.background = style.background.value_or(TextBackground{});
                style.background->borderWidth = std::clamp(number, 0.0, 0.2);
            } else if (field == u"accentColor"_s) {
                style.background = style.background.value_or(TextBackground{});
                style.background->accentColor = toColor(value);
            } else if (field == u"animation.enabled"_s) {
                if (flag) {
                    if (!c.text()->animation) {
                        TextAnimation a;
                        a.type = TextAnimationType::Typewriter;
                        a.duration = RationalTime(45, 30);
                        c.text()->animation = a;
                    }
                } else {
                    c.text()->animation = std::nullopt;
                }
            } else if (field == u"animation.type"_s) {
                TextAnimation a = c.text()->animation.value_or(TextAnimation{});
                a.type = static_cast<TextAnimationType>(std::clamp(value.toInt(), 0, 9));
                if (a.duration.value() <= 0) {
                    a.duration = RationalTime(45, 30);
                }
                c.text()->animation = a;
            } else if (field == u"animation.scope"_s) {
                TextAnimation a = c.text()->animation.value_or(TextAnimation{});
                a.scope = static_cast<TextAnimationScope>(std::clamp(value.toInt(), 0, 3));
                c.text()->animation = a;
            } else if (field == u"animation.duration"_s) {
                TextAnimation a = c.text()->animation.value_or(TextAnimation{});
                const double durSec = std::clamp(number, 0.1, 10.0);
                a.duration = RationalTime(std::llround(durSec * 30.0), 30);
                c.text()->animation = a;
            } else if (field == u"animation.cursor"_s) {
                TextAnimation a = c.text()->animation.value_or(TextAnimation{});
                a.cursor = flag;
                c.text()->animation = a;
            } else if (field == u"letterSpacing"_s) {
                style.letterSpacing = Param(std::clamp(number, -0.5, 2.0));
            } else if (field == u"lineHeight"_s) {
                style.lineHeight = std::clamp(number, 0.5, 4.0);
            }
        }, field == u"content"_s ? tr("Edit text") : tr("Change text style"), mergeTarget);
    }
    return false;
}

bool ClipInspector::reset(const QString &section)
{
    const Clip *clip = focus();
    if (!clip) {
        return false;
    }
    const std::vector<ClipId> clips = targets(section);
    endGesture();
    bool done = false;
    if (section == u"video"_s) {
        done = update(clips, [](Clip &c) {
            const FitMode fit = c.transform.fit;
            c.transform = Transform{};
            c.transform.fit = fit;
            c.opacity = Param(1.0);
        }, tr("Reset position and size"), {});
    } else if (section == u"background"_s) {
        done = update(clips, [](Clip &c) { c.background.reset(); }, tr("Reset background"), {});
    } else if (section == u"sticker"_s) {
        done = update(clips, [](Clip &c) {
            StickerClipData &sticker = *c.sticker();
            sticker.tint = Color{0, 0, 0, 0};
            sticker.speed = 1.0;
            sticker.loop = true;
            sticker.visualizer = presetVisualizer(sticker);
            sticker.graphic = presetGraphic(sticker);
        }, tr("Reset sticker"), {});
    } else if (section == u"audio"_s) {
        done = update(clips, [](Clip &c) {
            if (c.media()) {
                c.media()->audio = ClipAudio{};
            }
            removeEffect(c, u"vedit.denoise"_s);
            removeEffect(c, u"vedit.voice_effect"_s);
            removeEffect(c, u"vedit.eq"_s);
            removeEffect(c, u"vedit.compressor"_s);
        }, tr("Reset volume and audio effects"), {});
    } else if (section == u"speed"_s) {
        // One undo step: the direction and effects, then the speed (which moves the following clips).
        const QString target = u"reset-speed:"_s + clip->id.toString();
        done = update({clip->id}, [](Clip &c) {
            c.media()->reversed = false;
            c.media()->preservePitch = true;
            removeEffect(c, kMotionBlurType);
        }, tr("Reset speed"), target);
        EditResult speed = TimelineEditor(m_editor.data(), m_editor.data().mainSequenceId).removeSpeedCurve(clip->id);
        if (clip->media()->speed != 1.0) {
            speed = TimelineEditor(m_editor.data(), m_editor.data().mainSequenceId).setSpeed(clip->id, 1.0);
        }
        speed.primaryClip = {};
        speed.text = tr("Reset speed");
        done = m_editor.push(std::move(speed), gestureKey(target)) || done;
    } else if (section == u"cutout"_s) {
        done = update(clips, [](Clip &c) {
            c.masks.clear();
            removeEffect(c, kChromaType);
        }, tr("Remove mask and green screen"), {});
    } else if (section == u"animation"_s) {
        done = update(clips, [](Clip &c) { c.animations = ClipAnimations{}; }, tr("Remove animations"), {});
    } else if (section == u"filter"_s) {
        done = update(clips, [](Clip &c) { removeEffect(c, kFilterType); }, tr("Remove filter"), {});
    } else if (section == u"adjust"_s) {
        done = update(clips, [](Clip &c) {
            removeEffect(c, kAdjustType);
            removeEffect(c, kGradeType);
            removeEffect(c, kLutType);
            removeEffect(c, kDeflickerType);
        }, tr("Reset adjustments"), {});
    } else if (section == u"grade"_s) {
        done = update(clips, [](Clip &c) { removeEffect(c, kGradeType); }, tr("Reset color grade"), {});
    } else if (section == u"lut"_s) {
        done = update(clips, [](Clip &c) { removeEffect(c, kLutType); }, tr("Remove LUT"), {});
    } else if (section == u"deflicker"_s) {
        done = update(clips, [](Clip &c) { removeEffect(c, kDeflickerType); }, tr("Reset deflicker"), {});
    } else if (section == u"text"_s) {
        done = update(clips, [](Clip &c) {
            c.text()->style = defaultTextStyle();
            c.text()->stylePreset.reset();
        }, tr("Reset text style"), {});
    } else if (section == u"textAnimation"_s) {
        done = update(clips, [](Clip &c) {
            c.text()->animation.reset();
        }, tr("Reset text animation"), {});
    }
    endGesture();
    return done;
}

bool ClipInspector::applyToAll(const QString &section)
{
    if (section == u"transition"_s) {
        const Track *track = nullptr;
        const vedit::Transition *transition = focusTransition(&track);
        if (!transition) {
            return false;
        }
        const vedit::Transition source = *transition;
        endGesture();
        EditResult result = TimelineEditor(m_editor.data(), m_editor.data().mainSequenceId)
                                .applyTransitionToAll(track->id, source.type, source.duration, source.params);
        if (!m_editor.push(std::move(result))) {
            return false;
        }
        int cuts = 0;
        if (const Track *updated = m_editor.data().findTrack(track->id)) {
            cuts = static_cast<int>(updated->transitions.size());
        }
        emit m_editor.message(tr("Transition on %n cut(s)", nullptr, cuts), true);
        return true;
    }
    const Clip *clip = focus();
    const Sequence *sequence = m_editor.data().mainSequence();
    if (!clip || !sequence || !supports(*clip, section) || section == u"speed"_s) {
        return false;
    }
    const Clip source = *clip;
    endGesture();
    const QString target = u"apply-all:"_s + section;
    int count = 0;
    std::vector<ClipId> clips;
    const auto sameKind = [this, &section](const Clip &c) { return supports(c, section); };
    for (const Track &track : sequence->visualTracks) {
        for (const Clip &c : track.clips) {
            if (sameKind(c)) {
                clips.push_back(c.id);
            }
        }
    }
    for (const Track &track : sequence->audioTracks) {
        for (const Clip &c : track.clips) {
            if (sameKind(c)) {
                clips.push_back(c.id);
            }
        }
    }
    bool done = false;
    if (section == u"background"_s) {
        // The default of the video (new clips get it too) and no clip with its own.
        std::vector<ClipId> own;
        for (const Clip &c : sequence->visualTracks.front().clips) {
            if (c.background) {
                own.push_back(c.id);
            }
        }
        const CanvasBackground background = source.background ? *source.background
                                                               : sequence->defaultBackground.value_or(CanvasBackground{});
        if (!own.empty()) {
            done = update(own, [](Clip &c) { c.background.reset(); }, tr("Apply background to all"), target);
        }
        EditResult setDefault =
            TimelineEditor(m_editor.data(), m_editor.data().mainSequenceId).setDefaultBackground(background);
        setDefault.text = tr("Apply background to all");
        done = m_editor.push(std::move(setDefault), gestureKey(target)) || done;
        count = static_cast<int>(clips.size());
    } else {
        std::function<void(Clip &)> change;
        QString text;
        if (section == u"video"_s) {
            change = [&source](Clip &c) {
                c.transform = source.transform;
                c.opacity = source.opacity;
            };
            text = tr("Apply position and size to all");
        } else if (section == u"audio"_s) {
            change = [&source](Clip &c) {
                if (c.media()) {
                    c.media()->audio = source.media()->audio;
                    copyEffect(source, c, u"vedit.denoise"_s);
                    copyEffect(source, c, u"vedit.voice_effect"_s);
                    copyEffect(source, c, u"vedit.eq"_s);
                    copyEffect(source, c, u"vedit.compressor"_s);
                }
            };
            text = tr("Apply audio to all");
        } else if (section == u"filter"_s) {
            change = [&source](Clip &c) { copyEffect(source, c, kFilterType); };
            text = tr("Apply filter to all");
        } else if (section == u"adjust"_s) {
            change = [&source](Clip &c) {
                copyEffect(source, c, kAdjustType);
                copyEffect(source, c, kGradeType);
                copyEffect(source, c, kLutType);
                copyEffect(source, c, kDeflickerType);
            };
            text = tr("Apply adjustments to all");
        } else if (section == u"grade"_s) {
            change = [&source](Clip &c) { copyEffect(source, c, kGradeType); };
            text = tr("Apply grade to all");
        } else if (section == u"lut"_s) {
            change = [&source](Clip &c) { copyEffect(source, c, kLutType); };
            text = tr("Apply LUT to all");
        } else if (section == u"deflicker"_s) {
            change = [&source](Clip &c) { copyEffect(source, c, kDeflickerType); };
            text = tr("Apply deflicker to all");
        } else if (section == u"text"_s) {
            change = [&source](Clip &c) {
                c.text()->style = source.text()->style;
                c.text()->stylePreset = source.text()->stylePreset;
            };
            text = tr("Apply text style to all");
        } else if (section == u"textAnimation"_s) {
            change = [&source](Clip &c) {
                c.text()->animation = source.text()->animation;
            };
            text = tr("Apply text animation to all");
        } else if (section == u"sticker"_s) {
            // The look (tint, and a visualizer's settings on the other visualizers), not the picture.
            change = [&source](Clip &c) {
                StickerClipData &sticker = *c.sticker();
                sticker.tint = source.sticker()->tint;
                if (sticker.visualizer && source.sticker()->visualizer) {
                    sticker.visualizer = source.sticker()->visualizer;
                }
            };
            text = tr("Apply sticker look to all");
        }
        if (!change) {
            return false;
        }
        done = update(clips, change, text, {});
        count = static_cast<int>(clips.size());
    }
    endGesture();
    if (done) {
        emit m_editor.message(tr("Applied to %n clip(s)", nullptr, count), true);
    }
    return done;
}

void ClipInspector::previewFilter(const QString &filterId)
{
    const Clip *clip = libraryClip();
    const fx::FilterPreset *preset = fx::Library::core().filter(filterId);
    if (!clip || !preset || !supports(*clip, u"filter"_s)) {
        return;
    }
    Clip previewed = *clip;
    Effect &filter = ensureEffect(previewed, kFilterType);
    filter.preset = coreAsset(preset->id, preset->version);
    engine::TimelineProjection::Preview preview;
    preview.clip = std::move(previewed);
    m_editor.player()->setPreview(std::move(preview));
}

bool ClipInspector::toggleFilter(const QString &filterId)
{
    m_editor.selectClipAtPlayhead(); // nothing selected: the clip on screen
    const Clip *clip = focus();
    if (!clip || !supports(*clip, u"filter"_s)) {
        emit m_editor.message(tr("Select a video or a photo first."), false);
        return false;
    }
    const std::vector<ClipId> clips = targets(u"filter"_s);
    const fx::FilterPreset *preset = fx::Library::core().filter(filterId);
    const Effect *current = findEffect(*clip, kFilterType);
    endGesture();
    bool done = false;
    if (!preset || (current && current->preset && current->preset->id == filterId)) {
        done = update(clips, [](Clip &c) { removeEffect(c, kFilterType); }, tr("Remove filter"), {});
    } else {
        const AssetRef ref = coreAsset(preset->id, preset->version);
        done = update(clips, [&ref](Clip &c) { ensureEffect(c, kFilterType).preset = ref; }, tr("Apply filter"), {});
    }
    clearPreview();
    return done;
}

QVariantMap ClipInspector::previewEffect(const QString &effectId)
{
    const Clip *clip = libraryClip();
    const fx::VideoEffectPreset *preset = fx::Library::core().videoEffect(effectId);
    if (!clip || !preset || !supports(*clip, u"effects"_s)) {
        return {};
    }
    Clip previewed = *clip;
    previewed.effects.push_back(effectOf(*preset));
    engine::TimelineProjection::Preview preview;
    preview.clip = std::move(previewed);
    m_editor.player()->setPreview(std::move(preview));
    const std::optional<fx::EffectKernel> kernel = fx::effectKernel(preset->kernel);
    if (preset->type == u"vedit.effect"_s && !(kernel && fx::isAnimatedKernel(*kernel))) {
        return {}; // a still effect shows on the current frame
    }
    // An animated one plays in a loop: two seconds from the playhead (or the clip's start), within the clip.
    const Rational rate = m_editor.data().settings.frameRate;
    const std::int64_t start = clip->start.rescaled(rate, Rounding::NearestEven).value();
    const std::int64_t end = clip->end().rescaled(rate, Rounding::NearestEven).value();
    const std::int64_t first = std::clamp<std::int64_t>(m_editor.player()->position(), start, std::max(start, end - 1));
    const std::int64_t last = std::min<std::int64_t>(end - 1, first + 2 * std::llround(rate.toDouble()));
    return {{u"start"_s, static_cast<int>(first)}, {u"end"_s, static_cast<int>(std::max(first, last))}};
}

bool ClipInspector::toggleEffect(const QString &effectId)
{
    m_editor.selectClipAtPlayhead(); // nothing selected: the clip on screen
    const Clip *clip = focus();
    clearPreview();
    const fx::VideoEffectPreset *preset = fx::Library::core().videoEffect(effectId);
    if (!clip || !preset || !supports(*clip, u"effects"_s)) {
        emit m_editor.message(tr("Select a video, a photo or a layer first."), false);
        return false;
    }
    const std::vector<ClipId> clips = targets(u"effects"_s);
    endGesture();
    const auto applied = [&effectId](const Effect &e) { return isLibraryEffect(e) && e.preset && e.preset->id == effectId; };
    if (std::any_of(clip->effects.begin(), clip->effects.end(), applied)) {
        return update(clips, [&applied](Clip &c) { std::erase_if(c.effects, applied); }, tr("Remove effect"), {});
    }
    const bool done = update(clips, [&preset](Clip &c) { c.effects.push_back(effectOf(*preset)); }, tr("Apply effect"), {});
    if (done && preset->type.startsWith(u"vedit.beat."_s)) {
        m_editor.ensureBeats();
    }
    return done;
}

bool ClipInspector::removeEffectAt(int index)
{
    const Clip *clip = focus();
    if (!clip) {
        return false;
    }
    const std::vector<const Effect *> effects = libraryEffects(*clip);
    if (index < 0 || index >= static_cast<int>(effects.size())) {
        return false;
    }
    const EffectId id = effects[static_cast<size_t>(index)]->id;
    endGesture();
    return update({clip->id}, [&id](Clip &c) { std::erase_if(c.effects, [&id](const Effect &e) { return e.id == id; }); },
                  tr("Remove effect"), {});
}

bool ClipInspector::setEffectParam(int index, const QString &name, const QVariant &value)
{
    const Clip *clip = focus();
    if (!clip) {
        return false;
    }
    const std::vector<const Effect *> effects = libraryEffects(*clip);
    if (index < 0 || index >= static_cast<int>(effects.size())) {
        return false;
    }
    const EffectId id = effects[static_cast<size_t>(index)]->id;
    const bool isColor = name == u"color"_s || name == u"color2"_s;
    return update({clip->id}, [&](Clip &c) {
        for (Effect &e : c.effects) {
            if (e.id != id) {
                continue;
            }
            if (name == u"mix"_s) {
                e.intensity = Param(std::clamp(value.toDouble(), 0.0, 1.0));
            } else if (isColor) {
                e.params[name] = Param(toColor(value));
            } else {
                e.params[name] = Param(value.toDouble());
            }
        }
    }, tr("Change effect"), u"effect:"_s + id.toString() + u':' + name);
}

void ClipInspector::previewTextStyle(const QString &styleId)
{
    const Clip *clip = focus();
    const fx::TextStylePreset *preset = fx::Library::core().textStyle(styleId);
    if (!clip || !clip->text() || !preset) {
        return;
    }
    Clip previewed = *clip;
    TextStyle style = projectjson::textStyleFromJson(preset->style);
    style.size = previewed.text()->style.size; // the size is layout, not look
    style.align = previewed.text()->style.align;
    previewed.text()->style = style;
    if (!preset->animation.isEmpty()) {
        previewed.text()->animation = projectjson::textAnimationFromJson(preset->animation);
    }
    engine::TimelineProjection::Preview preview;
    preview.clip = std::move(previewed);
    m_editor.player()->setPreview(std::move(preview));
}

bool ClipInspector::applyTextStyle(const QString &styleId)
{
    const fx::TextStylePreset *preset = fx::Library::core().textStyle(styleId);
    if (!preset) {
        return false;
    }
    const TextStyle look = projectjson::textStyleFromJson(preset->style);
    const AssetRef ref = coreAsset(preset->id, preset->version);
    const std::optional<TextAnimation> anim = !preset->animation.isEmpty()
                                                ? projectjson::textAnimationFromJson(preset->animation)
                                                : std::nullopt;
    endGesture();
    const bool done = update(targets(u"text"_s), [&look, &ref, &anim](Clip &c) {
        TextStyle style = look;
        style.size = c.text()->style.size;
        style.align = c.text()->style.align;
        c.text()->style = style;
        c.text()->stylePreset = ref;
        if (anim) {
            c.text()->animation = anim;
        }
    }, tr("Change text style"), {});
    clearPreview();
    return done;
}

std::optional<vedit::Transition> ClipInspector::plannedTransition(const QString &typeId, const Track **track) const
{
    const fx::TransitionPreset *preset = fx::Library::core().transition(typeId);
    const std::optional<ClipId> fromId = m_editor.transitionTarget();
    const Sequence *sequence = m_editor.data().mainSequence();
    if (!preset || !fromId || !sequence) {
        return std::nullopt;
    }
    for (const Track &candidate : sequence->visualTracks) {
        const int index = candidate.clipIndex(*fromId);
        if (index < 0 || index + 1 >= static_cast<int>(candidate.clips.size())) {
            continue;
        }
        const Clip &from = candidate.clips[static_cast<size_t>(index)];
        const Clip &to = candidate.clips[static_cast<size_t>(index) + 1];
        const Rational rate = m_editor.data().settings.frameRate;
        vedit::Transition transition;
        transition.id = TransitionId::create();
        transition.type = coreAsset(preset->id, preset->version);
        transition.from = from.id;
        transition.to = to.id;
        transition.duration = RationalTime(std::max<std::int64_t>(1, std::llround(preset->defaultSeconds * rate.toDouble())), rate);
        for (const vedit::Transition &existing : candidate.transitions) {
            if (existing.from == from.id && existing.to == to.id) {
                transition.id = existing.id;
                transition.duration = existing.duration; // another type keeps the length chosen
            }
        }
        transition.duration = std::clamp(transition.duration, RationalTime(1, rate), std::min(from.duration, to.duration));
        *track = &candidate;
        return transition;
    }
    return std::nullopt;
}

QVariantMap ClipInspector::previewTransition(const QString &typeId)
{
    const Track *track = nullptr;
    const std::optional<vedit::Transition> transition = plannedTransition(typeId, &track);
    if (!transition) {
        return {};
    }
    const Clip *to = track->findClip(transition->to);
    const std::int64_t cut = to->start.value();
    const std::int64_t length = transition->duration.value();
    engine::TimelineProjection::Preview preview;
    preview.transitionTrack = track->id;
    preview.transition = *transition;
    m_editor.player()->setPreview(std::move(preview));
    const std::int64_t start = std::max<std::int64_t>(0, cut - length / 2);
    return {{u"start"_s, static_cast<int>(start)}, {u"end"_s, static_cast<int>(start + length)}};
}

bool ClipInspector::toggleTransition(const QString &typeId)
{
    const Track *track = nullptr;
    const std::optional<vedit::Transition> planned = plannedTransition(typeId, &track);
    clearPreview();
    if (!planned) {
        emit m_editor.message(tr("Put two clips next to each other first: the transition goes between them."), false);
        return false;
    }
    endGesture();
    const vedit::Transition *existing = nullptr;
    for (const vedit::Transition &transition : track->transitions) {
        if (transition.id == planned->id) {
            existing = &transition;
        }
    }
    TimelineEditor editor(m_editor.data(), m_editor.data().mainSequenceId);
    if (existing && existing->type.id == typeId) {
        return m_editor.push(editor.removeTransitions({existing->id}));
    }
    const ClipId from = planned->from;
    const ClipId to = planned->to;
    const TrackId trackId = track->id;
    if (!m_editor.push(editor.addTransition(from, planned->type, planned->duration, planned->params))) {
        return false;
    }
    // The new transition becomes the selection: its duration and "Apply to all" are in the panel.
    if (const Track *updated = m_editor.data().findTrack(trackId)) {
        for (const vedit::Transition &transition : updated->transitions) {
            if (transition.from == from && transition.to == to) {
                m_editor.selectTransition(transition.id.toString());
            }
        }
    }
    return true;
}

bool ClipInspector::removeTransition()
{
    const vedit::Transition *transition = focusTransition();
    if (!transition) {
        return false;
    }
    endGesture();
    return m_editor.push(TimelineEditor(m_editor.data(), m_editor.data().mainSequenceId).removeTransitions({transition->id}));
}

const Track *ClipInspector::transitionTrack() const
{
    const Track *track = nullptr;
    if (focusTransition(&track)) {
        return track;
    }
    const Sequence *sequence = m_editor.data().mainSequence();
    if (const std::optional<ClipId> clip = m_editor.focusClip(); clip && sequence) {
        for (const Track &candidate : sequence->visualTracks) {
            if (candidate.findClip(*clip)) {
                return &candidate;
            }
        }
    }
    return sequence && !sequence->visualTracks.empty() ? &sequence->visualTracks.front() : nullptr;
}

bool ClipInspector::removeAllTransitions()
{
    const Track *track = transitionTrack();
    if (!track) {
        return false;
    }
    endGesture();
    return m_editor.push(TimelineEditor(m_editor.data(), m_editor.data().mainSequenceId).removeAllTransitions(track->id));
}

bool ClipInspector::randomTransitions()
{
    const Track *track = transitionTrack();
    const std::vector<fx::TransitionPreset> &presets = fx::Library::core().transitions();
    if (!track || presets.empty()) {
        return false;
    }
    std::vector<std::pair<ClipId, size_t>> cuts; // first clip of each cut, preset index
    for (size_t i = 0; i + 1 < track->clips.size(); ++i) {
        if (track->clips[i].end() == track->clips[i + 1].start) {
            cuts.emplace_back(track->clips[i].id, QRandomGenerator::global()->bounded(static_cast<quint32>(presets.size())));
        }
    }
    if (cuts.empty()) {
        emit m_editor.message(tr("There are no cuts between clips on this track."), false);
        return false;
    }
    endGesture();
    const QString target = u"random-transitions:"_s + track->id.toString();
    const Rational rate = m_editor.data().settings.frameRate;
    for (const auto &[from, index] : cuts) {
        const fx::TransitionPreset &preset = presets[index];
        EditResult result = TimelineEditor(m_editor.data(), m_editor.data().mainSequenceId)
                                .addTransition(from, coreAsset(preset.id, preset.version),
                                               RationalTime(std::max<std::int64_t>(1, std::llround(preset.defaultSeconds * rate.toDouble())), rate));
        result.text = tr("Random transitions");
        m_editor.push(std::move(result), gestureKey(target));
    }
    endGesture();
    emit m_editor.message(tr("Transition on %n cut(s)", nullptr, static_cast<int>(cuts.size())), true);
    return true;
}

QVariantMap ClipInspector::previewAnimation(const QString &animationId)
{
    const Clip *clip = libraryClip();
    const fx::AnimationPreset *preset = fx::Library::core().animation(animationId);
    if (!clip || !preset || !supports(*clip, u"animation"_s)) {
        return {};
    }
    const Rational rate = m_editor.data().settings.frameRate;
    const QString kind = animationKind(animationId);
    Clip previewed = *clip;
    std::optional<ClipAnimation> &slot = animationSlot(previewed.animations, kind);
    slot = animationOf(*preset, slot, rate);
    const std::int64_t start = clip->start.rescaled(rate, Rounding::NearestEven).value();
    const std::int64_t end = clip->end().rescaled(rate, Rounding::NearestEven).value();
    const std::int64_t length = std::min(end - start, slot->duration.rescaled(rate, Rounding::NearestEven).value());
    engine::TimelineProjection::Preview preview;
    preview.clip = std::move(previewed);
    m_editor.player()->setPreview(std::move(preview));
    // The frames that show it: the start or the end of the clip; a loop, one cycle (at least a second).
    const std::int64_t shown = kind == u"loop"_s ? std::min(end - start, std::max<std::int64_t>(length, std::llround(rate.toDouble())))
                                                 : length;
    const std::int64_t first = kind == u"out"_s ? end - shown : start;
    return {{u"start"_s, static_cast<int>(first)}, {u"end"_s, static_cast<int>(first + shown - 1)}};
}

bool ClipInspector::toggleAnimation(const QString &animationId)
{
    m_editor.selectClipAtPlayhead(); // nothing selected: the clip on screen
    const Clip *clip = focus();
    const fx::AnimationPreset *preset = fx::Library::core().animation(animationId);
    clearPreview();
    if (!clip || !preset || !supports(*clip, u"animation"_s)) {
        emit m_editor.message(tr("Select a video, a photo or a text first."), false);
        return false;
    }
    const QString kind = animationKind(animationId);
    const Rational rate = m_editor.data().settings.frameRate;
    const bool applied = [&] {
        ClipAnimations animations = clip->animations;
        const std::optional<ClipAnimation> &slot = animationSlot(animations, kind);
        return slot && slot->type.id == animationId;
    }();
    endGesture();
    return update(targets(u"animation"_s), [&](Clip &c) {
        std::optional<ClipAnimation> &slot = animationSlot(c.animations, kind);
        if (applied) {
            slot.reset();
        } else {
            slot = animationOf(*preset, slot, rate);
        }
    }, applied ? tr("Remove animation") : tr("Animate clip"), {});
}

void ClipInspector::clearPreview()
{
    m_editor.player()->clearPreview();
}

void ClipInspector::copyAttributes()
{
    const Clip *clip = focus();
    if (!clip) {
        return;
    }
    m_clipboard = *clip;
    emit clipboardChanged();
    emit m_editor.message(tr("Attributes copied"), false);
}

bool ClipInspector::pasteAttributes()
{
    if (!m_clipboard) {
        return false;
    }
    const Clip source = *m_clipboard;
    endGesture();
    const bool done = update(m_editor.selectedClips(), [this, &source](Clip &c) {
        if (supports(c, u"video"_s) && supports(source, u"video"_s)) {
            c.transform = source.transform;
            c.opacity = source.opacity;
            c.blendMode = source.blendMode;
        }
        if (supports(c, u"background"_s) && supports(source, u"background"_s)) {
            c.background = source.background;
        }
        if (supports(c, u"filter"_s) && supports(source, u"filter"_s)) {
            copyEffect(source, c, kFilterType);
            copyEffect(source, c, kAdjustType);
            copyEffect(source, c, kGradeType);
            copyEffect(source, c, kLutType);
            copyEffect(source, c, kDeflickerType);
        }
        if (c.media() && source.media()) {
            c.media()->audio = source.media()->audio;
            copyEffect(source, c, u"vedit.denoise"_s);
            copyEffect(source, c, u"vedit.voice_effect"_s);
            copyEffect(source, c, u"vedit.eq"_s);
            copyEffect(source, c, u"vedit.compressor"_s);
        }
        if (c.text() && source.text()) {
            c.text()->style = source.text()->style;
            c.text()->stylePreset = source.text()->stylePreset;
            c.text()->animation = source.text()->animation;
        }
    }, tr("Paste attributes"), {});
    endGesture();
    return done;
}

bool ClipInspector::autoEnhance()
{
    const Clip *clip = focus();
    const MediaClipData *media = clip ? clip->media() : nullptr;
    const Media *source = media ? m_editor.data().findMedia(media->mediaId) : nullptr;
    if (!source) {
        emit m_editor.message(tr("Select a video, a photo or a sound first."), false);
        return false;
    }
    // The part of the media the clip uses, in seconds of the source.
    const double from = media->sourceIn.toSecondsDouble();
    const double length = clip->duration.toSecondsDouble() * media->speed;
    const double mediaSeconds = source->info.duration ? source->info.duration->toSecondsDouble() : 0.0;

    std::optional<fx::ColorAdjust> look;
    if (supports(*clip, u"adjust"_s)) {
        const QImage strip = m_editor.analysis().thumbnails(*source);
        if (strip.isNull()) {
            emit m_editor.message(tr("The clip is still being analysed: try again in a moment."), false);
            return false;
        }
        const QImage rgba = strip.convertToFormat(QImage::Format_RGBA8888);
        const int count = std::max(1, engine::MediaAnalysis::thumbnailCount(*source));
        const int width = rgba.width() / count;
        std::vector<fx::ConstImageView> frames;
        for (int i = 0; i < count; ++i) {
            // Thumbnail i shows the media at (i + 0.5) / count of its duration.
            const double time = mediaSeconds > 0 ? (i + 0.5) * mediaSeconds / count : from;
            if (count > 1 && (time < from || time > from + length)) {
                continue;
            }
            frames.emplace_back(rgba.constBits() + static_cast<qsizetype>(i) * width * 4, width, rgba.height(),
                                static_cast<int>(rgba.bytesPerLine()));
        }
        if (frames.empty()) { // a clip shorter than the spacing of the thumbnails: the nearest one
            const int nearest = mediaSeconds > 0 ? std::clamp(static_cast<int>(from / mediaSeconds * count), 0, count - 1) : 0;
            frames.emplace_back(rgba.constBits() + static_cast<qsizetype>(nearest) * width * 4, width, rgba.height(),
                                static_cast<int>(rgba.bytesPerLine()));
        }
        look = fx::autoEnhance(frames);
    }
    std::optional<double> gainDb;
    if (supports(*clip, u"audio"_s)) {
        if (const std::shared_ptr<const engine::Waveform> waveform = m_editor.analysis().waveform(*source)) {
            const int first = std::max(0, static_cast<int>(from * waveform->bucketsPerSecond));
            const int last = std::min(waveform->bucketCount(), static_cast<int>((from + length) * waveform->bucketsPerSecond) + 1);
            int peak = 0;
            for (int i = first; i < last; ++i) {
                peak = std::max({peak, std::abs(int(waveform->peaks[2 * i])), std::abs(int(waveform->peaks[2 * i + 1]))});
            }
            gainDb = std::round(fx::autoGainDb(peak / 127.0) * 10.0) / 10.0;
        }
    }
    if (!look && !gainDb) {
        emit m_editor.message(tr("The clip is still being analysed: try again in a moment."), false);
        return false;
    }
    endGesture();
    const bool done = update({clip->id}, [&look, &gainDb](Clip &c) {
        if (look) {
            writeAdjust(ensureEffect(c, kAdjustType), *look);
        }
        if (gainDb) {
            c.media()->audio.gainDb = Param(*gainDb);
        }
    }, tr("Auto enhance"), {});
    endGesture();
    if (done) {
        emit m_editor.message(tr("Enhanced: light, colour and volume are in Adjust and Audio"), true);
    }
    return done;
}

bool ClipInspector::autoWhiteBalance()
{
    const Clip *clip = focus();
    const MediaClipData *media = clip ? clip->media() : nullptr;
    const Media *source = media ? m_editor.data().findMedia(media->mediaId) : nullptr;
    if (!clip || !source) {
        return false;
    }
    const QImage filmstrip = m_editor.analysis().thumbnails(*source);
    if (filmstrip.isNull()) {
        emit m_editor.message(tr("The clip is still being analysed: try again in a moment."), false);
        return false;
    }

    const double mediaSeconds = source->info.duration ? source->info.duration->toSecondsDouble() : 0.0;
    const double from = media->sourceIn.toSecondsDouble();
    const int count = m_editor.analysis().thumbnailCount(*source);
    const int width = filmstrip.width() / std::max(1, count);
    const int idx = mediaSeconds > 0 ? std::clamp(static_cast<int>(from / mediaSeconds * count), 0, count - 1) : 0;
    const QImage frame = filmstrip.copy(idx * width, 0, width, filmstrip.height());

    double sumR = 0, sumG = 0, sumB = 0;
    int sampled = 0;
    for (int y = 0; y < frame.height(); y += 2) {
        for (int x = 0; x < frame.width(); x += 2) {
            const QRgb px = frame.pixel(x, y);
            sumR += qRed(px);
            sumG += qGreen(px);
            sumB += qBlue(px);
            ++sampled;
        }
    }
    if (sampled == 0) {
        return false;
    }
    const double avgR = sumR / sampled / 255.0;
    const double avgG = sumG / sampled / 255.0;
    const double avgB = sumB / sampled / 255.0;
    const double grey = (avgR + avgG + avgB) / 3.0;
    double gainR = avgR > 0.01 ? grey / avgR : 1.0;
    double gainG = avgG > 0.01 ? grey / avgG : 1.0;
    double gainB = avgB > 0.01 ? grey / avgB : 1.0;

    if (gainG > 0.001) {
        gainR /= gainG;
        gainB /= gainG;
        gainG = 1.0;
    }

    endGesture();
    const bool done = update({clip->id}, [gainR, gainG, gainB](Clip &c) {
        Effect &grade = ensureEffect(c, kGradeType);
        grade.params[u"balance.r"_s] = Param(gainR);
        grade.params[u"balance.g"_s] = Param(gainG);
        grade.params[u"balance.b"_s] = Param(gainB);
    }, tr("Auto white balance"), {});
    endGesture();
    if (done) {
        emit m_editor.message(tr("White balance adjusted"), true);
    }
    return done;
}

bool ClipInspector::matchColor(const QString &referenceClipId)
{
    const Clip *clip = focus();
    if (!clip) {
        return false;
    }
    const Sequence *sequence = m_editor.data().mainSequence();
    if (!sequence) {
        return false;
    }
    const Clip *ref = nullptr;
    if (!referenceClipId.isEmpty()) {
        const std::optional<ClipId> targetId = ClipId::fromString(referenceClipId);
        if (targetId) {
            for (const Track &t : sequence->visualTracks) {
                if (const Clip *c = t.findClip(*targetId)) {
                    ref = c;
                    break;
                }
            }
        }
    } else {
        for (const Track &t : sequence->visualTracks) {
            for (const Clip &c : t.clips) {
                if (c.id != clip->id && c.start < clip->start) {
                    ref = &c;
                }
            }
        }
        if (!ref) {
            for (const Track &t : sequence->visualTracks) {
                for (const Clip &c : t.clips) {
                    if (c.id != clip->id) {
                        ref = &c;
                        break;
                    }
                }
            }
        }
    }
    if (!ref) {
        emit m_editor.message(tr("No reference clip found to match"), false);
        return false;
    }

    const MediaClipData *curMedia = clip->media();
    const MediaClipData *refMedia = ref->media();
    const Media *curSource = curMedia ? m_editor.data().findMedia(curMedia->mediaId) : nullptr;
    const Media *refSource = refMedia ? m_editor.data().findMedia(refMedia->mediaId) : nullptr;
    if (!curSource || !refSource) {
        return false;
    }

    const QImage curThumb = m_editor.analysis().thumbnails(*curSource);
    const QImage refThumb = m_editor.analysis().thumbnails(*refSource);
    if (curThumb.isNull() || refThumb.isNull()) {
        emit m_editor.message(tr("Clips are still being analysed: try again in a moment."), false);
        return false;
    }

    const auto computeMeanRgb = [](const QImage &img) {
        double r = 0, g = 0, b = 0;
        int count = 0;
        for (int y = 0; y < img.height(); y += 2) {
            for (int x = 0; x < img.width(); x += 2) {
                const QRgb px = img.pixel(x, y);
                r += qRed(px);
                g += qGreen(px);
                b += qBlue(px);
                ++count;
            }
        }
        if (count == 0) return std::array<double, 3>{0.5, 0.5, 0.5};
        return std::array<double, 3>{r / count / 255.0, g / count / 255.0, b / count / 255.0};
    };

    const auto curMean = computeMeanRgb(curThumb);
    const auto refMean = computeMeanRgb(refThumb);

    const double gainR = curMean[0] > 0.01 ? std::clamp(refMean[0] / curMean[0], 0.2, 5.0) : 1.0;
    const double gainG = curMean[1] > 0.01 ? std::clamp(refMean[1] / curMean[1], 0.2, 5.0) : 1.0;
    const double gainB = curMean[2] > 0.01 ? std::clamp(refMean[2] / curMean[2], 0.2, 5.0) : 1.0;

    endGesture();
    const bool done = update({clip->id}, [gainR, gainG, gainB](Clip &c) {
        Effect &grade = ensureEffect(c, kGradeType);
        grade.params[u"balance.r"_s] = Param(gainR);
        grade.params[u"balance.g"_s] = Param(gainG);
        grade.params[u"balance.b"_s] = Param(gainB);
    }, tr("Match colour"), {});
    endGesture();
    if (done) {
        emit m_editor.message(tr("Color matched to reference clip"), true);
    }
    return done;
}

bool ClipInspector::normalizeLoudness(double targetLufs)
{
    const Clip *clip = focus();
    if (!clip || !clip->media()) {
        emit m_editor.message(tr("Select an audio or video clip first."), false);
        return false;
    }
    const Media *source = m_editor.data().findMedia(clip->media()->mediaId);
    if (!source || !source->info.audio) {
        emit m_editor.message(tr("The selected clip does not have audio."), false);
        return false;
    }
    const auto loudnessOpt = engine::extractLoudness(source->path);
    if (!loudnessOpt) {
        emit m_editor.message(tr("Could not measure loudness of the audio."), false);
        return false;
    }
    const double currentLufs = loudnessOpt->integratedLufs;
    const double deltaDb = fx::gainAdjustmentForTargetLufs(currentLufs, targetLufs);
    const double currentGainDb = numberOf(clip->media()->audio.gainDb, 0.0);
    const double newGainDb = std::clamp(currentGainDb + deltaDb, -60.0, 20.0);
    const bool done = set(u"volume"_s, newGainDb);
    if (done) {
        emit m_editor.message(tr("Loudness normalized to %1 LUFS (%2 dB)").arg(targetLufs, 0, 'f', 1).arg(deltaDb >= 0 ? QStringLiteral("+") + QString::number(deltaDb, 'f', 1) : QString::number(deltaDb, 'f', 1)), true);
    }
    return done;
}

bool ClipInspector::autoDuck(double duckingDb)
{
    const Clip *clip = focus();
    if (!clip || !clip->media()) {
        emit m_editor.message(tr("Select an audio clip to duck."), false);
        return false;
    }
    const Sequence *seq = m_editor.data().mainSequence();
    if (!seq) {
        return false;
    }
    const TimeRange targetRange = clip->range();
    if (targetRange.duration.value() <= 0) {
        return false;
    }

    std::vector<TimeRange> busyIntervals;
    const auto checkTrack = [&](const Track &track) {
        for (const Clip &c : track.clips) {
            if (c.id == clip->id || !c.media() || c.media()->audio.muted) {
                continue;
            }
            const Media *src = m_editor.data().findMedia(c.media()->mediaId);
            if (!src || !src->info.audio || c.media()->streams == Streams::VideoOnly) {
                continue;
            }
            const TimeRange clipRange = c.range();
            if (targetRange.intersects(clipRange)) {
                const RationalTime oStart = std::max(targetRange.start, clipRange.start);
                const RationalTime oEnd = std::min(targetRange.end(), clipRange.end());
                if (oEnd > oStart) {
                    busyIntervals.push_back(TimeRange(oStart, oEnd - oStart));
                }
            }
        }
    };
    for (const Track &t : seq->visualTracks) {
        checkTrack(t);
    }
    for (const Track &t : seq->audioTracks) {
        checkTrack(t);
    }

    if (busyIntervals.empty()) {
        emit m_editor.message(tr("No overlapping audio found to duck against."), false);
        return false;
    }

    std::sort(busyIntervals.begin(), busyIntervals.end(), [](const TimeRange &a, const TimeRange &b) {
        return a.start < b.start;
    });

    const Rational rate = m_editor.data().settings.frameRate;
    const RationalTime gapThreshold(std::max<int64_t>(1, std::llround(0.5 * rate.toDouble())), rate);
    std::vector<TimeRange> merged;
    for (const TimeRange &r : busyIntervals) {
        if (merged.empty()) {
            merged.push_back(r);
        } else if (r.start <= merged.back().end() + gapThreshold) {
            merged.back().duration = std::max(merged.back().end(), r.end()) - merged.back().start;
        } else {
            merged.push_back(r);
        }
    }

    const RationalTime attack(std::max<int64_t>(1, std::llround(0.2 * rate.toDouble())), rate);
    const RationalTime release(std::max<int64_t>(1, std::llround(0.4 * rate.toDouble())), rate);
    const double normalGain = 0.0;
    const double duckGain = std::clamp(duckingDb, -60.0, 0.0);

    std::vector<Keyframe> keyframes;
    for (const TimeRange &m : merged) {
        const RationalTime relStart = m.start - targetRange.start;
        const RationalTime relEnd = m.end() - targetRange.start;

        const RationalTime t0 = std::max(RationalTime(0, rate), relStart - attack);
        const RationalTime t1 = relStart;
        const RationalTime t2 = relEnd;
        const RationalTime t3 = std::min(targetRange.duration, relEnd + release);

        keyframes.push_back(Keyframe{t0, ParamValue(normalGain), Interpolation::Linear, {}});
        keyframes.push_back(Keyframe{t1, ParamValue(duckGain), Interpolation::Linear, {}});
        keyframes.push_back(Keyframe{t2, ParamValue(duckGain), Interpolation::Linear, {}});
        keyframes.push_back(Keyframe{t3, ParamValue(normalGain), Interpolation::Linear, {}});
    }

    std::sort(keyframes.begin(), keyframes.end(), [](const Keyframe &a, const Keyframe &b) {
        return a.time < b.time;
    });
    std::vector<Keyframe> uniqueKf;
    for (const auto &kf : keyframes) {
        if (uniqueKf.empty() || uniqueKf.back().time != kf.time) {
            uniqueKf.push_back(kf);
        }
    }

    Param param;
    for (const auto &kf : uniqueKf) {
        setKeyframeValue(param, kf.time, kf.value);
    }

    endGesture();
    const bool done = update({clip->id}, [param](Clip &c) {
        if (c.media()) {
            c.media()->audio.gainDb = param;
        }
    }, tr("Auto-duck audio"), {});
    endGesture();
    if (done) {
        emit m_editor.message(tr("Auto-ducking applied"), true);
    }
    return done;
}

} // namespace vedit::ui
