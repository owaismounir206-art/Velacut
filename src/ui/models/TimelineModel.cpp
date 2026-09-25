// SPDX-License-Identifier: GPL-3.0-or-later
#include "TimelineModel.h"

#include "core/project/Project.h"
#include "fx/Library.h"

#include <algorithm>

using namespace Qt::StringLiterals;

namespace vedit::ui {

namespace {

int frames(const RationalTime &time, const Rational &rate)
{
    return static_cast<int>(time.rate() == rate ? time.value() : time.rescaled(rate, Rounding::NearestEven).value());
}

QString kindOf(const Clip &clip, const ProjectData &project)
{
    if (const MediaClipData *media = clip.media()) {
        const Media *item = project.findMedia(media->mediaId);
        if (!item) {
            return u"other"_s;
        }
        if (media->streams == Streams::AudioOnly || item->kind == MediaKind::Audio) {
            return u"audio"_s;
        }
        return item->kind == MediaKind::Image ? u"image"_s : u"video"_s;
    }
    if (std::holds_alternative<ColorClipData>(clip.payload)) {
        return u"color"_s;
    }
    if (clip.text()) {
        return u"text"_s;
    }
    return u"other"_s;
}

} // namespace

TimelineModel::TimelineModel(Project &project, SequenceId sequenceId, QObject *parent)
    : QAbstractListModel(parent)
    , m_project(project)
    , m_sequenceId(std::move(sequenceId))
{
    connect(&m_project, &Project::changed, this, &TimelineModel::onProjectChanged);
    rebuild();
}

int TimelineModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_rows.size());
}

QVariant TimelineModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= static_cast<int>(m_rows.size())) {
        return {};
    }
    const Entry &entry = m_rows[static_cast<size_t>(index.row())];
    switch (role) {
    case ClipIdRole:
        return entry.id.toString();
    case TrackRowRole:
        return entry.trackRow;
    case StartRole:
        return entry.start;
    case DurationRole:
        return entry.duration;
    case NameRole:
        return entry.name;
    case KindRole:
        return entry.kind;
    case MediaIdRole:
        return entry.mediaId.toString();
    case SourceInRole:
        return entry.sourceIn;
    case SelectedRole:
        return entry.selected;
    case LockedRole:
        return entry.locked;
    case MediaLengthRole:
        return entry.mediaLength;
    default:
        return {};
    }
}

QHash<int, QByteArray> TimelineModel::roleNames() const
{
    return {{ClipIdRole, "clipId"},     {TrackRowRole, "trackRow"}, {StartRole, "start"},
            {DurationRole, "duration"}, {NameRole, "name"},         {KindRole, "kind"},
            {MediaIdRole, "mediaId"},   {SourceInRole, "sourceIn"}, {SelectedRole, "selected"},
            {LockedRole, "locked"},     {MediaLengthRole, "mediaLength"}};
}

std::optional<TimelineModel::TrackRow> TimelineModel::trackRow(int row) const
{
    if (row < 0 || row >= static_cast<int>(m_trackRows.size())) {
        return std::nullopt;
    }
    return m_trackRows[static_cast<size_t>(row)];
}

void TimelineModel::onProjectChanged(const ChangeSet &changes)
{
    if (changes.settingsChanged || changes.sequences.contains(m_sequenceId) || !changes.tracks.isEmpty() ||
        !changes.clips.isEmpty() || !changes.media.isEmpty()) {
        rebuild();
    }
}

