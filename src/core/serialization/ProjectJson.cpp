// SPDX-License-Identifier: GPL-3.0-or-later
#include "ProjectJson.h"

#include "core/serialization/Migrations.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSet>

#include <cmath>

using namespace Qt::StringLiterals;

namespace velacut::projectjson {

namespace {

// ---------------------------------------------------------------- enum tables

template<typename E>
struct EnumName
{
    E value;
    QLatin1StringView name;
};

constexpr EnumName<MediaKind> kMediaKinds[] = {{MediaKind::Video, "video"_L1},
                                               {MediaKind::Audio, "audio"_L1},
                                               {MediaKind::Image, "image"_L1},
                                               {MediaKind::ImageSequence, "imageSequence"_L1}};
constexpr EnumName<ProxyPolicy> kProxyPolicies[] = {{ProxyPolicy::Auto, "auto"_L1}, {ProxyPolicy::Off, "off"_L1}};
constexpr EnumName<CanvasPreset> kCanvasPresets[] = {
    {CanvasPreset::Landscape16x9, "16:9"_L1}, {CanvasPreset::Portrait9x16, "9:16"_L1},
    {CanvasPreset::Square1x1, "1:1"_L1},      {CanvasPreset::Portrait4x5, "4:5"_L1},
    {CanvasPreset::Cinema21x9, "21:9"_L1},    {CanvasPreset::Portrait3x4, "3:4"_L1},
    {CanvasPreset::Custom, "custom"_L1}};
constexpr EnumName<TrackKind> kTrackKinds[] = {
    {TrackKind::Video, "video"_L1},           {TrackKind::Text, "text"_L1},   {TrackKind::Sticker, "sticker"_L1},
    {TrackKind::Effect, "effect"_L1},         {TrackKind::Adjustment, "adjustment"_L1},
    {TrackKind::Audio, "audio"_L1}};
constexpr EnumName<ClipKind> kClipKinds[] = {
    {ClipKind::Media, "media"_L1},   {ClipKind::Text, "text"_L1},     {ClipKind::Subtitle, "subtitle"_L1},
    {ClipKind::Sticker, "sticker"_L1}, {ClipKind::Color, "color"_L1}, {ClipKind::Effect, "effect"_L1},
    {ClipKind::Adjustment, "adjustment"_L1}, {ClipKind::Compound, "compound"_L1}};
constexpr EnumName<BlendMode> kBlendModes[] = {
    {BlendMode::Normal, "normal"_L1},         {BlendMode::Lighten, "lighten"_L1},
    {BlendMode::Screen, "screen"_L1},         {BlendMode::Multiply, "multiply"_L1},
    {BlendMode::Overlay, "overlay"_L1},       {BlendMode::SoftLight, "softLight"_L1},
    {BlendMode::HardLight, "hardLight"_L1},   {BlendMode::Difference, "difference"_L1},
    {BlendMode::Darken, "darken"_L1},         {BlendMode::Color, "color"_L1},
    {BlendMode::Luminosity, "luminosity"_L1}, {BlendMode::Add, "add"_L1},
    {BlendMode::ColorDodge, "colorDodge"_L1}, {BlendMode::ColorBurn, "colorBurn"_L1},
    {BlendMode::Exclusion, "exclusion"_L1},   {BlendMode::Hue, "hue"_L1},
    {BlendMode::Saturation, "saturation"_L1}};
constexpr EnumName<FitMode> kFitModes[] = {{FitMode::Contain, "contain"_L1},
                                           {FitMode::Cover, "cover"_L1},
                                           {FitMode::Stretch, "stretch"_L1},
                                           {FitMode::None, "none"_L1}};
constexpr EnumName<MaskShape> kMaskShapes[] = {
    {MaskShape::Linear, "linear"_L1},       {MaskShape::Mirror, "mirror"_L1},
    {MaskShape::Circle, "circle"_L1},       {MaskShape::Rectangle, "rectangle"_L1},
    {MaskShape::Heart, "heart"_L1},         {MaskShape::Star, "star"_L1},
    {MaskShape::Path, "path"_L1}};
constexpr EnumName<Streams> kStreams[] = {
    {Streams::AudioVideo, "av"_L1}, {Streams::VideoOnly, "video"_L1}, {Streams::AudioOnly, "audio"_L1}};
constexpr EnumName<Interpolation> kInterpolations[] = {{Interpolation::Linear, "linear"_L1},
                                                       {Interpolation::Hold, "hold"_L1},
                                                       {Interpolation::Bezier, "bezier"_L1}};
constexpr EnumName<MarkerKind> kMarkerKinds[] = {
    {MarkerKind::User, "user"_L1}, {MarkerKind::Beat, "beat"_L1}, {MarkerKind::Chapter, "chapter"_L1}};
constexpr EnumName<TransitionAlignment> kAlignments[] = {{TransitionAlignment::Center, "center"_L1},
                                                         {TransitionAlignment::Overlap, "overlap"_L1}};
constexpr EnumName<MissingMaterial> kMissingMaterial[] = {{MissingMaterial::Freeze, "freeze"_L1},
                                                          {MissingMaterial::None, "none"_L1}};
constexpr EnumName<BackgroundType> kBackgroundTypes[] = {{BackgroundType::Color, "color"_L1},
                                                         {BackgroundType::Blur, "blur"_L1},
                                                         {BackgroundType::Image, "image"_L1},
                                                         {BackgroundType::Pattern, "pattern"_L1}};
constexpr EnumName<VisualizerStyle> kVisualizerStyles[] = {
    {VisualizerStyle::Bars, "bars"_L1},
    {VisualizerStyle::Spectrum, "spectrum"_L1},
    {VisualizerStyle::Waveform, "waveform"_L1},
    {VisualizerStyle::PulsingCircle, "circle"_L1}};
constexpr EnumName<GraphicKind> kGraphicKinds[] = {
    {GraphicKind::Counter, "counter"_L1},         {GraphicKind::Countdown, "countdown"_L1},
    {GraphicKind::Timer, "timer"_L1},             {GraphicKind::ProgressBar, "progressBar"_L1},
    {GraphicKind::Arrow, "arrow"_L1},             {GraphicKind::Circle, "circle"_L1},
    {GraphicKind::Underline, "underline"_L1},     {GraphicKind::Highlighter, "highlighter"_L1},
    {GraphicKind::Check, "check"_L1},             {GraphicKind::Cross, "cross"_L1}};
constexpr EnumName<PlaceholderKind> kPlaceholderKinds[] = {
    {PlaceholderKind::Any, "any"_L1}, {PlaceholderKind::Video, "video"_L1}, {PlaceholderKind::Photo, "photo"_L1}};
constexpr EnumName<TextAlign> kTextAligns[] = {
    {TextAlign::Left, "left"_L1}, {TextAlign::Center, "center"_L1}, {TextAlign::Right, "right"_L1}};
constexpr EnumName<BubbleShape> kBubbleShapes[] = {
    {BubbleShape::Rectangle, "rectangle"_L1},
    {BubbleShape::SpeechRound, "speechRound"_L1},
    {BubbleShape::SpeechSquare, "speechSquare"_L1},
    {BubbleShape::ThoughtCloud, "thoughtCloud"_L1},
    {BubbleShape::ComicShout, "comicShout"_L1},
    {BubbleShape::Callout, "callout"_L1},
    {BubbleShape::LowerThirdBar, "lowerThirdBar"_L1},
    {BubbleShape::LowerThirdTwoTone, "lowerThirdTwoTone"_L1},
    {BubbleShape::Badge, "badge"_L1}};
constexpr EnumName<BubbleTail> kBubbleTails[] = {
    {BubbleTail::None, "none"_L1},
    {BubbleTail::BottomLeft, "bottomLeft"_L1},
    {BubbleTail::BottomCenter, "bottomCenter"_L1},
    {BubbleTail::BottomRight, "bottomRight"_L1},
    {BubbleTail::TopLeft, "topLeft"_L1},
    {BubbleTail::TopRight, "topRight"_L1},
    {BubbleTail::Left, "left"_L1},
    {BubbleTail::Right, "right"_L1}};
constexpr EnumName<TextAnimationType> kTextAnimationTypes[] = {
    {TextAnimationType::None, "none"_L1},
    {TextAnimationType::Typewriter, "typewriter"_L1},
    {TextAnimationType::FadeIn, "fadeIn"_L1},
    {TextAnimationType::SlideUp, "slideUp"_L1},
    {TextAnimationType::SlideDown, "slideDown"_L1},
    {TextAnimationType::Bounce, "bounce"_L1},
    {TextAnimationType::PopIn, "popIn"_L1},
    {TextAnimationType::Wave, "wave"_L1},
    {TextAnimationType::Glitch, "glitch"_L1},
    {TextAnimationType::Blur, "blur"_L1}};
constexpr EnumName<TextAnimationScope> kTextAnimationScopes[] = {
    {TextAnimationScope::Character, "character"_L1},
    {TextAnimationScope::Word, "word"_L1},
    {TextAnimationScope::Line, "line"_L1},
    {TextAnimationScope::All, "all"_L1}};
constexpr EnumName<CaptionHighlight> kCaptionHighlights[] = {
    {CaptionHighlight::None, "none"_L1}, {CaptionHighlight::Color, "color"_L1}, {CaptionHighlight::Scale, "scale"_L1},
    {CaptionHighlight::Box, "box"_L1},   {CaptionHighlight::Karaoke, "karaoke"_L1}};
constexpr EnumName<CaptionAnimation> kCaptionAnimations[] = {{CaptionAnimation::None, "none"_L1},
                                                             {CaptionAnimation::Pop, "pop"_L1},
                                                             {CaptionAnimation::Fade, "fade"_L1},
                                                             {CaptionAnimation::Bounce, "bounce"_L1}};

template<typename E, size_t N>
QString nameOf(const EnumName<E> (&table)[N], E value)
{
    for (const auto &entry : table) {
        if (entry.value == value) {
            return entry.name;
        }
    }
    return table[0].name;
}

template<typename E, size_t N>
std::optional<E> valueOf(const EnumName<E> (&table)[N], QStringView name)
{
    for (const auto &entry : table) {
        if (name == entry.name) {
            return entry.value;
        }
    }
    return std::nullopt;
}

// Keys interpreted by this version; everything else is preserved in `extras`.
const QSet<QString> kProjectKeys{u"format"_s,     u"formatVersion"_s, u"generator"_s,   u"id"_s,
                                 u"name"_s,       u"createdAt"_s,     u"modifiedAt"_s,  u"settings"_s,
                                 u"mediaFolders"_s, u"media"_s,       u"sequences"_s,   u"mainSequenceId"_s};
const QSet<QString> kSequenceKeys{u"id"_s,           u"name"_s,        u"canvas"_s,  u"magneticMain"_s,
                                  u"visualTracks"_s, u"audioTracks"_s, u"markers"_s, u"groups"_s,
                                  u"defaultBackground"_s};
const QSet<QString> kTrackKeys{u"id"_s,     u"kind"_s,   u"name"_s,     u"locked"_s, u"muted"_s,      u"solo"_s,
                               u"hidden"_s, u"height"_s, u"captions"_s, u"clips"_s,  u"transitions"_s, u"gainDb"_s};
const QSet<QString> kClipCommonKeys{u"id"_s,        u"kind"_s,        u"start"_s,      u"duration"_s,
                                    u"name"_s,      u"enabled"_s,     u"linkId"_s,     u"transform"_s,
                                    u"opacity"_s,   u"blendMode"_s,   u"effects"_s,    u"masks"_s,
                                    u"animations"_s, u"markers"_s,    u"background"_s, u"placeholder"_s};
const QSet<QString> kTextClipKeys{u"text"_s, u"style"_s, u"stylePreset"_s, u"box"_s, u"animation"_s};
const QSet<QString> kSubtitleClipKeys{u"text"_s, u"words"_s, u"styleOverride"_s, u"translation"_s};

const QSet<QString> kTextStyleKeys{u"font"_s,          u"size"_s,       u"color"_s, u"stroke"_s,   u"shadow"_s,
                                   u"background"_s,    u"letterSpacing"_s, u"lineHeight"_s, u"align"_s,
                                   u"underline"_s};
const QSet<QString> kMediaClipKeys{u"mediaId"_s,       u"streams"_s,  u"sourceIn"_s, u"speed"_s,
                                   u"preservePitch"_s, u"reversed"_s, u"smooth"_s,  u"cutout"_s, u"audio"_s};
const QSet<QString> kColorClipKeys{u"color"_s};
const QSet<QString> kCompoundClipKeys{u"sequenceId"_s, u"sourceIn"_s, u"activeAngle"_s};
const QSet<QString> kStickerClipKeys{u"source"_s, u"mediaId"_s, u"emoji"_s, u"loop"_s, u"speed"_s, u"tint"_s, u"visualizer"_s,
                                     u"graphic"_s};

QJsonObject unknownKeys(const QJsonObject &object, const QSet<QString> &known,
                        const QSet<QString> &alsoKnown = {})
{
    QJsonObject extras;
    for (auto it = object.begin(); it != object.end(); ++it) {
        if (!known.contains(it.key()) && !alsoKnown.contains(it.key())) {
            extras.insert(it.key(), it.value());
        }
    }
    return extras;
}

void mergeInto(QJsonObject &target, const QJsonObject &source)
{
    for (auto it = source.begin(); it != source.end(); ++it) {
        if (!target.contains(it.key())) {
            target.insert(it.key(), it.value());
        }
    }
}

// ---------------------------------------------------------------- writing

QJsonValue timeValue(const RationalTime &time)
{
    return time.toString();
}

QJsonValue optionalTime(const std::optional<RationalTime> &time)
{
    return time ? QJsonValue(time->toString()) : QJsonValue(QJsonValue::Null);
}

template<typename Tag>
QJsonValue idValue(const Id<Tag> &id)
{
    return id.isNull() ? QJsonValue(QJsonValue::Null) : QJsonValue(id.toString());
}

QJsonValue nullableString(const QString &text)
{
    return text.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(text);
}

QJsonValue paramValueJson(const ParamValue &value)
{
    return std::visit(
        [](const auto &v) -> QJsonValue {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, double> || std::is_same_v<T, bool> || std::is_same_v<T, QString>) {
                return QJsonValue(v);
            } else if constexpr (std::is_same_v<T, Color>) {
                return v.toString();
            } else if constexpr (std::is_same_v<T, Vec2>) {
                return QJsonArray{v.x, v.y};
            } else {
                return v;
            }
        },
        value);
}

QJsonValue paramJson(const Param &param)
{
    if (!param.isAnimated()) {
        return paramValueJson(param.staticValue());
    }
    QJsonArray keyframes;
    for (const Keyframe &keyframe : param.keyframes()) {
        QJsonObject object{{u"t"_s, timeValue(keyframe.time)},
                           {u"v"_s, paramValueJson(keyframe.value)},
                           {u"interp"_s, nameOf(kInterpolations, keyframe.interpolation)}};
        if (keyframe.interpolation == Interpolation::Bezier) {
            if (keyframe.easing.presetKind() == Easing::Preset::Custom) {
                const auto &b = keyframe.easing.bezier();
                object.insert(u"ease"_s, QJsonArray{b[0], b[1], b[2], b[3]});
            } else {
                object.insert(u"ease"_s, keyframe.easing.name());
            }
        }
        keyframes.append(object);
    }
    return QJsonObject{{u"keyframes"_s, keyframes}};
}

QJsonObject paramMapJson(const std::map<QString, Param> &params)
{
    QJsonObject object;
    for (const auto &[name, param] : params) {
        object.insert(name, paramJson(param));
    }
    return object;
}

QJsonObject assetRefJson(const AssetRef &ref)
{
    return {{u"pack"_s, ref.pack}, {u"id"_s, ref.id}, {u"version"_s, ref.version}};
}

QJsonObject canvasJson(const Canvas &canvas)
{
    return {{u"width"_s, canvas.width}, {u"height"_s, canvas.height}, {u"preset"_s, nameOf(kCanvasPresets, canvas.preset)}};
}

QJsonObject effectJson(const Effect &effect)
{
    return {{u"id"_s, idValue(effect.id)},
            {u"type"_s, effect.type},
            {u"typeVersion"_s, effect.typeVersion},
            {u"enabled"_s, effect.enabled},
            {u"preset"_s, effect.preset ? QJsonValue(assetRefJson(*effect.preset)) : QJsonValue(QJsonValue::Null)},
            {u"intensity"_s, paramJson(effect.intensity)},
            {u"params"_s, paramMapJson(effect.params)}};
}

QJsonObject markerJson(const Marker &marker)
{
    return {{u"id"_s, idValue(marker.id)},
            {u"t"_s, timeValue(marker.time)},
            {u"duration"_s, optionalTime(marker.duration)},
            {u"name"_s, marker.name},
            {u"color"_s, marker.color},
            {u"note"_s, marker.note},
            {u"kind"_s, nameOf(kMarkerKinds, marker.kind)}};
}

template<typename T, typename F>
QJsonArray arrayOf(const std::vector<T> &items, F &&toJson)
{
    QJsonArray array;
    for (const T &item : items) {
        array.append(toJson(item));
    }
    return array;
}

QJsonObject transformJson(const Transform &t)
{
    return {{u"position"_s, paramJson(t.position)},
            {u"scale"_s, paramJson(t.scale)},
            {u"uniformScale"_s, t.uniformScale},
            {u"rotation"_s, paramJson(t.rotation)},
            {u"flipH"_s, t.flipH},
            {u"flipV"_s, t.flipV},
            {u"crop"_s, QJsonObject{{u"left"_s, paramJson(t.crop.left)},
                                    {u"top"_s, paramJson(t.crop.top)},
                                    {u"right"_s, paramJson(t.crop.right)},
                                    {u"bottom"_s, paramJson(t.crop.bottom)}}},
            {u"fit"_s, nameOf(kFitModes, t.fit)}};
}

QJsonValue backgroundJson(const std::optional<CanvasBackground> &background)
{
    if (!background) {
        return QJsonValue::Null;
    }
    QJsonObject object{{u"type"_s, nameOf(kBackgroundTypes, background->type)}};
    switch (background->type) {
    case BackgroundType::Color:
        object.insert(u"color"_s, background->color.toString());
        break;
    case BackgroundType::Blur:
        object.insert(u"amount"_s, background->amount);
        break;
    case BackgroundType::Image:
        object.insert(u"mediaId"_s, idValue(background->mediaId));
        break;
    case BackgroundType::Pattern:
        object.insert(u"asset"_s, background->pattern ? QJsonValue(assetRefJson(*background->pattern)) : QJsonValue::Null);
        break;
    }
    return object;
}

QJsonObject textStyleJson(const TextStyle &style)
{
    QJsonObject object{
        {u"font"_s, QJsonObject{{u"family"_s, style.fontFamily}, {u"weight"_s, style.fontWeight}, {u"italic"_s, style.italic}}},
        {u"size"_s, paramJson(style.size)},
        {u"color"_s, paramJson(style.color)},
        {u"stroke"_s, style.stroke ? QJsonValue(QJsonObject{{u"color"_s, paramJson(style.stroke->color)},
                                                            {u"width"_s, style.stroke->width}})
                                   : QJsonValue::Null},
        {u"shadow"_s, style.shadow ? QJsonValue(QJsonObject{{u"color"_s, style.shadow->color.toString()},
                                                            {u"offset"_s, QJsonArray{style.shadow->offset.x, style.shadow->offset.y}},
                                                            {u"blur"_s, style.shadow->blur}})
                                   : QJsonValue::Null},
        {u"background"_s, style.background ? QJsonValue(QJsonObject{{u"color"_s, style.background->color.toString()},
                                                                    {u"padding"_s, style.background->padding},
                                                                    {u"radius"_s, style.background->radius},
                                                                    {u"shape"_s, nameOf(kBubbleShapes, style.background->shape)},
                                                                    {u"tail"_s, nameOf(kBubbleTails, style.background->tail)},
                                                                    {u"tailSize"_s, style.background->tailSize},
                                                                    {u"borderColor"_s, style.background->borderColor.toString()},
                                                                    {u"borderWidth"_s, style.background->borderWidth},
                                                                    {u"accentColor"_s, style.background->accentColor.toString()}})
                                           : QJsonValue::Null},
        {u"letterSpacing"_s, paramJson(style.letterSpacing)},
        {u"lineHeight"_s, style.lineHeight},
        {u"align"_s, nameOf(kTextAligns, style.align)},
        {u"underline"_s, style.underline}};
    mergeInto(object, style.extras);
    return object;
}

inline QJsonArray vec2Json(const Vec2 &v)
{
    return QJsonArray{v.x, v.y};
}

QJsonObject maskJson(const Mask &mask)
{
    QJsonObject object{{u"id"_s, idValue(mask.id)},
                       {u"shape"_s, nameOf(kMaskShapes, mask.shape)},
                       {u"center"_s, paramJson(mask.center)},
                       {u"size"_s, paramJson(mask.size)},
                       {u"rotation"_s, paramJson(mask.rotation)},
                       {u"roundness"_s, paramJson(mask.roundness)},
                       {u"feather"_s, paramJson(mask.feather)},
                       {u"invert"_s, mask.invert}};
    if (mask.shape == MaskShape::Path && !mask.points.empty()) {
        QJsonArray points;
        for (const MaskPoint &pt : mask.points) {
            points.append(QJsonObject{{u"p"_s, vec2Json(pt.p)},
                                      {u"in"_s, vec2Json(pt.in)},
                                      {u"out"_s, vec2Json(pt.out)}});
        }
        object.insert(u"points"_s, points);
    }
    return object;
}

QJsonObject clipAnimationJson(const ClipAnimation &anim)
{
    QJsonObject object{{u"type"_s, assetRefJson(anim.type)},
                       {u"duration"_s, timeValue(anim.duration)},
                       {u"easing"_s, anim.easing.name()}};
    if (!anim.params.isEmpty()) {
        object.insert(u"params"_s, anim.params);
    }
    return object;
}

QJsonObject textAnimationJson(const TextAnimation &anim)
{
    QJsonObject object{{u"type"_s, nameOf(kTextAnimationTypes, anim.type)},
                       {u"scope"_s, nameOf(kTextAnimationScopes, anim.scope)},
                       {u"duration"_s, timeValue(anim.duration)},
                       {u"easing"_s, anim.easing.name()},
                       {u"cursor"_s, anim.cursor},
                       {u"stagger"_s, anim.stagger}};
    if (!anim.params.isEmpty()) {
        object.insert(u"params"_s, anim.params);
    }
    return object;
}

QJsonObject animationsJson(const ClipAnimations &anims)
{
    QJsonObject object;
    if (anims.in) {
        object.insert(u"in"_s, clipAnimationJson(*anims.in));
    }
    if (anims.out) {
        object.insert(u"out"_s, clipAnimationJson(*anims.out));
    }
    if (anims.loop) {
        object.insert(u"loop"_s, clipAnimationJson(*anims.loop));
    }
    return object;
}

QJsonObject visualizerJson(const AudioVisualizerSettings &v)
{
    QJsonObject object{{u"style"_s, nameOf(kVisualizerStyles, v.style)},
                       {u"barCount"_s, v.barCount},
                       {u"primaryColor"_s, v.primaryColor.toString()},
                       {u"secondaryColor"_s, v.secondaryColor.toString()},
                       {u"sensitivity"_s, v.sensitivity},
                       {u"smoothing"_s, v.smoothing},
                       {u"mirror"_s, v.mirror},
                       {u"roundness"_s, v.roundness},
                       {u"thickness"_s, v.thickness}};
    return object;
}

QJsonObject graphicJson(const GraphicSettings &g)
{
    QJsonObject object{{u"kind"_s, nameOf(kGraphicKinds, g.kind)},
                       {u"color"_s, g.color.toString()},
                       {u"color2"_s, g.color2.toString()},
                       {u"thickness"_s, g.thickness}};
    if (g.kind == GraphicKind::Counter) {
        object.insert(u"from"_s, g.from);
        object.insert(u"to"_s, g.to);
        object.insert(u"decimals"_s, g.decimals);
        object.insert(u"prefix"_s, g.prefix);
        object.insert(u"suffix"_s, g.suffix);
    }
    if (g.kind >= GraphicKind::Arrow) {
        object.insert(u"drawSeconds"_s, g.drawSeconds);
    }
    return object;
}

QJsonObject clipJson(const Clip &clip)
{
    QJsonObject object{{u"id"_s, idValue(clip.id)},
                       {u"kind"_s, nameOf(kClipKinds, clip.kind())},
                       {u"start"_s, timeValue(clip.start)},
                       {u"duration"_s, timeValue(clip.duration)},
                       {u"name"_s, nullableString(clip.name)},
                       {u"enabled"_s, clip.enabled},
                       {u"linkId"_s, idValue(clip.linkId)},
                       {u"transform"_s, transformJson(clip.transform)},
                       {u"opacity"_s, paramJson(clip.opacity)},
                       {u"blendMode"_s, nameOf(kBlendModes, clip.blendMode)},
                       {u"effects"_s, arrayOf(clip.effects, effectJson)},
                       {u"markers"_s, arrayOf(clip.markers, markerJson)}};
    if (!clip.masks.empty()) {
        object.insert(u"masks"_s, arrayOf(clip.masks, maskJson));
    }
    if (!clip.animations.isEmpty()) {
        object.insert(u"animations"_s, animationsJson(clip.animations));
    }
    if (clip.background) {
        object.insert(u"background"_s, backgroundJson(clip.background));
    }
    if (clip.placeholder) {
        object.insert(u"placeholder"_s, QJsonObject{{u"label"_s, clip.placeholder->label},
                                                    {u"kind"_s, nameOf(kPlaceholderKinds, clip.placeholder->kind)}});
    }
    std::visit(
        [&object](const auto &data) {
            using T = std::decay_t<decltype(data)>;
            if constexpr (std::is_same_v<T, MediaClipData>) {
                object.insert(u"mediaId"_s, idValue(data.mediaId));
                object.insert(u"streams"_s, nameOf(kStreams, data.streams));
                object.insert(u"sourceIn"_s, timeValue(data.sourceIn));
                if (data.curve) {
                    QJsonArray points;
                    for (const auto &[position, speed] : data.curve->points) {
                        points.append(QJsonArray{position, speed});
                    }
                    object.insert(u"speed"_s, QJsonObject{{u"curve"_s, QJsonObject{{u"preset"_s, nullableString(data.curve->preset)},
                                                                                     {u"points"_s, points}}}});
                } else {
                    object.insert(u"speed"_s, data.speed);
                }
                object.insert(u"preservePitch"_s, data.preservePitch);
                object.insert(u"reversed"_s, data.reversed);
                if (data.smooth) {
                    object.insert(u"smooth"_s, true); // absent = false (files of before keep their bytes)
                }
                if (data.cutout) {
                    object.insert(u"cutout"_s, true);
                }
                object.insert(u"audio"_s, QJsonObject{{u"gainDb"_s, paramJson(data.audio.gainDb)},
                                                      {u"muted"_s, data.audio.muted},
                                                      {u"pan"_s, paramJson(data.audio.pan)},
                                                      {u"fadeIn"_s, optionalTime(data.audio.fadeIn)},
                                                      {u"fadeOut"_s, optionalTime(data.audio.fadeOut)}});
            } else if constexpr (std::is_same_v<T, ColorClipData>) {
                object.insert(u"color"_s, paramJson(data.color));
            } else if constexpr (std::is_same_v<T, CompoundClipData>) {
                object.insert(u"sequenceId"_s, idValue(data.sequenceId));
                object.insert(u"sourceIn"_s, timeValue(data.sourceIn));
                if (data.activeAngle != 0) {
                    object.insert(u"activeAngle"_s, data.activeAngle);
                }
            } else if constexpr (std::is_same_v<T, TextClipData>) {
                object.insert(u"text"_s, data.text);
                object.insert(u"style"_s, textStyleJson(data.style));
                object.insert(u"stylePreset"_s, data.stylePreset ? QJsonValue(assetRefJson(*data.stylePreset)) : QJsonValue::Null);
                object.insert(u"box"_s, QJsonObject{{u"width"_s, data.boxWidth ? QJsonValue(*data.boxWidth) : QJsonValue::Null}});
                if (data.animation) {
                    object.insert(u"animation"_s, textAnimationJson(*data.animation));
                }
                mergeInto(object, data.fields);
            } else if constexpr (std::is_same_v<T, SubtitleClipData>) {
                object.insert(u"text"_s, data.text);
                QJsonArray words;
                for (const TimedWord &word : data.words) {
                    words.append(QJsonObject{{u"w"_s, word.text}, {u"t0"_s, timeValue(word.start)}, {u"t1"_s, timeValue(word.end)}});
                }
                object.insert(u"words"_s, words);
                if (!data.translation.isEmpty()) {
                    object.insert(u"translation"_s, data.translation);
                }
                object.insert(u"styleOverride"_s, data.styleOverride ? QJsonValue(textStyleJson(*data.styleOverride)) : QJsonValue::Null);
                mergeInto(object, data.fields);
            } else if constexpr (std::is_same_v<T, AdjustmentClipData>) {
                // Effects are serialized at the clip level
            } else if constexpr (std::is_same_v<T, StickerClipData>) {
                if (data.source) {
                    object.insert(u"source"_s, assetRefJson(*data.source));
                }
                if (!data.mediaId.isNull()) {
                    object.insert(u"mediaId"_s, idValue(data.mediaId));
                }
                if (!data.emoji.isEmpty()) {
                    object.insert(u"emoji"_s, data.emoji);
                }
                if (!data.loop) {
                    object.insert(u"loop"_s, data.loop);
                }
                if (data.speed != 1.0) {
                    object.insert(u"speed"_s, data.speed);
                }
                if (data.tint.a > 0) {
                    object.insert(u"tint"_s, data.tint.toString());
                }
                if (data.visualizer) {
                    object.insert(u"visualizer"_s, visualizerJson(*data.visualizer));
                }
                if (data.graphic) {
                    object.insert(u"graphic"_s, graphicJson(*data.graphic));
                }
                mergeInto(object, data.fields);
            } else {
                mergeInto(object, data.fields);
            }
        },
        clip.payload);
    mergeInto(object, clip.extras);
    return object;
}

QJsonObject transitionJson(const Transition &transition)
{
    return {{u"id"_s, idValue(transition.id)},
            {u"type"_s, assetRefJson(transition.type)},
            {u"from"_s, idValue(transition.from)},
            {u"to"_s, idValue(transition.to)},
            {u"duration"_s, timeValue(transition.duration)},
            {u"alignment"_s, nameOf(kAlignments, transition.alignment)},
            {u"fillMissing"_s, nameOf(kMissingMaterial, transition.fillMissing)},
            {u"audioCrossfade"_s, transition.audioCrossfade},
            {u"params"_s, paramMapJson(transition.params)}};
}

QJsonObject trackJson(const Track &track)
{
    QJsonObject object{{u"id"_s, idValue(track.id)},
                       {u"kind"_s, nameOf(kTrackKinds, track.kind)},
                       {u"name"_s, nullableString(track.name)},
                       {u"locked"_s, track.locked},
                       {u"muted"_s, track.muted},
                       {u"solo"_s, track.solo},
                       {u"hidden"_s, track.hidden},
                       {u"height"_s, track.height},
                       {u"captions"_s, track.captions},
                       {u"gainDb"_s, paramJson(track.gainDb)},
                       {u"clips"_s, arrayOf(track.clips, clipJson)},
                       {u"transitions"_s, arrayOf(track.transitions, transitionJson)}};
    mergeInto(object, track.extras);
    return object;
}

QJsonObject sequenceJson(const Sequence &sequence)
{
    QJsonObject object{{u"id"_s, idValue(sequence.id)},
                       {u"name"_s, sequence.name},
                       {u"canvas"_s, canvasJson(sequence.canvas)},
                       {u"magneticMain"_s, sequence.magneticMain},
                       {u"defaultBackground"_s, backgroundJson(sequence.defaultBackground)},
                       {u"visualTracks"_s, arrayOf(sequence.visualTracks, trackJson)},
                       {u"audioTracks"_s, arrayOf(sequence.audioTracks, trackJson)},
                       {u"markers"_s, arrayOf(sequence.markers, markerJson)},
                       {u"groups"_s, arrayOf(sequence.groups, [](const Group &group) {
                            QJsonArray ids;
                            for (const ClipId &id : group.clipIds) {
                                ids.append(id.toString());
                            }
                            return QJsonObject{{u"id"_s, idValue(group.id)}, {u"clipIds"_s, ids}};
                        })}};
    mergeInto(object, sequence.extras);
    return object;
}

QJsonObject mediaJson(const Media &media)
{
    QJsonObject info;
    if (media.info.duration) {
        info.insert(u"duration"_s, timeValue(*media.info.duration));
    }
    if (const auto &v = media.info.video) {
        QJsonObject video{{u"width"_s, v->width},
                          {u"height"_s, v->height},
                          {u"vfr"_s, v->variableFrameRate},
                          {u"rotation"_s, v->rotation},
                          {u"codec"_s, v->codec},
                          {u"pixelFormat"_s, v->pixelFormat},
                          {u"primaries"_s, v->primaries},
                          {u"transfer"_s, v->transfer},
                          {u"hdr"_s, v->hdr},
                          {u"hasAlpha"_s, v->hasAlpha},
                          {u"sar"_s, v->sampleAspectRatio.toString()}};
        if (v->frameRate) {
            video.insert(u"frameRate"_s, v->frameRate->toString());
        }
        info.insert(u"video"_s, video);
    }
    if (const auto &a = media.info.audio) {
        info.insert(u"audio"_s, QJsonObject{{u"codec"_s, a->codec}, {u"sampleRate"_s, a->sampleRate}, {u"channels"_s, a->channels}});
    }
    return {{u"id"_s, idValue(media.id)},
            {u"kind"_s, nameOf(kMediaKinds, media.kind)},
            {u"name"_s, media.name},
            {u"path"_s, media.path},
            {u"relativePath"_s, nullableString(media.relativePath)},
            {u"fingerprint"_s, QJsonObject{{u"algo"_s, media.fingerprint.algorithm},
                                           {u"value"_s, media.fingerprint.value},
                                           {u"size"_s, static_cast<double>(media.fingerprint.size)}}},
            {u"info"_s, info},
            {u"proxy"_s, nameOf(kProxyPolicies, media.proxy)},
            {u"favorite"_s, media.favorite},
            {u"folderId"_s, idValue(media.folderId)}};
}

QString isoDate(const QDateTime &dateTime)
{
    return dateTime.toUTC().toString(Qt::ISODate);
}

// ---------------------------------------------------------------- reading

class Reader
{
public:
    QStringList warnings;
    QString error;

