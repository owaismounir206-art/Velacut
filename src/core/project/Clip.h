// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/effects/Effect.h"
#include "core/effects/Param.h"
#include "core/project/Id.h"
#include "core/time/RationalTime.h"

#include <QJsonObject>
#include <QString>

#include <optional>
#include <utility>
#include <variant>
#include <vector>

namespace vedit {

enum class ClipKind
{
    Media,
    Text,
    Subtitle,
    Sticker,
    Color,
    Effect,
    Adjustment,
    Compound,
};

enum class BlendMode
{
    Normal,
    Lighten,
    Screen,
    Multiply,
    Overlay,
    SoftLight,
    HardLight,
    Difference,
    Darken,
    Color,
    Luminosity,
    Add,
    ColorDodge,
    ColorBurn,
    Exclusion,
    Hue,
    Saturation,
};

enum class FitMode
{
    Contain,
    Cover,
    Stretch,
    None,
};

// Canvas coordinates: origin at the canvas center, x in canvas widths, y in canvas heights (y down).
// Scale 1.0 = fitted according to `fit`. Angles in degrees, clockwise. docs/FILE_FORMAT.md §3.4 and §5.4.
struct Crop
{
    Param left{0.0};
    Param top{0.0};
    Param right{0.0};
    Param bottom{0.0};

    friend bool operator==(const Crop &, const Crop &) = default;
};

struct Transform
{
    Param position{Vec2{0.0, 0.0}};
    Param scale{Vec2{1.0, 1.0}};
    bool uniformScale = true;
    Param rotation{0.0};
    bool flipH = false;
    bool flipV = false;
    Crop crop;
    FitMode fit = FitMode::Contain;

    friend bool operator==(const Transform &, const Transform &) = default;
};

enum class MaskShape
{
    Linear,
    Mirror,
    Circle,
    Rectangle,
    Heart,
    Star,
    Path,
};

struct MaskPoint
{
    Vec2 p;
    Vec2 in;
    Vec2 out;

    friend bool operator==(const MaskPoint &, const MaskPoint &) = default;
};

struct Mask
{
    MaskId id;
    MaskShape shape = MaskShape::Rectangle;
    Param center{Vec2{0.0, 0.0}};
    Param size{Vec2{0.5, 0.5}};
    Param rotation{0.0};
    Param roundness{0.0};
    Param feather{0.0};
    bool invert = false;
    std::vector<MaskPoint> points;

    friend bool operator==(const Mask &, const Mask &) = default;
};

struct ClipAnimation
{
    AssetRef type;
    RationalTime duration;
    Easing easing = Easing::preset(Easing::Preset::EaseInOut);
    QJsonObject params;

    friend bool operator==(const ClipAnimation &, const ClipAnimation &) = default;
};

struct ClipAnimations
{
    std::optional<ClipAnimation> in;
    std::optional<ClipAnimation> out;
    std::optional<ClipAnimation> loop;

    bool isEmpty() const noexcept { return !in && !out && !loop; }
    friend bool operator==(const ClipAnimations &, const ClipAnimations &) = default;
};

enum class MarkerKind
{
    User,
    Beat,
    Chapter,
};

struct Marker
{
    MarkerId id;
    RationalTime time;
    std::optional<RationalTime> duration;
    QString name;
    QString color = QStringLiteral("primary"); // theme role name or "#RRGGBBAA"
    QString note;
    MarkerKind kind = MarkerKind::User;

    friend bool operator==(const Marker &, const Marker &) = default;
};

enum class Streams
{
    AudioVideo,
    VideoOnly,
    AudioOnly,
};

struct ClipAudio
{
    Param gainDb{0.0};
    bool muted = false;
    Param pan{0.0};
    std::optional<RationalTime> fadeIn;
    std::optional<RationalTime> fadeOut;

    friend bool operator==(const ClipAudio &, const ClipAudio &) = default;
};

struct SpeedCurve
{
    QString preset;                              // empty = custom
    std::vector<std::pair<double, double>> points; // (relative position 0..1, speed > 0)

    friend bool operator==(const SpeedCurve &, const SpeedCurve &) = default;
};

struct MediaClipData
{
    MediaId mediaId;
    Streams streams = Streams::AudioVideo;
    // First source frame used, in content time at 1x, on the project frame grid.
    RationalTime sourceIn;
    double speed = 1.0; // constant speed (0.1 .. 100), ignored when `curve` is set
    std::optional<SpeedCurve> curve;
    bool preservePitch = true;
    bool reversed = false;
    ClipAudio audio;

    friend bool operator==(const MediaClipData &, const MediaClipData &) = default;
};

struct ColorClipData
{
    Param color{Color{0, 0, 0, 255}};

