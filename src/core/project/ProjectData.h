// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/project/Id.h"
#include "core/project/Media.h"
#include "core/project/Sequence.h"
#include "core/time/Rational.h"

#include <QDateTime>
#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <optional>
#include <vector>

namespace vedit {

struct ProjectSettings
{
    Rational frameRate{30};
    int sampleRate = 48000;
    int audioChannels = 2;
    QString colorSpace = QStringLiteral("bt709");
    // True until canvas and frame rate are fixed by the first clip or by the user (SPEC 0bis, rule 1).
    bool formatFromFirstClip = true;
    Canvas defaultCanvas;

    friend bool operator==(const ProjectSettings &, const ProjectSettings &) = default;
};

// Where a clip lives inside a project.
struct ClipLocation
{
    int sequenceIndex = -1;
    bool audioTrack = false;
    int trackIndex = -1;
    int clipIndex = -1;

    bool isValid() const { return sequenceIndex >= 0 && trackIndex >= 0 && clipIndex >= 0; }
};

struct TrackLocation
{
    int sequenceIndex = -1;
    bool audioTrack = false;
    int trackIndex = -1;

    bool isValid() const { return sequenceIndex >= 0 && trackIndex >= 0; }
};

// Whether a clip of this kind (and streams) may live on a track of this kind.
bool clipAllowedOnTrack(const Clip &clip, TrackKind trackKind);

// The whole project as a plain value: copyable (snapshots for export and tests), comparable,
// serializable. Mutations go through ProjectMutator so that every change is recorded.
struct ProjectData
{
    ProjectId id;
    QString name;
    QDateTime createdAt;
    QDateTime modifiedAt;
    ProjectSettings settings;
    std::vector<MediaFolder> mediaFolders;
    std::vector<Media> media;
    std::vector<Sequence> sequences;
    SequenceId mainSequenceId;
    QJsonObject extras;

    // A new empty project: one main sequence with an empty main track (SPEC 0bis, rule 1).
    static ProjectData createEmpty(const QString &name);

    const Media *findMedia(const MediaId &mediaId) const;
    int mediaIndex(const MediaId &mediaId) const;
    int sequenceIndex(const SequenceId &sequenceId) const;
    const Sequence *findSequence(const SequenceId &sequenceId) const;
    const Sequence *mainSequence() const;
    TrackLocation locateTrack(const TrackId &trackId) const;
    const Track *findTrack(const TrackId &trackId) const;
    ClipLocation locateClip(const ClipId &clipId) const;
    const Clip *findClip(const ClipId &clipId) const;
    const Track &track(const TrackLocation &location) const;
    const Clip &clip(const ClipLocation &location) const;

    // Structural invariants (docs/ARCHITECTURE.md §4.3). Returns human-readable violations; empty = valid.
    QStringList checkInvariants() const;

    friend bool operator==(const ProjectData &, const ProjectData &) = default;
};

} // namespace vedit