    bool failed() const { return !error.isEmpty(); }

    void fail(const QString &path, const QString &message)
    {
        if (error.isEmpty()) {
            error = path + u": "_s + message;
        }
    }

    void warn(const QString &path, const QString &message) { warnings.append(path + u": "_s + message); }

    static QString join(const QString &path, const QString &key) { return path.isEmpty() ? key : path + u'.' + key; }
    static QString index(const QString &path, qsizetype i) { return path + u'[' + QString::number(i) + u']'; }

    QJsonObject object(const QJsonObject &parent, const QString &key, const QString &path, bool required = true)
    {
        const QJsonValue value = parent.value(key);
        if (value.isObject()) {
            return value.toObject();
        }
        if (required) {
            fail(join(path, key), u"object expected"_s);
        } else if (!value.isUndefined() && !value.isNull()) {
            warn(join(path, key), u"object expected, ignored"_s);
        }
        return {};
    }

    QJsonArray array(const QJsonObject &parent, const QString &key, const QString &path)
    {
        const QJsonValue value = parent.value(key);
        if (value.isArray()) {
            return value.toArray();
        }
        if (!value.isUndefined() && !value.isNull()) {
            warn(join(path, key), u"array expected, ignored"_s);
        }
        return {};
    }

    QString string(const QJsonObject &parent, const QString &key, const QString &path, const QString &fallback = {})
    {
        const QJsonValue value = parent.value(key);
        if (value.isString()) {
            return value.toString();
        }
        if (!value.isUndefined() && !value.isNull()) {
            warn(join(path, key), u"string expected, default used"_s);
        }
        return fallback;
    }

