// SPDX-License-Identifier: GPL-3.0-or-later
#include "ClipInspector.h"

#include "EditorController.h"
#include "core/edit/TimelineEditor.h"
#include "core/project/ClipTime.h"
#include "core/serialization/ProjectJson.h"
#include "engine/analysis/MediaAnalysis.h"
#include "engine/playback/TimelinePlayer.h"
#include "engine/timeline/ClipPlacement.h"
#include "fx/Enhance.h"
#include "fx/Library.h"

#include <QColor>
#include <QRandomGenerator>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace vedit::ui {

namespace {

const QString kFilterType = u"vedit.filter"_s;
const QString kAdjustType = u"vedit.adjust.basic"_s;

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

// The parameters that have keyframes in the interface (the renderer animates them: vedit.transform).
const QStringList kKeyframeKeys{u"position"_s, u"scale"_s, u"rotation"_s, u"opacity"_s};

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
    return keyframe.easing.name();
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
    const bool animated = std::any_of(kKeyframeKeys.begin(), kKeyframeKeys.end(),
                                      [clip](const QString &key) { return keyframeParam(*clip, key)->isAnimated(); });
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
        for (const Keyframe &keyframe : keyframeParam(*clip, key)->keyframes()) {
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
    if (!time || !kKeyframeKeys.contains(key)) {
        return false;
    }
    const Param &current = *keyframeParam(*clip, key);
    const bool here = std::any_of(current.keyframes().begin(), current.keyframes().end(),
                                  [&time](const Keyframe &k) { return k.time == *time; });
    endGesture();
    return update({clip->id}, [&](Clip &c) {
        Param &param = *keyframeParam(c, key);
        const ParamValue value = param.valueAt(*time);
        if (!here) {
            setKeyframeValue(param, *time, value);
            return;
        }
        std::vector<Keyframe> keyframes = param.keyframes();
        std::erase_if(keyframes, [&time](const Keyframe &k) { return k.time == *time; });
        if (keyframes.empty()) {
            param.setKeyframes({});
            param.setStaticValue(value); // the last one leaves its value
        } else {
            param.setKeyframes(std::move(keyframes));
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
            Param &param = *keyframeParam(c, key);
            std::vector<Keyframe> keyframes = param.keyframes();
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
            param.setKeyframes(std::move(keyframes));
        }
    }, tr("Change keyframe easing"), {});
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

int ClipInspector::kind() const
{
    const Clip *clip = focus();
    if (!clip) {
        return focusTransition() ? Transition : None;
    }
    if (clip->text()) {
        return Text;
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
    const bool visualMedia = media && !audioOnly;
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
    if (section == u"filter"_s || section == u"adjust"_s) {
        return visualMedia;
    }
    if (section == u"text"_s) {
        return clip.text() != nullptr;
    }
    if (section == u"animation"_s) {
        return !audioOnly;
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
    for (const QString &section : {u"text"_s, u"video"_s, u"background"_s, u"audio"_s, u"speed"_s, u"animation"_s, u"filter"_s,
                                   u"adjust"_s}) {
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
        if (media->speed != 1.0 || media->reversed || !media->preservePitch) {
            result << u"speed"_s;
        }
    }
    if (!clip->animations.isEmpty()) {
        result << u"animation"_s;
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
    if (const TextClipData *text = clip->text(); text && text->style != defaultTextStyle()) {
        result << u"text"_s;
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
    for (const QString &key : kKeyframeKeys) {
        const Param &param = *keyframeParam(*clip, key);
        int state = param.isAnimated() ? 1 : 0;
        for (const Keyframe &keyframe : param.keyframes()) {
            if (keyTime && keyframe.time == *keyTime) {
                state = 2;
                if (easing.isEmpty()) {
                    easing = easingName(keyframe);
                }
            }
        }
        map[u"kf."_s + key] = state;
    }
    map[u"kf.easing"_s] = easing;
    map[u"flipH"_s] = clip->transform.flipH;
    map[u"flipV"_s] = clip->transform.flipV;
    map[u"fit"_s] = clip->transform.fit == FitMode::Cover ? 1 : 0;

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
    }
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
        map[u"text.letterSpacing"_s] = numberOf(style.letterSpacing, 0.0);
        map[u"text.lineHeight"_s] = style.lineHeight;
        map[u"text.preset"_s] = text->stylePreset ? text->stylePreset->id : QString();
    }
    return map;
}

QString ClipInspector::sectionOf(const QString &key) const
{
    static const QStringList video{u"x"_s, u"y"_s, u"scale"_s, u"rotation"_s, u"opacity"_s, u"flipH"_s, u"flipV"_s, u"fit"_s};
    static const QStringList audio{u"volume"_s, u"fadeIn"_s, u"fadeOut"_s};
    static const QStringList speed{u"speed"_s, u"reversed"_s, u"preservePitch"_s};
    if (video.contains(key)) {
        return u"video"_s;
    }
    if (audio.contains(key)) {
        return u"audio"_s;
    }
    if (speed.contains(key)) {
        return u"speed"_s;
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
        return update(clips, [&](Clip &c) { c.media()->audio.gainDb = Param(std::clamp(number, -60.0, 20.0)); },
                      tr("Change volume"), mergeTarget);
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
    } else if (section == u"audio"_s) {
        done = update(clips, [](Clip &c) { c.media()->audio = ClipAudio{}; }, tr("Reset volume"), {});
    } else if (section == u"speed"_s) {
        // One undo step: the direction, then the speed (which moves the following clips).
        const QString target = u"reset-speed:"_s + clip->id.toString();
        done = update({clip->id}, [](Clip &c) {
            c.media()->reversed = false;
            c.media()->preservePitch = true;
        }, tr("Reset speed"), target);
        EditResult speed = TimelineEditor(m_editor.data(), m_editor.data().mainSequenceId).setSpeed(clip->id, 1.0);
        speed.primaryClip = {};
        speed.text = tr("Reset speed");
        done = m_editor.push(std::move(speed), gestureKey(target)) || done;
    } else if (section == u"animation"_s) {
        done = update(clips, [](Clip &c) { c.animations = ClipAnimations{}; }, tr("Remove animations"), {});
    } else if (section == u"filter"_s) {
        done = update(clips, [](Clip &c) { removeEffect(c, kFilterType); }, tr("Remove filter"), {});
    } else if (section == u"adjust"_s) {
        done = update(clips, [](Clip &c) { removeEffect(c, kAdjustType); }, tr("Reset adjustments"), {});
    } else if (section == u"text"_s) {
        done = update(clips, [](Clip &c) {
            c.text()->style = defaultTextStyle();
            c.text()->stylePreset.reset();
        }, tr("Reset text style"), {});
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
            change = [&source](Clip &c) { c.media()->audio = source.media()->audio; };
            text = tr("Apply volume to all");
        } else if (section == u"filter"_s) {
            change = [&source](Clip &c) { copyEffect(source, c, kFilterType); };
            text = tr("Apply filter to all");
        } else if (section == u"adjust"_s) {
            change = [&source](Clip &c) { copyEffect(source, c, kAdjustType); };
            text = tr("Apply adjustments to all");
        } else if (section == u"text"_s) {
            change = [&source](Clip &c) {
                c.text()->style = source.text()->style;
                c.text()->stylePreset = source.text()->stylePreset;
            };
            text = tr("Apply text style to all");
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
    endGesture();
    const bool done = update(targets(u"text"_s), [&look, &ref](Clip &c) {
        TextStyle style = look;
        style.size = c.text()->style.size;
        style.align = c.text()->style.align;
        c.text()->style = style;
        c.text()->stylePreset = ref;
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
        }
        if (c.media() && source.media()) {
            c.media()->audio = source.media()->audio;
        }
        if (c.text() && source.text()) {
            c.text()->style = source.text()->style;
            c.text()->stylePreset = source.text()->stylePreset;
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

} // namespace vedit::ui
