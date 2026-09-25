// SPDX-License-Identifier: GPL-3.0-or-later
#include "SequenceDiff.h"

#include <QHash>

#include <algorithm>
#include <map>
#include <stdexcept>

namespace vedit {

namespace {

struct IndexedTrack
{
    int index;
    const Track *track;
};

QHash<TrackId, IndexedTrack> indexTracks(const std::vector<Track> &tracks)
{
    QHash<TrackId, IndexedTrack> result;
    for (size_t i = 0; i < tracks.size(); ++i) {
        result.insert(tracks[i].id, {static_cast<int>(i), &tracks[i]});
    }
    return result;
}

// Clip-level diff of a track present in both states.
void diffTrackContent(const Track &before, const Track &after, EditScript &removals, EditScript &changes,
                      EditScript &insertions)
{
    if (before.kind != after.kind || before.name != after.name || before.locked != after.locked ||
        before.muted != after.muted || before.solo != after.solo || before.hidden != after.hidden ||
        before.height != after.height || before.captions != after.captions || before.extras != after.extras ||
        !(before.gainDb == after.gainDb)) {
        changes.push_back(edits::setTrackProperties(before, after));
    }

    // Transitions: removed first (they may reference clips about to be removed), inserted last.
    for (const Transition &old : before.transitions) {
        const auto it = std::find_if(after.transitions.begin(), after.transitions.end(),
                                     [&](const Transition &t) { return t.id == old.id; });
        if (it == after.transitions.end()) {
            removals.push_back(edits::removeTransition(before.id, old));
        } else if (!(*it == old)) {
            changes.push_back(edits::replaceTransition(before.id, old, *it));
        }
    }

    QHash<ClipId, const Clip *> afterClips;
    for (const Clip &clip : after.clips) {
        afterClips.insert(clip.id, &clip);
    }
    QHash<ClipId, const Clip *> beforeClips;
    // Shift-only changes grouped by offset: many clips moved by the same delta cost one small edit.
    std::map<std::int64_t, std::vector<ClipId>> shifts;
    for (const Clip &old : before.clips) {
        beforeClips.insert(old.id, &old);
        const Clip *updated = afterClips.value(old.id, nullptr);
        if (!updated) {
            removals.push_back(edits::removeClip(before.id, old));
            continue;
        }
        if (*updated == old) {
            continue;
        }
        Clip shifted = old;
        shifted.start = updated->start;
        if (shifted == *updated && old.start.hasSameRate(updated->start)) {
            shifts[(updated->start - old.start).value()].push_back(old.id);
        } else {
            changes.push_back(edits::replaceClip(old, *updated));
        }
    }
    for (auto &[delta, ids] : shifts) {
        const Rational rate = before.clips.front().start.rate();
        changes.push_back(edits::shiftClips(before.id, ids, RationalTime(delta, rate)));
    }
    for (const Clip &clip : after.clips) {
        if (!beforeClips.contains(clip.id)) {
            insertions.push_back(edits::insertClip(after.id, clip));
        }
    }
    for (const Transition &transition : after.transitions) {
        const auto it = std::find_if(before.transitions.begin(), before.transitions.end(),
                                     [&](const Transition &t) { return t.id == transition.id; });
        if (it == before.transitions.end()) {
            insertions.push_back(edits::insertTransition(after.id, transition));
        }
    }
}

void diffTrackList(const SequenceId &sequenceId, bool audio, const std::vector<Track> &before,
                   const std::vector<Track> &after, EditScript &trackRemovals, EditScript &removals,
                   EditScript &changes, EditScript &trackInsertions, EditScript &insertions)
{
    const auto beforeIndex = indexTracks(before);
    const auto afterIndex = indexTracks(after);

    // Surviving tracks must keep their relative order (tracks are never reordered by these operations).
    int lastIndex = -1;
    for (const Track &track : after) {
        const auto it = beforeIndex.constFind(track.id);
        if (it == beforeIndex.constEnd()) {
            continue;
        }
        if (it->index < lastIndex) {
            throw std::logic_error("diffSequence: track reordering is not supported");
        }
        lastIndex = it->index;
    }

    // Removed tracks: descending original index, so earlier indices stay valid.
    for (int i = static_cast<int>(before.size()) - 1; i >= 0; --i) {
        const Track &track = before[static_cast<size_t>(i)];
        if (!afterIndex.contains(track.id)) {
            trackRemovals.push_back(edits::removeTrack(sequenceId, audio, i, track));
        }
    }
    for (const Track &track : before) {
        const auto it = afterIndex.constFind(track.id);
        if (it != afterIndex.constEnd()) {
            diffTrackContent(track, *it->track, removals, changes, insertions);
        }
    }
    // New tracks: ascending final index (applied after the removals). They are inserted empty and
    // their clips follow as clip insertions, so every clip edit refers to an existing track.
    for (size_t i = 0; i < after.size(); ++i) {
        const Track &track = after[i];
        if (beforeIndex.contains(track.id)) {
            continue;
        }
        Track empty = track;
        empty.clips.clear();
        empty.transitions.clear();
        trackInsertions.push_back(edits::insertTrack(sequenceId, audio, static_cast<int>(i), std::move(empty)));
        for (const Clip &clip : track.clips) {
            insertions.push_back(edits::insertClip(track.id, clip));
        }
        for (const Transition &transition : track.transitions) {
            insertions.push_back(edits::insertTransition(track.id, transition));
        }
    }
}

void append(EditScript &target, EditScript &source)
{
    for (auto &edit : source) {
        target.push_back(std::move(edit));
    }
    source.clear();
}

} // namespace

EditScript diffSequence(const Sequence &before, const Sequence &after)
{
    if (before.id != after.id) {
        throw std::logic_error("diffSequence: different sequences");
    }
    EditScript trackRemovals;
    EditScript removals;
    EditScript changes;
    EditScript trackInsertions;
    EditScript insertions;
    diffTrackList(before.id, false, before.visualTracks, after.visualTracks, trackRemovals, removals, changes,
                  trackInsertions, insertions);
    diffTrackList(before.id, true, before.audioTracks, after.audioTracks, trackRemovals, removals, changes,
                  trackInsertions, insertions);

    EditScript script;
    // Groups first when they shrink (they may reference clips being removed) — replacing the whole list
    // is simplest: groups are small.
    if (before.groups != after.groups) {
        script.push_back(edits::setGroups(before.id, before.groups, after.groups));
    }
    append(script, removals);
    append(script, trackRemovals);
    append(script, changes);
    append(script, trackInsertions);
    append(script, insertions);
    if (!(before.canvas == after.canvas)) {
        script.push_back(edits::setCanvas(before.id, before.canvas, after.canvas));
    }
    if (before.defaultBackground != after.defaultBackground) {
        script.push_back(edits::setDefaultBackground(before.id, before.defaultBackground, after.defaultBackground));
    }
    if (before.markers != after.markers) {
        script.push_back(edits::setSequenceMarkers(before.id, before.markers, after.markers));
    }
    return script;
}

} // namespace vedit