    bool boolean(const QJsonObject &parent, const QString &key, const QString &path, bool fallback)
    {
        const QJsonValue value = parent.value(key);
        if (value.isBool()) {
            return value.toBool();
        }
        if (!value.isUndefined()) {
            warn(join(path, key), u"boolean expected, default used"_s);
        }
        return fallback;
    }

    double number(const QJsonObject &parent, const QString &key, const QString &path, double fallback, double min,
                  double max)
    {
        const QJsonValue value = parent.value(key);
        if (value.isUndefined()) {
            return fallback;
        }
        if (!value.isDouble() || !std::isfinite(value.toDouble())) {
            warn(join(path, key), u"number expected, default used"_s);
            return fallback;
        }
        const double number = value.toDouble();
        if (number < min || number > max) {
            warn(join(path, key), u"out of range, clamped"_s);
            return std::clamp(number, min, max);
        }
        return number;
    }

    int integer(const QJsonObject &parent, const QString &key, const QString &path, int fallback, int min, int max)
    {
        const double value = number(parent, key, path, fallback, min, max);
        if (value != std::floor(value)) {
            warn(join(path, key), u"integer expected, rounded"_s);
        }
        return static_cast<int>(std::lround(value));
    }

    template<typename E, size_t N>
    E enumeration(const QJsonObject &parent, const QString &key, const QString &path, const EnumName<E> (&table)[N],
                  E fallback)
    {
        const QJsonValue value = parent.value(key);
        if (value.isUndefined()) {
            return fallback;
        }
        if (const auto parsed = valueOf(table, value.toString())) {
            return *parsed;
        }
        warn(join(path, key), u"unknown value '"_s + value.toString() + u"', default used"_s);
        return fallback;
    }

