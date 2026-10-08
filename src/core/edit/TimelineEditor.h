// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/commands/Edit.h"
#include "core/project/Captions.h"
#include "core/project/ProjectData.h"

#include <QString>

#include <functional>
#include <map>
#include <optional>
#include <vector>

namespace velacut {

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
    // Ripple trim from clip start to time (Q) or from time to clip end (W).
    // Subsequent clips on the track (and on magnetic main track) shift to close the gap.
    EditResult rippleTrimClip(const ClipId &clipId, ClipEdge edge, const RationalTime &time);
    EditResult splitClip(const ClipId &clipId, const RationalTime &time);
    // Several cuts in one step ("Split scenes"); times outside the clip are ignored.
    EditResult splitClipAt(const ClipId &clipId, std::vector<RationalTime> times);
    // Several clips split at the same time in one step (S over a multiple selection): one command,
    // one change notification, one projection patch instead of one per clip. Clips that cannot be
    // split at `time` (not covering it, on a locked track, already deleted…) are ignored; an error
    // when none of them can.
    EditResult splitClips(const std::vector<ClipId> &clipIds, const RationalTime &time);
    // Takes stretches of the clip's source out of it ("Remove pauses"): the rest stays together, the following clips
    // of the track move back. Ranges in the media's own time; media clips at a steady speed, played forwards.
    EditResult removeSourceRanges(const ClipId &clipId, const std::vector<std::pair<RationalTime, RationalTime>> &ranges);
    // The same on several clips in one step ("delete these words" across clips), with its undo text.
    EditResult removeSourceRanges(const std::map<ClipId, std::vector<std::pair<RationalTime, RationalTime>>> &ranges,
                                  const QString &text);
    EditResult deleteClips(const std::vector<ClipId> &clipIds);
    // Deletes clips and closes the gaps on their tracks by shifting subsequent clips left.
    EditResult rippleDeleteClips(const std::vector<ClipId> &clipIds);
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
    // Speed ramping curve with variable speed profile across the clip duration.
    EditResult setSpeedCurve(const ClipId &clipId, const SpeedCurve &curve);
    EditResult removeSpeedCurve(const ClipId &clipId);
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
    EditResult setMagneticMain(bool enabled);

    // ---- Phase 3 ----
    EditResult createCompoundClip(const std::vector<ClipId> &clipIds, const QString &name = QString());
    EditResult expandCompoundClip(const ClipId &clipId);
    EditResult insertAdjustment(const RationalTime &position, const RationalTime &duration);
    EditResult addSequenceMarker(const RationalTime &time, const QString &name = QString(),
                                 const QString &color = QStringLiteral("primary"),
                                 const QString &note = QString(), MarkerKind kind = MarkerKind::User);
    EditResult removeSequenceMarker(const MarkerId &markerId);
    // The chapter markers of the sequence (YouTube chapters) replaced by these, in one step.
    EditResult setChapterMarkers(const std::vector<std::pair<RationalTime, QString>> &chapters);
    EditResult updateSequenceMarker(const Marker &marker);
    // Clip markers: `time` in the clip's keyframe time (D-05, core/project/ClipTime.h).
    EditResult addClipMarker(const ClipId &clipId, const RationalTime &time, const QString &name = QString(),
                             const QString &color = QStringLiteral("primary"),
                             const QString &note = QString(), MarkerKind kind = MarkerKind::User);
    EditResult removeClipMarker(const ClipId &clipId, const MarkerId &markerId);
    EditResult updateClipMarker(const ClipId &clipId, const Marker &marker);

    // ---- Phase 5 ----
    // A sticker on a sticker track (created when needed), `duration` long from `position`.
    EditResult insertSticker(const RationalTime &position, StickerClipData sticker, const RationalTime &duration = {});
    // Replaces the clip's beat markers with the beats of its source (seconds of the source, e.g. detected in its
    // audio) that the clip plays.
    EditResult setBeatMarkers(const ClipId &clipId, const std::vector<double> &beatSeconds);
    EditResult setClipAnimations(const ClipId &clipId, const ClipAnimations &animations);
    // A template slot at the end of the main track: a grey clip waiting for media (Clip::placeholder).
    EditResult insertPlaceholder(const Placeholder &placeholder, const RationalTime &duration);
    // "Replace" (SPEC §5.2): the clip shows `mediaId` from its start instead, keeping its place, length and look
    // (transform, effects, masks, animations, transitions). A shorter video shortens the clip (on the main track the
    // rest follows); a photo keeps the length. The clip is no longer a placeholder.
    EditResult replaceClipMedia(const ClipId &clipId, const MediaId &mediaId);

    // ---- Phase 6 ----
    // Caption lines (from a subtitle file, a transcription or a script; absolute times) on the caption track: with
    // `replace` its lines are replaced, otherwise they join it when they fit, else they go on a new caption track on
    // top. A new track gets `style` (default style when empty). Overlapping lines are shortened so that each one
    // ends where the next starts.
    EditResult insertCaptions(const std::vector<captions::CaptionLine> &lines,
                              const std::optional<CaptionStyle> &style = std::nullopt, bool replace = false);
    // A caption line and the next one on its track become one line (from the start of the first to the end of the
    // second, words kept where they are said).
    EditResult mergeCaptionLines(const ClipId &first);
    // Every line of a caption track earlier (negative) or later by `delta`, never before 0 (sync with the speech).
    EditResult shiftCaptions(const TrackId &trackId, const RationalTime &delta);

    // ---- Phase 4 ----
    using AudioOffsetFn = std::function<std::optional<double>(const Clip &ref, const Clip &target)>;
    EditResult alignClipsByAudio(const ClipId &refClipId, const std::vector<ClipId> &targetClipIds,
                                 const AudioOffsetFn &calcOffset);
    EditResult createMulticamClip(const std::vector<ClipId> &clipIds, const QString &name = QString(),
                                  const AudioOffsetFn &calcOffset = nullptr);
    EditResult setMulticamAngle(const ClipId &clipId, int angle);
    EditResult cutAndSwitchAngle(const ClipId &clipId, int angle, const RationalTime &time);

    // Duration of a new text and of a freeze frame (SPEC 0bis rule 6).
    static constexpr int kDefaultTextSeconds = 3;

    // Duration of an image when inserted, with a slow zoom over the whole clip (SPEC 0bis, rule 6): the "Ken Burns"
    // loop animation of the core pack, without a duration (it spans the clip).
    static constexpr int kDefaultImageSeconds = 3;
    static ClipAnimation kenBurns();

    // Duration and size (share of the canvas) of a new sticker.
    static constexpr int kDefaultStickerSeconds = 3;
    static constexpr double kDefaultStickerScale = 0.35;

private:
    EditResult fail(const QString &message) const;
    // Splits a clip of `modified` at `time` (no checks of the sequence); an error for the user, empty on success.
    QString splitIn(Sequence &modified, const ClipId &clipId, const RationalTime &time, ClipId *secondId) const;
    QString removeRangesIn(Sequence &modified, const ClipId &clipId,
                           const std::vector<std::pair<RationalTime, RationalTime>> &ranges, ClipId *firstKept) const;
    EditResult finish(Sequence &&modified, const QString &text, const ClipId &primary) const;

    const ProjectData &m_project;
    const Sequence *m_sequence = nullptr;
    Rational m_rate;
};

} // namespace velacut