    friend bool operator==(const ColorClipData &, const ColorClipData &) = default;
};

// Background of the canvas behind a clip of the main track (docs/FILE_FORMAT.md §5.2, §5.4).
enum class BackgroundType
{
    Color,
    Blur,    // the clip itself, enlarged to cover the canvas and blurred
    Image,   // a picture of the media pool (rendered from a later phase; preserved)
    Pattern, // a library pattern (rendered from a later phase; preserved)
};

struct CanvasBackground
{
    BackgroundType type = BackgroundType::Color;
    Color color{0, 0, 0, 255};
    double amount = 0.6; // blur strength 0…1
    MediaId mediaId;     // Image
    std::optional<AssetRef> pattern;

    friend bool operator==(const CanvasBackground &, const CanvasBackground &) = default;
};

enum class TextAlign
{
    Left,
    Center,
    Right,
};

struct TextStroke
{
    Param color{Color{0, 0, 0, 255}};
    double width = 0.08; // fraction of the font size

    friend bool operator==(const TextStroke &, const TextStroke &) = default;
};

struct TextShadow
{
    Color color{0, 0, 0, 153};
    Vec2 offset{0.0, 0.02}; // fractions of the canvas height
    double blur = 0.03;     // fraction of the font size

    friend bool operator==(const TextShadow &, const TextShadow &) = default;
};

struct TextBackground
{
    Color color{0, 0, 0, 160};
    double padding = 0.25; // fractions of the font size
    double radius = 0.2;

    friend bool operator==(const TextBackground &, const TextBackground &) = default;
};

struct TextStyle
{
    QString fontFamily = QStringLiteral("Inter");
    int fontWeight = 700;
    bool italic = false;
    Param size{0.06}; // fraction of the canvas height
    Param color{Color{255, 255, 255, 255}};
    std::optional<TextStroke> stroke;
    std::optional<TextShadow> shadow;
    std::optional<TextBackground> background;
    Param letterSpacing{0.0}; // fraction of the font size
    double lineHeight = 1.2;
    TextAlign align = TextAlign::Center;
    bool underline = false;
    QJsonObject extras; // gradient and fields of later versions, preserved

    friend bool operator==(const TextStyle &, const TextStyle &) = default;
};

// A text clip (docs/FILE_FORMAT.md §5.5 "text"). Fields of later phases (spans, path, textEffect, tts) are kept in
// `fields` verbatim.
struct TextClipData
{
    QString text;
    TextStyle style;
    std::optional<AssetRef> stylePreset;
    std::optional<double> boxWidth; // fraction of the canvas width; none = automatic
    QJsonObject fields;

    friend bool operator==(const TextClipData &, const TextClipData &) = default;
};

struct CompoundClipData
{
    SequenceId sequenceId;
    RationalTime sourceIn;

    friend bool operator==(const CompoundClipData &, const CompoundClipData &) = default;
};

struct AdjustmentClipData
{
    std::vector<Effect> effects;

    friend bool operator==(const AdjustmentClipData &, const AdjustmentClipData &) = default;
};

// Payload of clip kinds whose editing features arrive in later phases (subtitle, sticker,
// effect): the kind-specific JSON fields are kept verbatim and written back unchanged,
// so no data is ever lost (docs/FILE_FORMAT.md §1). Each kind gets a typed struct when implemented.
struct PreservedClipData
{
    ClipKind kind = ClipKind::Subtitle;
    QJsonObject fields;

    friend bool operator==(const PreservedClipData &, const PreservedClipData &) = default;
};

using ClipPayload = std::variant<MediaClipData, ColorClipData, CompoundClipData, TextClipData, AdjustmentClipData, PreservedClipData>;

struct Clip
{
    ClipId id;
    RationalTime start;
    RationalTime duration;
    QString name;
    bool enabled = true;
    LinkId linkId; // clips sharing a link id move together (audio/video link)
    Transform transform;
    Param opacity{1.0};
    BlendMode blendMode = BlendMode::Normal;
    // Main track only: the canvas behind this clip; none = the sequence default.
    std::optional<CanvasBackground> background;
    std::vector<Effect> effects;
    std::vector<Mask> masks;
    ClipAnimations animations;
    std::vector<Marker> markers;
    ClipPayload payload = MediaClipData{};
    // Common fields not yet interpreted by this version (transitionIn/Out…), preserved verbatim.
    QJsonObject extras;

    ClipKind kind() const;
    RationalTime end() const { return start + duration; }
    TimeRange range() const { return TimeRange(start, duration); }

    const MediaClipData *media() const { return std::get_if<MediaClipData>(&payload); }
    MediaClipData *media() { return std::get_if<MediaClipData>(&payload); }
    const TextClipData *text() const { return std::get_if<TextClipData>(&payload); }
    TextClipData *text() { return std::get_if<TextClipData>(&payload); }
    const AdjustmentClipData *adjustment() const { return std::get_if<AdjustmentClipData>(&payload); }
    AdjustmentClipData *adjustment() { return std::get_if<AdjustmentClipData>(&payload); }

    friend bool operator==(const Clip &, const Clip &) = default;
};

} // namespace vedit