    std::optional<RationalTime> timeFrom(const QJsonValue &value, const QString &path, bool required)
    {
        if (value.isString()) {
            if (auto time = RationalTime::fromString(value.toString())) {
                return time;
            }
            fail(path, u"invalid time '"_s + value.toString() + u'\'');
            return std::nullopt;
        }
        if (required) {
            fail(path, u"time expected"_s);
        } else if (!value.isUndefined() && !value.isNull()) {
            warn(path, u"time expected, ignored"_s);
        }
        return std::nullopt;
    }

    RationalTime requiredTime(const QJsonObject &parent, const QString &key, const QString &path)
    {
        return timeFrom(parent.value(key), join(path, key), true).value_or(RationalTime());
    }

    std::optional<RationalTime> optionalTime(const QJsonObject &parent, const QString &key, const QString &path)
    {
        return timeFrom(parent.value(key), join(path, key), false);
    }

    template<typename Tag>
    Id<Tag> id(const QJsonObject &parent, const QString &key, const QString &path, bool required)
    {
        const QJsonValue value = parent.value(key);
        if (value.isString()) {
            if (auto parsed = Id<Tag>::fromString(value.toString())) {
                return *parsed;
            }
        }
        if (required) {
            fail(join(path, key), u"valid id expected"_s);
        } else if (!value.isUndefined() && !value.isNull()) {
            warn(join(path, key), u"invalid id, ignored"_s);
        }
        return {};
    }

    std::optional<Rational> rational(const QJsonObject &parent, const QString &key, const QString &path, bool required)
    {
        const QJsonValue value = parent.value(key);
        if (value.isString()) {
            if (auto parsed = Rational::fromString(value.toString()); parsed && parsed->isPositive()) {
                return parsed;
            }
        }
        if (required) {
            fail(join(path, key), u"positive rational expected"_s);
        } else if (!value.isUndefined() && !value.isNull()) {
            warn(join(path, key), u"invalid rational, ignored"_s);
        }
        return std::nullopt;
    }

    static ParamValue paramValue(const QJsonValue &value)
    {
        if (value.isDouble()) {
            return value.toDouble();
        }
        if (value.isBool()) {
            return value.toBool();
        }
        if (value.isString()) {
            const QString text = value.toString();
            if (auto color = Color::fromString(text)) {
                return *color;
            }
            return text;
        }
        if (value.isArray()) {
            const QJsonArray array = value.toArray();
            if (array.size() == 2 && array[0].isDouble() && array[1].isDouble()) {
                return Vec2{array[0].toDouble(), array[1].toDouble()};
            }
        }
        return value; // preserved verbatim
    }

    // `expected`: if set, the value must have the same type (typed model fields); otherwise any type.
    Param param(const QJsonValue &json, const QString &path, const std::optional<Param> &expected = std::nullopt)
    {
        if (json.isUndefined()) {
            return expected.value_or(Param());
        }
        const auto matches = [&expected](const ParamValue &value) {
            return !expected || value.index() == expected->staticValue().index();
        };
        const QJsonObject object = json.toObject();
        if (json.isObject() && object.contains(u"keyframes"_s)) {
            std::vector<Keyframe> keyframes;
            const QJsonArray array = object.value(u"keyframes"_s).toArray();
            for (qsizetype i = 0; i < array.size(); ++i) {
                const QJsonObject k = array[i].toObject();
                const QString kPath = index(join(path, u"keyframes"_s), i);
                Keyframe keyframe;
                keyframe.time = requiredTime(k, u"t"_s, kPath);
                keyframe.value = paramValue(k.value(u"v"_s));
                keyframe.interpolation = enumeration(k, u"interp"_s, kPath, kInterpolations, Interpolation::Linear);
                const QJsonValue ease = k.value(u"ease"_s);
                if (ease.isString()) {
                    if (auto easing = Easing::fromName(ease.toString())) {
                        keyframe.easing = *easing;
                    } else {
                        warn(join(kPath, u"ease"_s), u"unknown easing, default used"_s);
                    }
                } else if (ease.isArray()) {
                    const QJsonArray b = ease.toArray();
                    std::optional<Easing> easing;
                    if (b.size() == 4) {
                        easing = Easing::cubicBezier(b[0].toDouble(), b[1].toDouble(), b[2].toDouble(), b[3].toDouble());
                    }
                    if (easing) {
                        keyframe.easing = *easing;
                    } else {
                        warn(join(kPath, u"ease"_s), u"invalid bezier, default used"_s);
                    }
                }
                keyframes.push_back(std::move(keyframe));
            }
            if (failed()) {
                return {};
            }
            Param result;
            if (keyframes.empty() || !matches(keyframes.front().value) || !result.setKeyframes(std::move(keyframes))) {
                warn(path, u"invalid keyframes, static default used"_s);
                return expected.value_or(Param());
            }
            return result;
        }
        const ParamValue value = paramValue(json);
        if (!matches(value)) {
            warn(path, u"unexpected value type, default used"_s);
            return *expected;
        }
        return Param(value);
    }

