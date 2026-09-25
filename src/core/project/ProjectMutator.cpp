// SPDX-License-Identifier: GPL-3.0-or-later
#include "ProjectMutator.h"

#include <algorithm>
#include <stdexcept>

namespace vedit {

Project::Project(ProjectData data, QObject *parent)
    : QObject(parent)
    , m_data(std::move(data))
{
}

ProjectMutator::ProjectMutator(Project &project)
    : m_project(project)
{
    ++m_project.m_transactionDepth;
}

ProjectMutator::~ProjectMutator()
{
    if (--m_project.m_transactionDepth == 0 && !m_project.m_pending.isEmpty()) {
        const ChangeSet pending = std::move(m_project.m_pending);
        m_project.m_pending = {};
        emit m_project.changed(pending);
    }
}

void ProjectMutator::setName(const QString &name)
{
    m_project.m_data.name = name;
    changes().projectChanged = true;
}

void ProjectMutator::setSettings(const ProjectSettings &settings)
{
    m_project.m_data.settings = settings;
    changes().settingsChanged = true;
}

void ProjectMutator::insertMedia(int index, Media media)
{
    auto &pool = m_project.m_data.media;
    if (index < 0 || index > static_cast<int>(pool.size())) {
        throw std::logic_error("ProjectMutator::insertMedia: index out of range");
    }
    changes().media.insert(media.id);
    pool.insert(pool.begin() + index, std::move(media));
}

Media ProjectMutator::removeMedia(const MediaId &mediaId)
{
    auto &pool = m_project.m_data.media;
    const int index = m_project.m_data.mediaIndex(mediaId);
    if (index < 0) {
        throw std::logic_error("ProjectMutator::removeMedia: unknown media");
    }
    Media removed = std::move(pool[static_cast<size_t>(index)]);
    pool.erase(pool.begin() + index);
    changes().media.insert(mediaId);
    return removed;
}

void ProjectMutator::replaceMedia(const Media &media)
{
    const int index = m_project.m_data.mediaIndex(media.id);
    if (index < 0) {
        throw std::logic_error("ProjectMutator::replaceMedia: unknown media");
    }
    m_project.m_data.media[static_cast<size_t>(index)] = media;
    changes().media.insert(media.id);
}

void ProjectMutator::insertSequence(int index, Sequence seq)
{
    auto &seqs = m_project.m_data.sequences;
    const size_t pos = static_cast<size_t>(std::clamp(index, 0, static_cast<int>(seqs.size())));
    const SequenceId id = seq.id;
    seqs.insert(seqs.begin() + pos, std::move(seq));
    changes().sequences.insert(id);
}

Sequence ProjectMutator::removeSequence(const SequenceId &sequenceId)
{
    auto &seqs = m_project.m_data.sequences;
    const int index = m_project.m_data.sequenceIndex(sequenceId);
    if (index < 0) {
        throw std::logic_error("ProjectMutator::removeSequence: unknown sequence");
    }
    Sequence removed = std::move(seqs[static_cast<size_t>(index)]);
    seqs.erase(seqs.begin() + index);
    changes().sequences.insert(sequenceId);
    return removed;
}

void ProjectMutator::replaceSequence(const Sequence &seq)
{
    const int index = m_project.m_data.sequenceIndex(seq.id);
    if (index < 0) {
        throw std::logic_error("ProjectMutator::replaceSequence: unknown sequence");
    }
    m_project.m_data.sequences[static_cast<size_t>(index)] = seq;
    changes().sequences.insert(seq.id);
}

Sequence &ProjectMutator::sequence(const SequenceId &sequenceId)
{
    const int index = m_project.m_data.sequenceIndex(sequenceId);
    if (index < 0) {
        throw std::logic_error("ProjectMutator: unknown sequence");
    }
    return m_project.m_data.sequences[static_cast<size_t>(index)];
}

Track &ProjectMutator::track(const TrackId &trackId, SequenceId *owner)
{
    const TrackLocation location = m_project.m_data.locateTrack(trackId);
    if (!location.isValid()) {
        throw std::logic_error("ProjectMutator: unknown track");
    }
    Sequence &seq = m_project.m_data.sequences[static_cast<size_t>(location.sequenceIndex)];
    if (owner) {
        *owner = seq.id;
    }
    auto &tracks = location.audioTrack ? seq.audioTracks : seq.visualTracks;
    return tracks[static_cast<size_t>(location.trackIndex)];
}

void ProjectMutator::setCanvas(const SequenceId &sequenceId, const Canvas &canvas)
{
    sequence(sequenceId).canvas = canvas;
    changes().sequences.insert(sequenceId);
}

void ProjectMutator::setDefaultBackground(const SequenceId &sequenceId, const std::optional<CanvasBackground> &background)
{
    Sequence &target = sequence(sequenceId);
    target.defaultBackground = background;
    changes().sequences.insert(sequenceId);
    // Every main-track clip without its own background shows it.
    if (!target.visualTracks.empty()) {
        changes().tracks.insert(target.visualTracks.front().id);
    }
}

void ProjectMutator::setSequenceMarkers(const SequenceId &sequenceId, std::vector<Marker> markers)
{
    sequence(sequenceId).markers = std::move(markers);
    changes().sequences.insert(sequenceId);
}

void ProjectMutator::setGroups(const SequenceId &sequenceId, std::vector<Group> groups)
{
    sequence(sequenceId).groups = std::move(groups);
    changes().sequences.insert(sequenceId);
}

void ProjectMutator::insertTrack(const SequenceId &sequenceId, bool audioTrack, int index, Track newTrack)
{
    Sequence &seq = sequence(sequenceId);
    auto &tracks = audioTrack ? seq.audioTracks : seq.visualTracks;
    if (index < 0 || index > static_cast<int>(tracks.size())) {
        throw std::logic_error("ProjectMutator::insertTrack: index out of range");
    }
    changes().sequences.insert(sequenceId);
    changes().tracks.insert(newTrack.id);
    for (const Clip &clip : newTrack.clips) {
        changes().clips.insert(clip.id);
    }
    tracks.insert(tracks.begin() + index, std::move(newTrack));
}

Track ProjectMutator::removeTrack(const TrackId &trackId)
{
    const TrackLocation location = m_project.m_data.locateTrack(trackId);
    if (!location.isValid()) {
        throw std::logic_error("ProjectMutator::removeTrack: unknown track");
    }
    Sequence &seq = m_project.m_data.sequences[static_cast<size_t>(location.sequenceIndex)];
    auto &tracks = location.audioTrack ? seq.audioTracks : seq.visualTracks;
    Track removed = std::move(tracks[static_cast<size_t>(location.trackIndex)]);
    tracks.erase(tracks.begin() + location.trackIndex);
    changes().sequences.insert(seq.id);
    changes().tracks.insert(trackId);
    for (const Clip &clip : removed.clips) {
        changes().clips.insert(clip.id);
    }
    return removed;
}

void ProjectMutator::setTrackProperties(const Track &properties)
{
    Track &target = track(properties.id);
    target.kind = properties.kind;
    target.name = properties.name;
    target.locked = properties.locked;
    target.muted = properties.muted;
    target.solo = properties.solo;
    target.hidden = properties.hidden;
    target.height = properties.height;
    target.captions = properties.captions;
    target.gainDb = properties.gainDb;
    target.extras = properties.extras;
    changes().tracks.insert(properties.id);
}

void ProjectMutator::sortClips(Track &target)
{
    std::stable_sort(target.clips.begin(), target.clips.end(),
                     [](const Clip &a, const Clip &b) { return a.start < b.start; });
    sortTransitions(target);
}

void ProjectMutator::sortTransitions(Track &target)
{
    // Canonical order: by position of the `from` clip, so undo/redo always restores identical data.
    std::stable_sort(target.transitions.begin(), target.transitions.end(),
                     [&target](const Transition &a, const Transition &b) {
                         return target.clipIndex(a.from) < target.clipIndex(b.from);
                     });
}

void ProjectMutator::insertClip(const TrackId &trackId, Clip clip)
{
    Track &target = track(trackId);
    changes().tracks.insert(trackId);
    changes().clips.insert(clip.id);
    const auto position = std::upper_bound(target.clips.begin(), target.clips.end(), clip.start,
                                           [](const RationalTime &t, const Clip &c) { return t < c.start; });
    target.clips.insert(position, std::move(clip));
}

Clip ProjectMutator::removeClip(const ClipId &clipId)
{
    const ClipLocation location = m_project.m_data.locateClip(clipId);
    if (!location.isValid()) {
        throw std::logic_error("ProjectMutator::removeClip: unknown clip");
    }
    Sequence &seq = m_project.m_data.sequences[static_cast<size_t>(location.sequenceIndex)];
    Track &owner = (location.audioTrack ? seq.audioTracks : seq.visualTracks)[static_cast<size_t>(location.trackIndex)];
    Clip removed = std::move(owner.clips[static_cast<size_t>(location.clipIndex)]);
    owner.clips.erase(owner.clips.begin() + location.clipIndex);
    changes().tracks.insert(owner.id);
    changes().clips.insert(clipId);
    return removed;
}

void ProjectMutator::replaceClip(const Clip &clip)
{
    const ClipLocation location = m_project.m_data.locateClip(clip.id);
    if (!location.isValid()) {
        throw std::logic_error("ProjectMutator::replaceClip: unknown clip");
    }
    Sequence &seq = m_project.m_data.sequences[static_cast<size_t>(location.sequenceIndex)];
    Track &owner = (location.audioTrack ? seq.audioTracks : seq.visualTracks)[static_cast<size_t>(location.trackIndex)];
    Clip &target = owner.clips[static_cast<size_t>(location.clipIndex)];
    const bool moved = target.start != clip.start;
    target = clip;
    if (moved) {
        sortClips(owner);
    }
    changes().tracks.insert(owner.id);
    changes().clips.insert(clip.id);
}

void ProjectMutator::shiftClips(const TrackId &trackId, const std::vector<ClipId> &clipIds, const RationalTime &delta)
{
    Track &target = track(trackId);
    for (const ClipId &clipId : clipIds) {
        const int index = target.clipIndex(clipId);
        if (index < 0) {
            throw std::logic_error("ProjectMutator::shiftClips: clip not on track");
        }
        Clip &clip = target.clips[static_cast<size_t>(index)];
        clip.start = clip.start + delta;
        changes().clips.insert(clipId);
    }
    sortClips(target);
    changes().tracks.insert(trackId);
}

void ProjectMutator::insertTransition(const TrackId &trackId, Transition transition)
{
    Track &target = track(trackId);
    changes().tracks.insert(trackId);
    target.transitions.push_back(std::move(transition));
    sortTransitions(target);
}

Transition ProjectMutator::removeTransition(const TrackId &trackId, const TransitionId &transitionId)
{
    Track &target = track(trackId);
    const auto it = std::find_if(target.transitions.begin(), target.transitions.end(),
                                 [&](const Transition &t) { return t.id == transitionId; });
    if (it == target.transitions.end()) {
        throw std::logic_error("ProjectMutator::removeTransition: unknown transition");
    }
    Transition removed = std::move(*it);
    target.transitions.erase(it);
    changes().tracks.insert(trackId);
    return removed;
}

void ProjectMutator::replaceTransition(const TrackId &trackId, const Transition &transition)
{
    Track &target = track(trackId);
    const auto it = std::find_if(target.transitions.begin(), target.transitions.end(),
                                 [&](const Transition &t) { return t.id == transition.id; });
    if (it == target.transitions.end()) {
        throw std::logic_error("ProjectMutator::replaceTransition: unknown transition");
    }
    *it = transition;
    changes().tracks.insert(trackId);
}

} // namespace vedit
