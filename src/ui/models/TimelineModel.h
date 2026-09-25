// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/project/ChangeSet.h"
#include "core/project/ProjectData.h"

#include <QAbstractListModel>
#include <QtQml/qqmlregistration.h>
#include <QSet>
#include <QVariantList>

#include <limits>
#include <optional>
#include <vector>

namespace vedit {
class Project;
}

namespace vedit::ui {

// The clips of a sequence for the QML timeline (D-16): one row per clip *in the visible time range* (plus a margin),
// so that 500+ clips cost only the delegates on screen. Updated from Project::changed() with a diff (rows are
// inserted, removed or changed, never reset), so delegates keep their state during edits.
//
// Track rows, top to bottom: overlay tracks (highest first), the main track, the audio tracks. Times are frames at the
// project frame rate.
class TimelineModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by the controllers")
    Q_PROPERTY(QVariantList tracks READ tracks NOTIFY tracksChanged FINAL)
    Q_PROPERTY(int mainRow READ mainRow NOTIFY tracksChanged FINAL)
    Q_PROPERTY(int rowCountTotal READ trackRowCount NOTIFY tracksChanged FINAL)
    Q_PROPERTY(int duration READ duration NOTIFY durationChanged FINAL)
    Q_PROPERTY(QVariantList cuts READ cuts NOTIFY cutsChanged FINAL)
    Q_PROPERTY(QVariantList markers READ markers NOTIFY markersChanged FINAL)

public:
    enum Role
    {
        ClipIdRole = Qt::UserRole + 1,
        TrackRowRole,
        StartRole,
        DurationRole,
        NameRole,
        KindRole, // "video", "image", "audio", "text", "color", "other"
        MediaIdRole,
        SourceInRole,
        SelectedRole,
        LockedRole,
        MediaLengthRole, // frames of material in the media (-1: unlimited, e.g. photos)
        MarkersRole,
    };

    struct TrackRow
    {
        TrackId id;
        bool audio = false;
        int index = 0; // in visualTracks / audioTracks
    };

    TimelineModel(Project &project, SequenceId sequenceId, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // [{trackId, kind: "overlay"|"main"|"audio", audio, index, muted, gainDb, hidden, locked}], top to bottom.
    QVariantList tracks() const { return m_trackList; }
    int mainRow() const { return m_mainRow; }
    int trackRowCount() const { return static_cast<int>(m_trackRows.size()); }
    int duration() const { return m_duration; }
    // The cuts between touching clips of the visual tracks in the visible range (plus a margin), where a transition
    // goes: [{fromClip, trackRow, frame, transitionId ("" = none), duration (frames), name}].
    QVariantList cuts() const { return m_cuts; }
    // [{id, frame, name, color, note, kind}]
    QVariantList markers() const { return m_markers; }
    std::optional<TrackRow> trackRow(int row) const;

    // Frames shown by the view; clips outside (with a margin of the same width) get no row.
    Q_INVOKABLE void setVisibleRange(int firstFrame, int lastFrame);
    void setSelection(const QSet<ClipId> &selection);
    // Every clip overlapping the view or not: for snapping.
    std::vector<std::pair<int, int>> clipEdges(const QSet<ClipId> &excluded) const;

signals:
    void tracksChanged();
    void durationChanged();
    void cutsChanged();
    void markersChanged();

private:
    struct Entry
    {
        ClipId id;
        int trackRow = 0;
        int start = 0;
        int duration = 0;
        QString name;
        QString kind;
        MediaId mediaId;
        int sourceIn = 0;
        bool selected = false;
        bool locked = false;
        int mediaLength = -1;
        QVariantList markers;

        friend bool operator==(const Entry &, const Entry &) = default;
    };

    void onProjectChanged(const ChangeSet &changes);
    void rebuild();
    void applyVisible();
    bool isVisible(const Entry &entry) const;

    Project &m_project;
    SequenceId m_sequenceId;
    std::vector<Entry> m_all;  // every clip
    std::vector<Entry> m_rows; // the visible ones: the model rows (in no particular order)
    struct Cut
    {
        int frame = 0;
        QVariantMap value;
    };
    std::vector<Cut> m_allCuts;
    QVariantList m_cuts;
    QVariantList m_markers;
    std::vector<TrackRow> m_trackRows;
    QVariantList m_trackList;
    QSet<ClipId> m_selection;
    int m_mainRow = 0;
    int m_duration = 0;
    int m_firstFrame = 0;
    int m_lastFrame = std::numeric_limits<int>::max() / 4;
};

} // namespace vedit::ui