    std::map<QString, Param> paramMap(const QJsonObject &parent, const QString &key, const QString &path)
    {
        std::map<QString, Param> params;
        const QJsonObject object = this->object(parent, key, path, false);
        for (auto it = object.begin(); it != object.end(); ++it) {
            params.emplace(it.key(), param(it.value(), join(join(path, key), it.key())));
        }
        return params;
    }

    std::optional<AssetRef> assetRef(const QJsonValue &value, const QString &path, bool required)
    {
        if (!value.isObject()) {
            if (required) {
                fail(path, u"asset reference expected"_s);
            }
            return std::nullopt;
        }
        const QJsonObject object = value.toObject();
        AssetRef ref{string(object, u"pack"_s, path), string(object, u"id"_s, path),
                     integer(object, u"version"_s, path, 1, 1, 1000000)};
        if (!ref.isValid()) {
            if (required) {
                fail(path, u"invalid asset reference"_s);
            } else {
                warn(path, u"invalid asset reference, ignored"_s);
            }
            return std::nullopt;
        }
        return ref;
    }

    Canvas canvas(const QJsonObject &parent, const QString &key, const QString &path)
    {
        const QJsonObject object = this->object(parent, key, path);
        const QString p = join(path, key);
        Canvas canvas;
        canvas.width = integer(object, u"width"_s, p, 1920, 2, 16384);
        canvas.height = integer(object, u"height"_s, p, 1080, 2, 16384);
        canvas.preset = enumeration(object, u"preset"_s, p, kCanvasPresets, CanvasPreset::Custom);
        return canvas;
    }

    Marker marker(const QJsonObject &object, const QString &path)
    {
        Marker marker;
        marker.id = id<MarkerTag>(object, u"id"_s, path, true);
        marker.time = requiredTime(object, u"t"_s, path);
        marker.duration = optionalTime(object, u"duration"_s, path);
        marker.name = string(object, u"name"_s, path);
        marker.color = string(object, u"color"_s, path, u"primary"_s);
        marker.note = string(object, u"note"_s, path);
        marker.kind = enumeration(object, u"kind"_s, path, kMarkerKinds, MarkerKind::User);
        return marker;
    }

    std::vector<Marker> markers(const QJsonObject &parent, const QString &path)
    {
        std::vector<Marker> result;
        const QJsonArray array = this->array(parent, u"markers"_s, path);
        for (qsizetype i = 0; i < array.size(); ++i) {
            result.push_back(marker(array[i].toObject(), index(join(path, u"markers"_s), i)));
        }
        return result;
    }

    Effect effect(const QJsonObject &object, const QString &path)
    {
        Effect effect;
        effect.id = id<EffectTag>(object, u"id"_s, path, true);
        effect.type = string(object, u"type"_s, path);
        if (effect.type.isEmpty()) {
            fail(join(path, u"type"_s), u"effect type expected"_s);
        }
        effect.typeVersion = integer(object, u"typeVersion"_s, path, 1, 1, 1000000);
        effect.enabled = boolean(object, u"enabled"_s, path, true);
        effect.preset = assetRef(object.value(u"preset"_s), join(path, u"preset"_s), false);
        effect.intensity = param(object.value(u"intensity"_s), join(path, u"intensity"_s), Param(1.0));
        effect.params = paramMap(object, u"params"_s, path);
        return effect;
    }

    Transform transform(const QJsonObject &parent, const QString &path)
    {
        Transform t;
        const QJsonObject object = this->object(parent, u"transform"_s, path, false);
        const QString p = join(path, u"transform"_s);
        t.position = param(object.value(u"position"_s), join(p, u"position"_s), t.position);
        t.scale = param(object.value(u"scale"_s), join(p, u"scale"_s), t.scale);
        t.uniformScale = boolean(object, u"uniformScale"_s, p, true);
        t.rotation = param(object.value(u"rotation"_s), join(p, u"rotation"_s), t.rotation);
        t.flipH = boolean(object, u"flipH"_s, p, false);
        t.flipV = boolean(object, u"flipV"_s, p, false);
        const QJsonObject crop = this->object(object, u"crop"_s, p, false);
        const QString c = join(p, u"crop"_s);
        t.crop.left = param(crop.value(u"left"_s), join(c, u"left"_s), t.crop.left);
        t.crop.top = param(crop.value(u"top"_s), join(c, u"top"_s), t.crop.top);
        t.crop.right = param(crop.value(u"right"_s), join(c, u"right"_s), t.crop.right);
        t.crop.bottom = param(crop.value(u"bottom"_s), join(c, u"bottom"_s), t.crop.bottom);
        t.fit = enumeration(object, u"fit"_s, p, kFitModes, FitMode::Contain);
        return t;
    }

    Color colorValue(const QJsonValue &value, const QString &path, Color fallback)
    {
        if (value.isUndefined() || value.isNull()) {
            return fallback;
        }
        if (const auto parsed = Color::fromString(value.toString())) {
            return *parsed;
        }
        warn(path, u"invalid colour, default used"_s);
        return fallback;
    }

    std::optional<CanvasBackground> background(const QJsonValue &value, const QString &path)
    {
        if (value.isUndefined() || value.isNull()) {
            return std::nullopt;
        }
        if (!value.isObject()) {
            warn(path, u"invalid background ignored"_s);
            return std::nullopt;
        }
        const QJsonObject object = value.toObject();
        CanvasBackground background;
        background.type = enumeration(object, u"type"_s, path, kBackgroundTypes, BackgroundType::Color);
        background.color = colorValue(object.value(u"color"_s), join(path, u"color"_s), Color{0, 0, 0, 255});
        background.amount = number(object, u"amount"_s, path, 0.6, 0.0, 1.0);
        background.mediaId = id<MediaTag>(object, u"mediaId"_s, path, false);
        background.pattern = assetRef(object.value(u"asset"_s), join(path, u"asset"_s), false);
        return background;
    }

    GraphicSettings graphic(const QJsonObject &gObj, const QString &gPath)
    {
        GraphicSettings g;
        g.kind = enumeration(gObj, u"kind"_s, gPath, kGraphicKinds, GraphicKind::Counter);
        g.from = number(gObj, u"from"_s, gPath, 0.0, -1e12, 1e12);
        g.to = number(gObj, u"to"_s, gPath, 100.0, -1e12, 1e12);
        g.decimals = integer(gObj, u"decimals"_s, gPath, 0, 0, 4);
        g.prefix = string(gObj, u"prefix"_s, gPath, {});
        g.suffix = string(gObj, u"suffix"_s, gPath, {});
        g.color = colorValue(gObj.value(u"color"_s), join(gPath, u"color"_s), Color{255, 255, 255, 255});
        g.color2 = colorValue(gObj.value(u"color2"_s), join(gPath, u"color2"_s), Color{255, 255, 255, 80});
        g.thickness = number(gObj, u"thickness"_s, gPath, 0.5, 0.0, 1.0);
        g.drawSeconds = number(gObj, u"drawSeconds"_s, gPath, 0.6, 0.05, 10.0);
        return g;
    }

    AudioVisualizerSettings visualizer(const QJsonObject &vObj, const QString &vPath)
    {
        AudioVisualizerSettings v;
        v.style = enumeration(vObj, u"style"_s, vPath, kVisualizerStyles, VisualizerStyle::Bars);
        v.barCount = integer(vObj, u"barCount"_s, vPath, 32, 4, 128);
        v.primaryColor = colorValue(vObj.value(u"primaryColor"_s), join(vPath, u"primaryColor"_s), Color{0, 220, 255, 255});
        v.secondaryColor = colorValue(vObj.value(u"secondaryColor"_s), join(vPath, u"secondaryColor"_s), Color{255, 100, 200, 255});
        v.sensitivity = number(vObj, u"sensitivity"_s, vPath, 1.0, 0.1, 10.0);
        v.smoothing = number(vObj, u"smoothing"_s, vPath, 0.5, 0.0, 1.0);
        v.mirror = boolean(vObj, u"mirror"_s, vPath, false);
        v.roundness = number(vObj, u"roundness"_s, vPath, 0.5, 0.0, 1.0);
        v.thickness = number(vObj, u"thickness"_s, vPath, 3.0, 0.5, 30.0);
        return v;
    }

    TextClipData text(const QJsonObject &object, const QString &path)
    {
        TextClipData data;
        data.text = string(object, u"text"_s, path);
        data.style = textStyle(this->object(object, u"style"_s, path, false), join(path, u"style"_s));
        data.stylePreset = assetRef(object.value(u"stylePreset"_s), join(path, u"stylePreset"_s), false);
        const QJsonValue width = object.value(u"box"_s).toObject().value(u"width"_s);
        if (width.isDouble() && width.toDouble() > 0.0) {
            data.boxWidth = std::min(width.toDouble(), 4.0);
        }
        if (object.value(u"animation"_s).isObject()) {
            data.animation = textAnimation(object.value(u"animation"_s).toObject(), join(path, u"animation"_s));
        }
        data.fields = unknownKeys(object, kClipCommonKeys, kTextClipKeys);
        return data;
    }

    std::optional<TextAnimation> textAnimation(const QJsonObject &object, const QString &path)
    {
        if (object.isEmpty()) {
            return std::nullopt;
        }
        TextAnimation anim;
        anim.type = enumeration(object, u"type"_s, path, kTextAnimationTypes, TextAnimationType::None);
        anim.scope = enumeration(object, u"scope"_s, path, kTextAnimationScopes, TextAnimationScope::Character);
        anim.duration = optionalTime(object, u"duration"_s, path).value_or(RationalTime(45, 30));
        const QString easingName = string(object, u"easing"_s, path);
        if (!easingName.isEmpty()) {
            if (auto e = Easing::fromName(easingName)) {
                anim.easing = *e;
            }
        }
        anim.cursor = boolean(object, u"cursor"_s, path, true);
        anim.stagger = number(object, u"stagger"_s, path, 0.05, 0.0, 2.0);
        anim.params = this->object(object, u"params"_s, path, false);
        return anim;
    }

