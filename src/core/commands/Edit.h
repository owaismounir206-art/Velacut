// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/project/ProjectMutator.h"

#include <QString>

#include <functional>
#include <memory>
#include <vector>

namespace vedit {

// A reversible primitive change. Commands are ordered lists of edits (EditScript): redo applies them
// in order, undo reverts them in reverse order. Edits store only what they change (by id), so undo
// history stays small even for large projects.
class Edit
{
public:
    virtual ~Edit() = default;
    virtual void apply(ProjectMutator &mutator) = 0;
    virtual void revert(ProjectMutator &mutator) = 0;
    // Merging of continuous gestures (e.g. slider drag): if `next` is applied right after this edit and
    // changes the same value, this edit can absorb it and the pair becomes a single before -> after change.
    virtual bool canAbsorb(const Edit &next) const
    {
        Q_UNUSED(next);
        return false;
    }
    // Precondition: canAbsorb(next). `next` must not be used afterwards.
    virtual void absorb(Edit &next) { Q_UNUSED(next); }
};

using EditScript = std::vector<std::unique_ptr<Edit>>;

// Factories for every primitive edit.
namespace edits {

std::unique_ptr<Edit> setName(QString before, QString after);
std::unique_ptr<Edit> setSettings(ProjectSettings before, ProjectSettings after);

std::unique_ptr<Edit> insertMedia(int index, Media media);
std::unique_ptr<Edit> removeMedia(int index, Media media);
std::unique_ptr<Edit> replaceMedia(Media before, Media after);

std::unique_ptr<Edit> setCanvas(SequenceId sequenceId, Canvas before, Canvas after);
std::unique_ptr<Edit> setDefaultBackground(SequenceId sequenceId, std::optional<CanvasBackground> before,
                                           std::optional<CanvasBackground> after);
std::unique_ptr<Edit> setSequenceMarkers(SequenceId sequenceId, std::vector<Marker> before, std::vector<Marker> after);
std::unique_ptr<Edit> setGroups(SequenceId sequenceId, std::vector<Group> before, std::vector<Group> after);

std::unique_ptr<Edit> insertTrack(SequenceId sequenceId, bool audioTrack, int index, Track track);
std::unique_ptr<Edit> removeTrack(SequenceId sequenceId, bool audioTrack, int index, Track track);
std::unique_ptr<Edit> setTrackProperties(Track before, Track after);

std::unique_ptr<Edit> insertClip(TrackId trackId, Clip clip);
std::unique_ptr<Edit> removeClip(TrackId trackId, Clip clip);
std::unique_ptr<Edit> replaceClip(Clip before, Clip after);
std::unique_ptr<Edit> shiftClips(TrackId trackId, std::vector<ClipId> clipIds, RationalTime delta);

std::unique_ptr<Edit> insertTransition(TrackId trackId, Transition transition);
std::unique_ptr<Edit> removeTransition(TrackId trackId, Transition transition);
std::unique_ptr<Edit> replaceTransition(TrackId trackId, Transition before, Transition after);

} // namespace edits

} // namespace vedit
