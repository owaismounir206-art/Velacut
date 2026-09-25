// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/commands/Edit.h"
#include "core/project/ProjectData.h"

#include <QString>

#include <functional>
#include <map>
#include <optional>
#include <vector>

namespace vedit {

// Result of a timeline operation: a ready-to-push script with its (translated) undo text, or an error
// message for the user when the operation is not possible (then the script is empty).
struct EditResult
{
    EditScript script;
    QString text;
    QString error;
    ClipId primaryClip; // clip to select after the operation (new or moved clip), if any

    bool ok() const { return error.isEmpty(); }
};

enum class Placement
{
    Auto,    // video and images on the main track, audio on an audio track
    Overlay, // on a track above the main one (picture in picture)
};

enum class ClipEdge
{
    Start,
    End,
};

// The editing rules of the timeline, "like CapCut" (docs/ARCHITECTURE.md §4.5), as pure functions:
// each operation reads the current project, simulates the change on a copy of the sequence and returns
// the minimal edit script. Rules:
//  - the main track (visualTracks[0]) is magnetic: clips are always packed from 0, no gaps;
//  - a clip dropped over another never overwrites it: a new track is created above;
//  - empty tracks (other than the main one) disappear in the same command;
//  - locked tracks cannot be modified.
class TimelineEditor
{
public:
    TimelineEditor(const ProjectData &project, const SequenceId &sequenceId);

    // Inserts a media item. `sourceRange` selects a part of the media (default: all of it; images 3 s).
    EditResult insertMedia(const MediaId &mediaId, const RationalTime &position,
                           std::optional<TimeRange> sourceRange = std::nullopt, Placement placement = Placement::Auto);
    // Moves a clip to `newStart`, optionally onto another track.
    EditResult moveClip(const ClipId &clipId, const RationalTime &newStart,
                        std::optional<TrackId> targetTrack = std::nullopt);
    // Moves one edge of a clip to `time` (clamped to the available material and to the neighbours).
    EditResult trimClip(const ClipId &clipId, ClipEdge edge, const RationalTime &time);
    EditResult splitClip(const ClipId &clipId, const RationalTime &time);
    EditResult deleteClips(const std::vector<ClipId> &clipIds);
    // Places a copy of each clip right after it (on the main track the following clips move along; elsewhere a new
    // track is created if there is no room). The last copy becomes the primary clip.
    EditResult duplicateClips(const std::vector<ClipId> &clipIds);
    // Moves a clip onto a new track created at `visualIndex` (visual clips, ≥ 1: above the main track) or at
    // `audioIndex` (audio clips): dragging a clip into the free space above or below the tracks.
    EditResult moveClipToNewTrack(const ClipId &clipId, const RationalTime &newStart, int index);

    // ---- Phase 2 ----
    // A text on a text track (created when needed), `duration` long from `position`.
    EditResult insertText(const RationalTime &position, TextClipData text, const RationalTime &duration);
    // Changes clips without moving them (transform, opacity, effects, audio, text, background…). `change` must not
    // alter id, kind, start or duration. One command for all the clips ("Apply to all").
    EditResult updateClips(const std::vector<ClipId> &clipIds, const std::function<void(Clip &)> &change,
                           const QString &text);
    // Constant speed 0.1–100: the clip keeps its source material, so its duration changes (ripple on the main track;
    // elsewhere a new track above if it no longer fits).
    EditResult setSpeed(const ClipId &clipId, double speed);
    // A still of `imageMediaId` (the frame at `time`, prepared by the caller) inserted at `time`, splitting the clip
    // if needed; the rest of the track moves along. The still gets the clip's look (transform, effects…).
    EditResult insertFreezeFrame(const ClipId &clipId, const RationalTime &time, const MediaId &imageMediaId,
                                 const RationalTime &duration);
    // Transitions between a clip and the next adjacent one on its track (centered on the cut, missing material
    // frozen). Adding where one exists replaces it. The duration is limited by the two clips.
    EditResult addTransition(const ClipId &fromClip, const AssetRef &type, const RationalTime &duration,
                             std::map<QString, Param> params = {});
    EditResult updateTransition(const TransitionId &transitionId, const std::function<void(Transition &)> &change);
    EditResult removeTransitions(const std::vector<TransitionId> &transitionIds);
    // Every cut of the track ("Applica a tutte") / no transition left on the track.
    EditResult applyTransitionToAll(const TrackId &trackId, const AssetRef &type, const RationalTime &duration,
                                    std::map<QString, Param> params = {});
    EditResult removeAllTransitions(const TrackId &trackId);
    // Track properties (volume, mute, solo, hidden, locked): `change` must not touch clips or transitions.
    EditResult updateTrack(const TrackId &trackId, const std::function<void(Track &)> &change, const QString &text);
    EditResult setDefaultBackground(const std::optional<CanvasBackground> &background);

    // Duration of a new text and of a freeze frame (SPEC 0bis rule 6).
    static constexpr int kDefaultTextSeconds = 3;

    // Duration of an image when inserted (SPEC 0bis, rule 6).
    static constexpr int kDefaultImageSeconds = 3;

private:
    EditResult fail(const QString &message) const;
    EditResult finish(Sequence &&modified, const QString &text, const ClipId &primary) const;

    const ProjectData &m_project;
    const Sequence *m_sequence = nullptr;
    Rational m_rate;
};

} // namespace vedit