    TextStyle textStyle(const QJsonObject &style, const QString &s)
    {
        TextStyle result;
        const QJsonObject font = this->object(style, u"font"_s, s, false);
        const QString f = join(s, u"font"_s);
        result.fontFamily = string(font, u"family"_s, f, u"Inter"_s);
        result.fontWeight = integer(font, u"weight"_s, f, 700, 100, 1000);
        result.italic = boolean(font, u"italic"_s, f, false);
        result.size = param(style.value(u"size"_s), join(s, u"size"_s), Param(0.06));
        result.color = param(style.value(u"color"_s), join(s, u"color"_s), Param(Color{255, 255, 255, 255}));
        if (style.value(u"stroke"_s).isObject()) {
            const QJsonObject stroke = style.value(u"stroke"_s).toObject();
            const QString k = join(s, u"stroke"_s);
            result.stroke = TextStroke{param(stroke.value(u"color"_s), join(k, u"color"_s), Param(Color{0, 0, 0, 255})),
                                           number(stroke, u"width"_s, k, 0.08, 0.0, 1.0)};
        }
        if (style.value(u"shadow"_s).isObject()) {
            const QJsonObject shadow = style.value(u"shadow"_s).toObject();
            const QString k = join(s, u"shadow"_s);
            TextShadow value;
            value.color = colorValue(shadow.value(u"color"_s), join(k, u"color"_s), value.color);
            const QJsonArray offset = shadow.value(u"offset"_s).toArray();
            if (offset.size() == 2) {
                value.offset = Vec2{offset[0].toDouble(), offset[1].toDouble()};
            }
            value.blur = number(shadow, u"blur"_s, k, 0.03, 0.0, 1.0);
            result.shadow = value;
        }
        if (style.value(u"background"_s).isObject()) {
            const QJsonObject box = style.value(u"background"_s).toObject();
            const QString k = join(s, u"background"_s);
            TextBackground value;
            value.color = colorValue(box.value(u"color"_s), join(k, u"color"_s), value.color);
            value.padding = number(box, u"padding"_s, k, 0.25, 0.0, 4.0);
            value.radius = number(box, u"radius"_s, k, 0.2, 0.0, 4.0);
            value.shape = enumeration(box, u"shape"_s, k, kBubbleShapes, BubbleShape::Rectangle);
            value.tail = enumeration(box, u"tail"_s, k, kBubbleTails, BubbleTail::None);
            value.tailSize = number(box, u"tailSize"_s, k, 0.4, 0.0, 4.0);
            value.borderColor = colorValue(box.value(u"borderColor"_s), join(k, u"borderColor"_s), value.borderColor);
            value.borderWidth = number(box, u"borderWidth"_s, k, 0.0, 0.0, 1.0);
            value.accentColor = colorValue(box.value(u"accentColor"_s), join(k, u"accentColor"_s), value.accentColor);
            result.background = value;
        }
        result.letterSpacing = param(style.value(u"letterSpacing"_s), join(s, u"letterSpacing"_s), Param(0.0));
        result.lineHeight = number(style, u"lineHeight"_s, s, 1.2, 0.5, 4.0);
        result.align = enumeration(style, u"align"_s, s, kTextAligns, TextAlign::Center);
        result.underline = boolean(style, u"underline"_s, s, false);
        result.extras = unknownKeys(style, kTextStyleKeys);
        return result;
    }

    Mask mask(const QJsonObject &object, const QString &path)
    {
        Mask m;
        m.id = id<MaskTag>(object, u"id"_s, path, true);
        m.shape = enumeration(object, u"shape"_s, path, kMaskShapes, MaskShape::Rectangle);
        m.center = param(object.value(u"center"_s), join(path, u"center"_s), Param(Vec2{0.0, 0.0}));
        m.size = param(object.value(u"size"_s), join(path, u"size"_s), Param(Vec2{0.5, 0.5}));
        m.rotation = param(object.value(u"rotation"_s), join(path, u"rotation"_s), Param(0.0));
        m.roundness = param(object.value(u"roundness"_s), join(path, u"roundness"_s), Param(0.0));
        m.feather = param(object.value(u"feather"_s), join(path, u"feather"_s), Param(0.0));
        m.invert = boolean(object, u"invert"_s, path, false);
        const QJsonArray points = object.value(u"points"_s).toArray();
        for (const QJsonValue &pv : points) {
            const QJsonObject po = pv.toObject();
            MaskPoint pt;
            const QJsonArray pa = po.value(u"p"_s).toArray();
            if (pa.size() == 2) {
                pt.p = Vec2{pa[0].toDouble(), pa[1].toDouble()};
            }
            const QJsonArray ina = po.value(u"in"_s).toArray();
            if (ina.size() == 2) {
                pt.in = Vec2{ina[0].toDouble(), ina[1].toDouble()};
            }
            const QJsonArray outa = po.value(u"out"_s).toArray();
            if (outa.size() == 2) {
                pt.out = Vec2{outa[0].toDouble(), outa[1].toDouble()};
            }
            m.points.push_back(pt);
        }
        return m;
    }

    ClipAnimation clipAnimation(const QJsonObject &object, const QString &path)
    {
        ClipAnimation anim;
        anim.type = assetRef(object.value(u"type"_s), join(path, u"type"_s), true).value_or(AssetRef{});
        anim.duration = requiredTime(object, u"duration"_s, path);
        const QString easingName = string(object, u"easing"_s, path);
        if (!easingName.isEmpty()) {
            if (auto e = Easing::fromName(easingName)) {
                anim.easing = *e;
            }
        }
        anim.params = this->object(object, u"params"_s, path, false);
        return anim;
    }

    ClipAnimations animations(const QJsonObject &object, const QString &path)
    {
        ClipAnimations anims;
        if (object.value(u"in"_s).isObject()) {
            anims.in = clipAnimation(object.value(u"in"_s).toObject(), join(path, u"in"_s));
        }
        if (object.value(u"out"_s).isObject()) {
            anims.out = clipAnimation(object.value(u"out"_s).toObject(), join(path, u"out"_s));
        }
        if (object.value(u"loop"_s).isObject()) {
            anims.loop = clipAnimation(object.value(u"loop"_s).toObject(), join(path, u"loop"_s));
        }
        return anims;
    }

    Clip clip(const QJsonObject &object, const QString &path)
    {
        Clip clip;
        clip.id = id<ClipTag>(object, u"id"_s, path, true);
        const QJsonValue kindValue = object.value(u"kind"_s);
        const auto kind = valueOf(kClipKinds, kindValue.toString());
        if (!kind) {
            fail(join(path, u"kind"_s), u"unknown clip kind '"_s + kindValue.toString() + u'\'');
            return clip;
        }
        clip.start = requiredTime(object, u"start"_s, path);
        clip.duration = requiredTime(object, u"duration"_s, path);
        clip.name = string(object, u"name"_s, path);
        clip.enabled = boolean(object, u"enabled"_s, path, true);
        clip.linkId = id<LinkTag>(object, u"linkId"_s, path, false);
        clip.transform = transform(object, path);
        clip.opacity = param(object.value(u"opacity"_s), join(path, u"opacity"_s), Param(1.0));
        clip.blendMode = enumeration(object, u"blendMode"_s, path, kBlendModes, BlendMode::Normal);
        clip.background = background(object.value(u"background"_s), join(path, u"background"_s));
        const QJsonArray effects = array(object, u"effects"_s, path);
        for (qsizetype i = 0; i < effects.size(); ++i) {
            clip.effects.push_back(effect(effects[i].toObject(), index(join(path, u"effects"_s), i)));
        }
        const QJsonArray masks = array(object, u"masks"_s, path);
        for (qsizetype i = 0; i < masks.size(); ++i) {
            clip.masks.push_back(mask(masks[i].toObject(), index(join(path, u"masks"_s), i)));
        }
        if (object.value(u"animations"_s).isObject()) {
            clip.animations = animations(object.value(u"animations"_s).toObject(), join(path, u"animations"_s));
        }
        clip.markers = markers(object, path);
        if (object.contains(u"placeholder"_s)) {
            const QJsonObject slot = this->object(object, u"placeholder"_s, path, false);
            const QString slotPath = join(path, u"placeholder"_s);
            clip.placeholder = Placeholder{string(slot, u"label"_s, slotPath, {}),
                                           enumeration(slot, u"kind"_s, slotPath, kPlaceholderKinds, PlaceholderKind::Any)};
        }

        switch (*kind) {
        case ClipKind::Media: {
            MediaClipData data;
            data.mediaId = id<MediaTag>(object, u"mediaId"_s, path, true);
            data.streams = enumeration(object, u"streams"_s, path, kStreams, Streams::AudioVideo);
            data.sourceIn = requiredTime(object, u"sourceIn"_s, path);
            const QJsonValue speed = object.value(u"speed"_s);
            if (speed.isObject()) {
                const QJsonObject curveObject = speed.toObject().value(u"curve"_s).toObject();
                SpeedCurve curve;
                curve.preset = string(curveObject, u"preset"_s, join(path, u"speed.curve"_s));
                for (const QJsonValue &point : curveObject.value(u"points"_s).toArray()) {
                    const QJsonArray pair = point.toArray();
                    const double position = pair.at(0).toDouble(-1.0);
                    const double value = pair.at(1).toDouble(-1.0);
                    if (pair.size() == 2 && position >= 0.0 && position <= 1.0 && value > 0.0) {
                        curve.points.emplace_back(position, value);
                    } else {
                        warn(join(path, u"speed.curve.points"_s), u"invalid point ignored"_s);
                    }
                }
                data.curve = curve;
            } else {
                data.speed = number(object, u"speed"_s, path, 1.0, 0.1, 100.0);
            }
            data.preservePitch = boolean(object, u"preservePitch"_s, path, true);
            data.reversed = boolean(object, u"reversed"_s, path, false);
            data.smooth = boolean(object, u"smooth"_s, path, false);
            data.cutout = boolean(object, u"cutout"_s, path, false);
            const QJsonObject audio = this->object(object, u"audio"_s, path, false);
            const QString a = join(path, u"audio"_s);
            data.audio.gainDb = param(audio.value(u"gainDb"_s), join(a, u"gainDb"_s), Param(0.0));
            data.audio.muted = boolean(audio, u"muted"_s, a, false);
            data.audio.pan = param(audio.value(u"pan"_s), join(a, u"pan"_s), Param(0.0));
            data.audio.fadeIn = optionalTime(audio, u"fadeIn"_s, a);
            data.audio.fadeOut = optionalTime(audio, u"fadeOut"_s, a);
            clip.payload = std::move(data);
            clip.extras = unknownKeys(object, kClipCommonKeys, kMediaClipKeys);
            break;
        }
        case ClipKind::Color:
            clip.payload = ColorClipData{param(object.value(u"color"_s), join(path, u"color"_s), Param(Color{}))};
            clip.extras = unknownKeys(object, kClipCommonKeys, kColorClipKeys);
            break;
        case ClipKind::Text:
            clip.payload = text(object, path);
            break;
        case ClipKind::Compound: {
            const int angle = object.value(u"activeAngle"_s).toInt(0);
            clip.payload = CompoundClipData{id<SequenceTag>(object, u"sequenceId"_s, path, true),
                                            requiredTime(object, u"sourceIn"_s, path),
                                            angle};
            clip.extras = unknownKeys(object, kClipCommonKeys, kCompoundClipKeys);
            break;
        }
        case ClipKind::Adjustment:
            clip.payload = AdjustmentClipData{clip.effects};
            clip.extras = unknownKeys(object, kClipCommonKeys);
            break;
        case ClipKind::Sticker: {
            StickerClipData data;
            data.source = assetRef(object.value(u"source"_s), join(path, u"source"_s), false);
            data.mediaId = id<MediaTag>(object, u"mediaId"_s, path, false);
            data.emoji = string(object, u"emoji"_s, path, {});
            data.loop = boolean(object, u"loop"_s, path, true);
            data.speed = number(object, u"speed"_s, path, 1.0, 0.1, 10.0);
            if (object.contains(u"tint"_s)) {
                data.tint = colorValue(object.value(u"tint"_s), join(path, u"tint"_s), Color{0, 0, 0, 0});
            }
            if (object.contains(u"visualizer"_s)) {
                data.visualizer = visualizer(this->object(object, u"visualizer"_s, path, false), join(path, u"visualizer"_s));
            }
            if (object.contains(u"graphic"_s)) {
                data.graphic = graphic(this->object(object, u"graphic"_s, path, false), join(path, u"graphic"_s));
            }
            if (!data.source && data.mediaId.isNull() && data.emoji.isEmpty() && !data.visualizer && !data.graphic) {
                warn(path, u"sticker without source, mediaId, emoji, visualizer or graphic: it shows nothing"_s);
            }
            data.fields = unknownKeys(object, kClipCommonKeys, kStickerClipKeys);
            clip.payload = std::move(data);
            break;
        }
        case ClipKind::Subtitle: {
            SubtitleClipData data;
            data.text = string(object, u"text"_s, path);
            const QString wordsPath = join(path, u"words"_s);
            for (const QJsonValue &value : object.value(u"words"_s).toArray()) {
                const QJsonObject word = value.toObject();
                const std::optional<RationalTime> t0 = timeFrom(word.value(u"t0"_s), join(wordsPath, u"t0"_s), false);
                const std::optional<RationalTime> t1 = timeFrom(word.value(u"t1"_s), join(wordsPath, u"t1"_s), false);
                if (!t0 || !t1 || *t1 < *t0 || word.value(u"w"_s).toString().isEmpty()) {
                    warn(wordsPath, u"invalid word ignored"_s);
                    continue;
                }
                data.words.push_back(TimedWord{word.value(u"w"_s).toString(), *t0, *t1});
            }
            if (object.value(u"styleOverride"_s).isObject()) {
                data.styleOverride = textStyle(object.value(u"styleOverride"_s).toObject(), join(path, u"styleOverride"_s));
            }
            data.translation = object.value(u"translation"_s).toString();
            data.fields = unknownKeys(object, kClipCommonKeys, kSubtitleClipKeys);
            clip.payload = std::move(data);
            break;
        }
        default:
            // Kinds implemented in later phases: every non-common field is kept verbatim.
            clip.payload = PreservedClipData{*kind, unknownKeys(object, kClipCommonKeys)};
            break;
        }
        return clip;
    }

