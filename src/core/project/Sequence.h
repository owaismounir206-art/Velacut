// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/project/Clip.h"
#include "core/project/Id.h"
#include "core/project/Track.h"

#include <QJsonObject>
#include <QString>

#include <vector>

namespace velacut {

enum class CanvasPreset
{
    Landscape16x9,
    Portrait9x16,
    Square1x1,
    Portrait4x5,
    Cinema21x9,
    Portrait3x4,
    Custom,
};

struct Canvas
{
    int width = 1920;
    int height = 1080;
    CanvasPreset preset = CanvasPreset::Landscape16x9;

    friend bool operator==(const Canvas &, const Canvas &) = default;
};

struct Group
{
    GroupId id;
    std::vector<ClipId> clipIds;

    friend bool operator==(const Group &, const Group &) = default;
};

struct Sequence
{
    SequenceId id;
    QString name;
    Canvas canvas;
    bool magneticMain = true;
    std::optional<CanvasBackground> defaultBackground; // none = black
    std::vector<Track> visualTracks; // [0] = main track, then upwards (composition order)
    std::vector<Track> audioTracks;
    std::vector<Marker> markers;
    std::vector<Group> groups;
    QJsonObject extras; // fields of later versions, preserved

    // End of the last clip on any track (the sequence duration is derived, never stored).
    RationalTime duration(const Rational &rate) const;

    friend bool operator==(const Sequence &, const Sequence &) = default;
};

} // namespace velacut
