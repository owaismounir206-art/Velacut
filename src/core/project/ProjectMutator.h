// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/project/Project.h"

#include <vector>

namespace velacut {

// The only way to modify a Project. A mutator is a transaction: primitives record what they touch
// into a ChangeSet, and when the outermost mutator is destroyed the project emits changed() once.
// Primitives throw std::logic_error when their target does not exist: edits are always computed from
// the current state, so a missing target is a programming error, never a user error.
class ProjectMutator
{
public:
    explicit ProjectMutator(Project &project);
    ~ProjectMutator();
    ProjectMutator(const ProjectMutator &) = delete;
    ProjectMutator &operator=(const ProjectMutator &) = delete;

    const ProjectData &data() const noexcept { return m_project.m_data; }

    // Project level
    void setName(const QString &name);
    void setSettings(const ProjectSettings &settings);

    // Media pool
    void insertMedia(int index, Media media);
    Media removeMedia(const MediaId &mediaId);
    void replaceMedia(const Media &media);

    // Sequences
    void insertSequence(int index, Sequence sequence);
    Sequence removeSequence(const SequenceId &sequenceId);
    void replaceSequence(const Sequence &sequence);
    void setCanvas(const SequenceId &sequenceId, const Canvas &canvas);
    void setDefaultBackground(const SequenceId &sequenceId, const std::optional<CanvasBackground> &background);
    void setSequenceMarkers(const SequenceId &sequenceId, std::vector<Marker> markers);
    void setGroups(const SequenceId &sequenceId, std::vector<Group> groups);

    // Tracks (index into visualTracks or audioTracks)
    void insertTrack(const SequenceId &sequenceId, bool audioTrack, int index, Track track);
    Track removeTrack(const TrackId &trackId);
    // Replaces every track property except clips and transitions.
    void setTrackProperties(const Track &properties);

    // Clips (kept sorted by start)
    void insertClip(const TrackId &trackId, Clip clip);
    Clip removeClip(const ClipId &clipId);
    void replaceClip(const Clip &clip);
    void shiftClips(const TrackId &trackId, const std::vector<ClipId> &clipIds, const RationalTime &delta);

    // Transitions
    void insertTransition(const TrackId &trackId, Transition transition);
    Transition removeTransition(const TrackId &trackId, const TransitionId &transitionId);
    void replaceTransition(const TrackId &trackId, const Transition &transition);

private:
    Sequence &sequence(const SequenceId &sequenceId);
    Track &track(const TrackId &trackId, SequenceId *owner = nullptr);
    static void sortClips(Track &track);
    static void sortTransitions(Track &track);

    Project &m_project;
    ChangeSet &changes() { return m_project.m_pending; }
};

} // namespace velacut