    Transition transition(const QJsonObject &object, const QString &path)
    {
        Transition transition;
        transition.id = id<TransitionTag>(object, u"id"_s, path, true);
        transition.type = assetRef(object.value(u"type"_s), join(path, u"type"_s), true).value_or(AssetRef{});
        transition.from = id<ClipTag>(object, u"from"_s, path, true);
        transition.to = id<ClipTag>(object, u"to"_s, path, true);
        transition.duration = requiredTime(object, u"duration"_s, path);
        transition.alignment = enumeration(object, u"alignment"_s, path, kAlignments, TransitionAlignment::Center);
        transition.fillMissing = enumeration(object, u"fillMissing"_s, path, kMissingMaterial, MissingMaterial::Freeze);
        transition.audioCrossfade = boolean(object, u"audioCrossfade"_s, path, true);
        transition.params = paramMap(object, u"params"_s, path);
        return transition;
    }

    Track track(const QJsonObject &object, const QString &path)
    {
        Track track;
        track.id = id<TrackTag>(object, u"id"_s, path, true);
        const auto kind = valueOf(kTrackKinds, object.value(u"kind"_s).toString());
        if (!kind) {
            fail(join(path, u"kind"_s), u"unknown track kind"_s);
            return track;
        }
        track.kind = *kind;
        track.name = string(object, u"name"_s, path);
        track.locked = boolean(object, u"locked"_s, path, false);
        track.muted = boolean(object, u"muted"_s, path, false);
        track.solo = boolean(object, u"solo"_s, path, false);
        track.hidden = boolean(object, u"hidden"_s, path, false);
        track.height = number(object, u"height"_s, path, 1.0, 0.25, 8.0);
        track.captions = boolean(object, u"captions"_s, path, false);
        track.gainDb = param(object.value(u"gainDb"_s), join(path, u"gainDb"_s), Param(0.0));
        const QJsonArray clips = array(object, u"clips"_s, path);
        for (qsizetype i = 0; i < clips.size() && !failed(); ++i) {
            track.clips.push_back(clip(clips[i].toObject(), index(join(path, u"clips"_s), i)));
        }
        std::stable_sort(track.clips.begin(), track.clips.end(),
                         [](const Clip &a, const Clip &b) { return a.start < b.start; });
        const QJsonArray transitions = array(object, u"transitions"_s, path);
        for (qsizetype i = 0; i < transitions.size() && !failed(); ++i) {
            track.transitions.push_back(transition(transitions[i].toObject(), index(join(path, u"transitions"_s), i)));
        }
        track.extras = unknownKeys(object, kTrackKeys);
        return track;
    }

    Sequence sequence(const QJsonObject &object, const QString &path)
    {
        Sequence sequence;
        sequence.id = id<SequenceTag>(object, u"id"_s, path, true);
        sequence.name = string(object, u"name"_s, path);
        sequence.canvas = canvas(object, u"canvas"_s, path);
        sequence.magneticMain = boolean(object, u"magneticMain"_s, path, true);
        sequence.defaultBackground = background(object.value(u"defaultBackground"_s), join(path, u"defaultBackground"_s));
        for (int pass = 0; pass < 2; ++pass) {
            const QString key = pass == 0 ? u"visualTracks"_s : u"audioTracks"_s;
            const QJsonArray tracks = array(object, key, path);
            for (qsizetype i = 0; i < tracks.size() && !failed(); ++i) {
                (pass == 0 ? sequence.visualTracks : sequence.audioTracks)
                    .push_back(track(tracks[i].toObject(), index(join(path, key), i)));
            }
        }
        sequence.markers = markers(object, path);
        const QJsonArray groups = array(object, u"groups"_s, path);
        for (qsizetype i = 0; i < groups.size(); ++i) {
            const QJsonObject g = groups[i].toObject();
            const QString gPath = index(join(path, u"groups"_s), i);
            Group group;
            group.id = id<GroupTag>(g, u"id"_s, gPath, true);
            for (const QJsonValue &clipId : g.value(u"clipIds"_s).toArray()) {
                if (auto parsed = ClipId::fromString(clipId.toString())) {
                    group.clipIds.push_back(*parsed);
                } else {
                    warn(gPath, u"invalid clip id ignored"_s);
                }
            }
            sequence.groups.push_back(std::move(group));
        }
        sequence.extras = unknownKeys(object, kSequenceKeys);
        return sequence;
    }

    Media media(const QJsonObject &object, const QString &path)
    {
        Media media;
        media.id = id<MediaTag>(object, u"id"_s, path, true);
        media.kind = enumeration(object, u"kind"_s, path, kMediaKinds, MediaKind::Video);
        media.name = string(object, u"name"_s, path);
        media.path = string(object, u"path"_s, path);
        if (media.path.isEmpty()) {
            fail(join(path, u"path"_s), u"media path expected"_s);
        }
        media.relativePath = string(object, u"relativePath"_s, path);
        const QJsonObject fingerprint = this->object(object, u"fingerprint"_s, path, false);
        const QString fPath = join(path, u"fingerprint"_s);
        media.fingerprint.algorithm = string(fingerprint, u"algo"_s, fPath);
        media.fingerprint.value = string(fingerprint, u"value"_s, fPath);
        media.fingerprint.size = static_cast<std::int64_t>(number(fingerprint, u"size"_s, fPath, 0.0, 0.0, 9.0e15));
        const QJsonObject info = this->object(object, u"info"_s, path, false);
        const QString iPath = join(path, u"info"_s);
        media.info.duration = optionalTime(info, u"duration"_s, iPath);
        if (info.value(u"video"_s).isObject()) {
            const QJsonObject v = info.value(u"video"_s).toObject();
            const QString vPath = join(iPath, u"video"_s);
            VideoStreamInfo video;
            video.width = integer(v, u"width"_s, vPath, 0, 0, 65536);
            video.height = integer(v, u"height"_s, vPath, 0, 0, 65536);
            video.frameRate = rational(v, u"frameRate"_s, vPath, false);
            video.variableFrameRate = boolean(v, u"vfr"_s, vPath, false);
            video.rotation = integer(v, u"rotation"_s, vPath, 0, -360, 360);
            video.codec = string(v, u"codec"_s, vPath);
            video.pixelFormat = string(v, u"pixelFormat"_s, vPath);
            video.primaries = string(v, u"primaries"_s, vPath);
            video.transfer = string(v, u"transfer"_s, vPath);
            video.hdr = boolean(v, u"hdr"_s, vPath, false);
            video.hasAlpha = boolean(v, u"hasAlpha"_s, vPath, false);
            video.sampleAspectRatio = rational(v, u"sar"_s, vPath, false).value_or(Rational(1));
            media.info.video = video;
        }
        if (info.value(u"audio"_s).isObject()) {
            const QJsonObject a = info.value(u"audio"_s).toObject();
            const QString aPath = join(iPath, u"audio"_s);
            media.info.audio = AudioStreamInfo{string(a, u"codec"_s, aPath), integer(a, u"sampleRate"_s, aPath, 0, 0, 1000000),
                                               integer(a, u"channels"_s, aPath, 0, 0, 64)};
        }
        media.proxy = enumeration(object, u"proxy"_s, path, kProxyPolicies, ProxyPolicy::Auto);
        media.favorite = boolean(object, u"favorite"_s, path, false);
        media.folderId = id<FolderTag>(object, u"folderId"_s, path, false);
        return media;
    }

    QDateTime date(const QJsonObject &parent, const QString &key, const QString &path)
    {
        const QString text = string(parent, key, path);
        QDateTime dateTime = QDateTime::fromString(text, Qt::ISODateWithMs);
        if (!dateTime.isValid()) {
            warn(join(path, key), u"invalid date, current time used"_s);
            dateTime = QDateTime::currentDateTimeUtc();
        }
        return dateTime.toUTC();
    }

