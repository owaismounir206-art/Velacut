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

struct CompoundClipData
{
    SequenceId sequenceId;
    RationalTime sourceIn;

    friend bool operator==(const CompoundClipData &, const CompoundClipData &) = default;
};

// Payload of clip kinds whose editing features arrive in later phases (text, subtitle, sticker,
// effect, adjustment): the kind-specific JSON fields are kept verbatim and written back unchanged,
// so no data is ever lost (docs/FILE_FORMAT.md §1). Each kind gets a typed struct when implemented.
struct PreservedClipData
{
    ClipKind kind = ClipKind::Text;
    QJsonObject fields;

    friend bool operator==(const PreservedClipData &, const PreservedClipData &) = default;
};

using ClipPayload = std::variant<MediaClipData, ColorClipData, CompoundClipData, PreservedClipData>;

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
    std::vector<Effect> effects;
    std::vector<Marker> markers;
    ClipPayload payload = MediaClipData{};
    // Common fields not yet interpreted by this version (masks, animations, background, transitionIn/Out…),
    // preserved verbatim.
    QJsonObject extras;

    ClipKind kind() const;
    RationalTime end() const { return start + duration; }
    TimeRange range() const { return TimeRange(start, duration); }

    const MediaClipData *media() const { return std::get_if<MediaClipData>(&payload); }
    MediaClipData *media() { return std::get_if<MediaClipData>(&payload); }

    friend bool operator==(const Clip &, const Clip &) = default;
};

} // namespace vedit
