// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/effects/Effect.h"
#include "core/project/Clip.h"
#include "core/project/Id.h"

#include <QJsonObject>
#include <QString>

#include <map>
#include <vector>

namespace velacut {

enum class TrackKind
{
    Video,
    Text,
    Sticker,
    Effect,
    Adjustment,
    Audio,
};

bool isVisualTrackKind(TrackKind kind);

enum class TransitionAlignment
{
    Center,  // centered on the cut, uses material beyond the cut points
    Overlap, // `to` starts `duration` before the end of `from` (the only overlap allowed on a track)
};

enum class MissingMaterial
{
    Freeze, // freeze the first/last frame when handles are too short
    None,   // only valid if the clips have enough material
};

// A transition between two adjacent clips of the same track (docs/FILE_FORMAT.md §5.8).
struct Transition
{
    TransitionId id;
    AssetRef type;
    ClipId from;
    ClipId to;
    RationalTime duration;
    TransitionAlignment alignment = TransitionAlignment::Center;
    MissingMaterial fillMissing = MissingMaterial::Freeze;
    bool audioCrossfade = true;
    std::map<QString, Param> params;

    friend bool operator==(const Transition &, const Transition &) = default;
};

struct Track
{
    TrackId id;
    TrackKind kind = TrackKind::Video;
    QString name;
    bool locked = false;
    bool muted = false;
    bool solo = false;
    bool hidden = false;
    double height = 1.0;
    bool captions = false;
    Param gainDb{0.0}; // volume of the whole track (mixer), dB
    std::vector<Clip> clips; // sorted by start
    std::vector<Transition> transitions;
    QJsonObject extras; // e.g. captionStyle, preserved until typed

    const Clip *findClip(const ClipId &clipId) const;
    int clipIndex(const ClipId &clipId) const; // -1 if absent

    friend bool operator==(const Track &, const Track &) = default;
};

} // namespace velacut