    ProjectData project(const QJsonObject &object)
    {
        ProjectData project;
        project.id = id<ProjectTag>(object, u"id"_s, {}, true);
        project.name = string(object, u"name"_s, {});
        project.createdAt = date(object, u"createdAt"_s, {});
        project.modifiedAt = date(object, u"modifiedAt"_s, {});

        const QJsonObject settings = this->object(object, u"settings"_s, {});
        project.settings.frameRate = rational(settings, u"frameRate"_s, u"settings"_s, true).value_or(Rational(30));
        project.settings.sampleRate = integer(settings, u"sampleRate"_s, u"settings"_s, 48000, 8000, 384000);
        project.settings.audioChannels = integer(settings, u"audioChannels"_s, u"settings"_s, 2, 1, 32);
        project.settings.colorSpace = string(settings, u"colorSpace"_s, u"settings"_s, u"bt709"_s);
        if (project.settings.colorSpace != u"bt709"_s) {
            warn(u"settings.colorSpace"_s, u"unsupported color space, bt709 used"_s);
            project.settings.colorSpace = u"bt709"_s;
        }
        project.settings.formatFromFirstClip = boolean(settings, u"formatFromFirstClip"_s, u"settings"_s, true);
        project.settings.defaultCanvas = canvas(settings, u"defaultCanvas"_s, u"settings"_s);

        const QJsonArray folders = array(object, u"mediaFolders"_s, {});
        for (qsizetype i = 0; i < folders.size(); ++i) {
            const QJsonObject f = folders[i].toObject();
            const QString fPath = index(u"mediaFolders"_s, i);
            project.mediaFolders.push_back(MediaFolder{id<FolderTag>(f, u"id"_s, fPath, true), string(f, u"name"_s, fPath),
                                                       id<FolderTag>(f, u"parentId"_s, fPath, false)});
        }
        const QJsonArray media = array(object, u"media"_s, {});
        for (qsizetype i = 0; i < media.size() && !failed(); ++i) {
            project.media.push_back(this->media(media[i].toObject(), index(u"media"_s, i)));
        }
        const QJsonArray sequences = array(object, u"sequences"_s, {});
        for (qsizetype i = 0; i < sequences.size() && !failed(); ++i) {
            project.sequences.push_back(sequence(sequences[i].toObject(), index(u"sequences"_s, i)));
        }
        project.mainSequenceId = id<SequenceTag>(object, u"mainSequenceId"_s, {}, true);
        project.extras = unknownKeys(object, kProjectKeys);
        return project;
    }
};

// Repairs the violations that can be fixed without guessing (docs/FILE_FORMAT.md §9.4).
void repair(ProjectData &project, QStringList &warnings)
{
    for (Sequence &sequence : project.sequences) {
        QSet<ClipId> clipIds;
        for (int pass = 0; pass < 2; ++pass) {
            for (Track &track : pass == 0 ? sequence.visualTracks : sequence.audioTracks) {
                for (const Clip &clip : track.clips) {
                    clipIds.insert(clip.id);
                }
                const auto before = track.transitions.size();
                std::erase_if(track.transitions, [&track](const Transition &t) {
                    const int from = track.clipIndex(t.from);
                    const int to = track.clipIndex(t.to);
                    return from < 0 || to != from + 1;
                });
                if (track.transitions.size() != before) {
                    warnings.append(u"track "_s + track.id.toString() + u": invalid transitions removed"_s);
                }
            }
        }
        for (Group &group : sequence.groups) {
            std::erase_if(group.clipIds, [&clipIds](const ClipId &id) { return !clipIds.contains(id); });
        }
        const auto groupsBefore = sequence.groups.size();
        std::erase_if(sequence.groups, [](const Group &group) { return group.clipIds.size() < 2; });
        if (sequence.groups.size() != groupsBefore) {
            warnings.append(u"sequence "_s + sequence.id.toString() + u": invalid groups removed"_s);
        }
    }
}

QString damaged(const QString &detail)
{
    return QCoreApplication::translate("velacut::projectjson", "The project file is damaged (%1).").arg(detail);
}

} // namespace

QJsonObject toJson(const ProjectData &project)
{
    QJsonObject settings{{u"frameRate"_s, project.settings.frameRate.toString()},
                         {u"sampleRate"_s, project.settings.sampleRate},
                         {u"audioChannels"_s, project.settings.audioChannels},
                         {u"colorSpace"_s, project.settings.colorSpace},
                         {u"formatFromFirstClip"_s, project.settings.formatFromFirstClip},
                         {u"defaultCanvas"_s, canvasJson(project.settings.defaultCanvas)}};
    QJsonObject object{
        {u"format"_s, QString(kProjectFormatName)},
        {u"formatVersion"_s, kProjectFormatVersion},
        {u"generator"_s, QJsonObject{{u"app"_s, u"velacut"_s}, {u"version"_s, QCoreApplication::applicationVersion()}}},
        {u"id"_s, idValue(project.id)},
        {u"name"_s, project.name},
        {u"createdAt"_s, isoDate(project.createdAt)},
        {u"modifiedAt"_s, isoDate(project.modifiedAt)},
        {u"settings"_s, settings},
        {u"mediaFolders"_s, arrayOf(project.mediaFolders,
                                    [](const MediaFolder &folder) {
                                        return QJsonObject{{u"id"_s, idValue(folder.id)},
                                                           {u"name"_s, folder.name},
                                                           {u"parentId"_s, idValue(folder.parentId)}};
                                    })},
        {u"media"_s, arrayOf(project.media, mediaJson)},
        {u"sequences"_s, arrayOf(project.sequences, sequenceJson)},
        {u"mainSequenceId"_s, idValue(project.mainSequenceId)}};
    mergeInto(object, project.extras);
    return object;
}

QJsonObject textStyleToJson(const TextStyle &style)
{
    return textStyleJson(style);
}

TextStyle textStyleFromJson(const QJsonObject &json)
{
    Reader reader;
    return reader.textStyle(json, u"style"_s);
}

QJsonObject textAnimationToJson(const TextAnimation &animation)
{
    return ::velacut::projectjson::textAnimationJson(animation);
}

std::optional<TextAnimation> textAnimationFromJson(const QJsonObject &json)
{
    Reader reader;
    return reader.textAnimation(json, u"animation"_s);
}

CaptionStyle captionStyleFromJson(const QJsonObject &json)
{
    CaptionStyle style;
    style.text = textStyleFromJson(json.value(u"style"_s).toObject());
    style.preset = json.value(u"preset"_s).toString();
    style.maxWordsPerLine = std::clamp(json.value(u"maxWordsPerLine"_s).toInt(0), 0, 20);
    style.position = std::clamp(json.value(u"position"_s).toDouble(0.32), -0.5, 0.5);
    style.highlight = valueOf(kCaptionHighlights, json.value(u"highlight"_s).toString()).value_or(CaptionHighlight::None);
    if (const auto color = Color::fromString(json.value(u"highlightColor"_s).toString())) {
        style.highlightColor = *color;
    }
    style.animation = valueOf(kCaptionAnimations, json.value(u"animation"_s).toString()).value_or(CaptionAnimation::None);
    style.uppercase = json.value(u"uppercase"_s).toBool(false);
    return style;
}

QJsonObject captionStyleToJson(const CaptionStyle &style)
{
    return {{u"style"_s, textStyleToJson(style.text)},
            {u"preset"_s, style.preset},
            {u"maxWordsPerLine"_s, style.maxWordsPerLine},
            {u"position"_s, style.position},
            {u"highlight"_s, nameOf(kCaptionHighlights, style.highlight)},
            {u"highlightColor"_s, style.highlightColor.toString()},
            {u"animation"_s, nameOf(kCaptionAnimations, style.animation)},
            {u"uppercase"_s, style.uppercase}};
}

CaptionStyle captionStyleOf(const Track &track)
{
    const QJsonValue value = track.extras.value(u"captionStyle"_s);
    if (value.isObject()) {
        return captionStyleFromJson(value.toObject());
    }
    // Default: readable white text with an outline, low in the picture (SPEC 0bis rule 6).
    CaptionStyle style;
    style.text.size = Param(0.055);
    style.text.stroke = TextStroke{};
    return style;
}

void setCaptionStyle(Track &track, const CaptionStyle &style)
{
    track.extras.insert(u"captionStyle"_s, captionStyleToJson(style));
}

GraphicSettings graphicFromJson(const QJsonObject &json)
{
    Reader reader;
    return reader.graphic(json, u"graphic"_s);
}

AudioVisualizerSettings visualizerFromJson(const QJsonObject &json)
{
    Reader reader;
    return reader.visualizer(json, u"visualizer"_s);
}

QJsonObject visualizerToJson(const AudioVisualizerSettings &settings)
{
    return visualizerJson(settings);
}

QJsonObject mediaToJson(const Media &media)
{
    return mediaJson(media);
}

std::optional<Media> mediaFromJson(const QJsonObject &json, QString *error)
{
    Reader reader;
    Media media = reader.media(json, u"media"_s);
    if (reader.failed()) {
        if (error) {
            *error = reader.error;
        }
        return std::nullopt;
    }
    return media;
}

QByteArray toBytes(const ProjectData &project)
{
    return QJsonDocument(toJson(project)).toJson(QJsonDocument::Indented);
}

ProjectLoadResult fromJson(const QJsonObject &json)
{
    ProjectLoadResult result;
    if (json.value(u"format"_s).toString() != kProjectFormatName) {
        result.error = QCoreApplication::translate("velacut::projectjson", "This file is not a velacut project.");
        return result;
    }
    const QJsonValue version = json.value(u"formatVersion"_s);
    if (!version.isDouble() || version.toDouble() != std::floor(version.toDouble())) {
        result.error = damaged(u"formatVersion"_s);
        return result;
    }
    result.sourceFormatVersion = version.toInt();
    migrations::Result migrated = migrations::migrate(json, result.sourceFormatVersion, kProjectFormatVersion);
    if (!migrated.project) {
        result.error = migrated.error;
        return result;
    }
    result.migrated = migrated.migrated;

    Reader reader;
    ProjectData project;
    try {
        project = reader.project(*migrated.project);
    } catch (const std::exception &exception) {
        // Out-of-range values that the model refuses (e.g. absurd rates) end here.
        reader.fail(u"project"_s, QString::fromUtf8(exception.what()));
    }
    result.warnings = reader.warnings;
    if (reader.failed()) {
        result.error = damaged(reader.error);
        return result;
    }
    repair(project, result.warnings);
    const QStringList violations = project.checkInvariants();
    if (!violations.isEmpty()) {
        result.error = damaged(violations.first());
        result.warnings.append(violations);
        return result;
    }
    result.project = std::move(project);
    return result;
}

ProjectLoadResult fromBytes(const QByteArray &bytes)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        ProjectLoadResult result;
        result.error = damaged(parseError.error != QJsonParseError::NoError
                                   ? parseError.errorString() + u" @ "_s + QString::number(parseError.offset)
                                   : u"not a JSON object"_s);
        return result;
    }
    return fromJson(document.object());
}

} // namespace velacut::projectjson
