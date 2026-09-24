// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/project/Id.h"

#include <QSet>

namespace vedit {

// What a transaction (one redo/undo) touched. Listeners (UI models, engine projection, autosave) use it
// to update only what changed (docs/ARCHITECTURE.md §4.3).
struct ChangeSet
{
    bool projectChanged = false;  // name or other project-level metadata
    bool settingsChanged = false; // frame rate, sample rate, default canvas…
    QSet<MediaId> media;          // added, removed or modified media
    QSet<SequenceId> sequences;   // sequence-level changes: canvas, tracks added/removed/reordered, markers
    QSet<TrackId> tracks;         // clips or transitions of these tracks changed (or track properties)
    QSet<ClipId> clips;           // clips added, removed or modified

    bool isEmpty() const
    {
        return !projectChanged && !settingsChanged && media.isEmpty() && sequences.isEmpty() &&
               tracks.isEmpty() && clips.isEmpty();
    }

    void merge(const ChangeSet &other)
    {
        projectChanged = projectChanged || other.projectChanged;
        settingsChanged = settingsChanged || other.settingsChanged;
        media.unite(other.media);
        sequences.unite(other.sequences);
        tracks.unite(other.tracks);
        clips.unite(other.clips);
    }
};

} // namespace vedit