void TimelineModel::rebuild()
{
    const ProjectData &project = m_project.data();
    const Sequence *sequence = project.findSequence(m_sequenceId);
    std::vector<TrackRow> trackRows;
    QVariantList trackList;
    std::vector<Entry> all;
    std::vector<Cut> allCuts;
    int duration = 0;
    int mainRow = 0;
    if (sequence) {
        const Rational rate = project.settings.frameRate;
        const auto addTrack = [&](const Track &track, bool audio, int index, const QString &kind) {
            const int row = static_cast<int>(trackRows.size());
            trackRows.push_back(TrackRow{track.id, audio, index});
            trackList.append(QVariantMap{{u"trackId"_s, track.id.toString()},
                                         {u"kind"_s, kind},
                                         {u"audio"_s, audio},
                                         {u"index"_s, index},
                                         {u"muted"_s, track.muted},
                                         {u"hidden"_s, track.hidden},
                                         {u"locked"_s, track.locked}});
            for (const Clip &clip : track.clips) {
                Entry entry;
                entry.id = clip.id;
                entry.trackRow = row;
                entry.start = frames(clip.start, rate);
                entry.duration = frames(clip.end(), rate) - entry.start;
                entry.kind = kindOf(clip, project);
                if (const MediaClipData *media = clip.media()) {
                    entry.mediaId = media->mediaId;
                    entry.sourceIn = frames(media->sourceIn, rate);
                    const Media *item = project.findMedia(media->mediaId);
                    entry.name = clip.name.isEmpty() && item ? item->name : clip.name;
                    if (item && item->kind != MediaKind::Image && item->info.duration) {
                        entry.mediaLength = frames(item->info.duration->rescaled(rate, Rounding::Floor), rate);
                    }
                } else if (const TextClipData *text = clip.text(); text && clip.name.isEmpty()) {
                    entry.name = text->text.section(u'\n', 0, 0); // what the text says
                } else {
                    entry.name = clip.name;
                }
                entry.selected = m_selection.contains(clip.id);
                entry.locked = track.locked;
                duration = std::max(duration, entry.start + entry.duration);
                all.push_back(std::move(entry));
            }
        };
        const int visualCount = static_cast<int>(sequence->visualTracks.size());
        // Cuts of the visual tracks, row by row as they are added below (row = visualCount - 1 - index).
        for (int i = 0; i < visualCount; ++i) {
            const Track &track = sequence->visualTracks[static_cast<size_t>(i)];
            for (size_t k = 0; k + 1 < track.clips.size(); ++k) {
                const Clip &from = track.clips[k];
                const Clip &to = track.clips[k + 1];
                if (!(from.end() == to.start)) {
                    continue;
                }
                QVariantMap cut{{u"fromClip"_s, from.id.toString()},
                                {u"trackRow"_s, visualCount - 1 - i},
                                {u"frame"_s, frames(to.start, rate)},
                                {u"transitionId"_s, QString()},
                                {u"duration"_s, 0},
                                {u"name"_s, QString()}};
                for (const Transition &transition : track.transitions) {
                    if (transition.from == from.id && transition.to == to.id) {
                        const fx::TransitionPreset *preset = fx::Library::core().transition(transition.type.id);
                        cut[u"transitionId"_s] = transition.id.toString();
                        cut[u"duration"_s] = frames(transition.duration, rate);
                        cut[u"name"_s] = preset ? preset->name.text() : transition.type.id;
                    }
                }
                allCuts.push_back(Cut{frames(to.start, rate), cut});
            }
        }
        for (int i = visualCount - 1; i >= 0; --i) {
            if (i == 0) {
                mainRow = static_cast<int>(trackRows.size());
            }
            addTrack(sequence->visualTracks[static_cast<size_t>(i)], false, i, i == 0 ? u"main"_s : u"overlay"_s);
        }
        for (int i = 0; i < static_cast<int>(sequence->audioTracks.size()); ++i) {
            addTrack(sequence->audioTracks[static_cast<size_t>(i)], true, i, u"audio"_s);
        }
    }
    m_all = std::move(all);
    m_allCuts = std::move(allCuts);
    if (trackList != m_trackList || mainRow != m_mainRow) {
        m_trackRows = std::move(trackRows);
        m_trackList = std::move(trackList);
        m_mainRow = mainRow;
        emit tracksChanged();
    } else {
        m_trackRows = std::move(trackRows);
    }
    if (duration != m_duration) {
        m_duration = duration;
        emit durationChanged();
    }
    applyVisible();
}

bool TimelineModel::isVisible(const Entry &entry) const
{
    const int margin = std::max(1, m_lastFrame - m_firstFrame);
    return entry.start + entry.duration >= m_firstFrame - margin && entry.start <= m_lastFrame + margin;
}

void TimelineModel::applyVisible()
{
    const int margin = std::max(1, m_lastFrame - m_firstFrame);
    QVariantList cuts;
    for (const Cut &cut : m_allCuts) {
        if (cut.frame >= m_firstFrame - margin && cut.frame <= m_lastFrame + margin) {
            cuts.append(cut.value);
        }
    }
    if (cuts != m_cuts) {
        m_cuts = std::move(cuts);
        emit cutsChanged();
    }
    QHash<ClipId, const Entry *> wanted;
    for (const Entry &entry : m_all) {
        if (isVisible(entry)) {
            wanted.insert(entry.id, &entry);
        }
    }
    // Remove rows no longer wanted (from the end, so indexes stay valid), update the others, append the new ones.
    for (int row = static_cast<int>(m_rows.size()) - 1; row >= 0; --row) {
        if (!wanted.contains(m_rows[static_cast<size_t>(row)].id)) {
            beginRemoveRows({}, row, row);
            m_rows.erase(m_rows.begin() + row);
            endRemoveRows();
        }
    }
    QSet<ClipId> present;
    for (int row = 0; row < static_cast<int>(m_rows.size()); ++row) {
        Entry &current = m_rows[static_cast<size_t>(row)];
        present.insert(current.id);
        const Entry &updated = *wanted.value(current.id);
        if (!(current == updated)) {
            current = updated;
            emit dataChanged(index(row), index(row));
        }
    }
    for (const Entry &entry : m_all) {
        if (wanted.contains(entry.id) && !present.contains(entry.id)) {
            const int row = static_cast<int>(m_rows.size());
            beginInsertRows({}, row, row);
            m_rows.push_back(entry);
            endInsertRows();
        }
    }
}

void TimelineModel::setVisibleRange(int firstFrame, int lastFrame)
{
    if (firstFrame == m_firstFrame && lastFrame == m_lastFrame) {
        return;
    }
    m_firstFrame = firstFrame;
    m_lastFrame = std::max(firstFrame, lastFrame);
    applyVisible();
}

void TimelineModel::setSelection(const QSet<ClipId> &selection)
{
    m_selection = selection;
    for (Entry &entry : m_all) {
        entry.selected = m_selection.contains(entry.id);
    }
    applyVisible();
}

std::vector<std::pair<int, int>> TimelineModel::clipEdges(const QSet<ClipId> &excluded) const
{
    std::vector<std::pair<int, int>> edges;
    for (const Entry &entry : m_all) {
        if (!excluded.contains(entry.id)) {
            edges.emplace_back(entry.start, entry.start + entry.duration);
        }
    }
    return edges;
}

} // namespace vedit::ui
