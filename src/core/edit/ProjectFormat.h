// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/edit/TimelineEditor.h"

#include <optional>

namespace vedit {

// Canvas showing a media item as it is displayed (rotation and pixel aspect applied), with the preset of its
// ratio; at most 4K (3840 on the long side, 2160 on the short one). None for audio.
std::optional<Canvas> canvasForMedia(const Media &media);

// The standard frame rate nearest to the media's (29.99 → 30, 29.9 → 29.97); slow motion at 120/240 fps → 60.
// None for audio and images.
std::optional<Rational> frameRateForMedia(const Media &media);

// Inserts a media item; when the sequence is still empty, the project frame rate and the canvas first follow that
// media, in the same command (SPEC 0bis rule 1: no questions on "New project").
EditResult insertMediaAdoptingFormat(const ProjectData &project, const SequenceId &sequenceId, const MediaId &mediaId,
                                     const RationalTime &position, Placement placement = Placement::Auto,
                                     std::optional<TimeRange> sourceRange = std::nullopt);

} // namespace vedit
