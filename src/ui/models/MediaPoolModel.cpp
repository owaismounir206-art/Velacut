// SPDX-License-Identifier: GPL-3.0-or-later
#include "MediaPoolModel.h"

#include "core/project/Project.h"

#include <cmath>

using namespace Qt::StringLiterals;

namespace velacut::ui {

MediaPoolModel::MediaPoolModel(Project &project, QObject *parent)
    : QAbstractListModel(parent)
    , m_project(project)
{
    connect(&m_project, &Project::changed, this, &MediaPoolModel::onProjectChanged);
    reload();
}

int MediaPoolModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_ids.size());
}

QString MediaPoolModel::durationText(double seconds)
{
    const auto total = static_cast<qint64>(std::llround(std::max(0.0, seconds)));
    const qint64 hours = total / 3600;
    const qint64 minutes = (total / 60) % 60;
    const qint64 secs = total % 60;
    if (hours > 0) {
        return u"%1:%2:%3"_s.arg(hours).arg(minutes, 2, 10, QLatin1Char('0')).arg(secs, 2, 10, QLatin1Char('0'));
    }
    return u"%1:%2"_s.arg(minutes).arg(secs, 2, 10, QLatin1Char('0'));
}

QVariant MediaPoolModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= static_cast<int>(m_ids.size())) {
        return {};
    }
    const Media *media = m_project.data().findMedia(m_ids[static_cast<size_t>(index.row())]);
    if (!media) {
        return {};
    }
    switch (role) {
    case MediaIdRole:
        return media->id.toString();
    case NameRole:
        return media->name;
    case KindRole:
        return media->kind == MediaKind::Audio ? u"audio"_s : (media->kind == MediaKind::Image ? u"image"_s : u"video"_s);
    case DurationTextRole:
        return media->kind == MediaKind::Image || !media->info.duration ? QString()
                                                                        : durationText(media->info.duration->toSecondsDouble());
    case AspectRatioRole: {
        if (!media->info.video || media->info.video->height <= 0) {
            return 1.0;
        }
        const VideoStreamInfo &video = *media->info.video;
        const double ratio = video.width * video.sampleAspectRatio.toDouble() / video.height;
        return video.rotation == 90 || video.rotation == 270 ? 1.0 / ratio : ratio;
    }
    case UsedRole:
        return m_used.contains(media->id);
    default:
        return {};
    }
}

QHash<int, QByteArray> MediaPoolModel::roleNames() const
{
    return {{MediaIdRole, "mediaId"},           {NameRole, "name"},
            {KindRole, "kind"},                 {DurationTextRole, "durationText"},
            {AspectRatioRole, "aspectRatio"},   {UsedRole, "used"}};
}

void MediaPoolModel::onProjectChanged(const ChangeSet &changes)
{
    if (!changes.media.isEmpty() || !changes.clips.isEmpty() || !changes.tracks.isEmpty() || !changes.sequences.isEmpty()) {
        reload();
    }
}

void MediaPoolModel::reload()
{
    const ProjectData &project = m_project.data();
    std::vector<MediaId> ids;
    for (auto it = project.media.rbegin(); it != project.media.rend(); ++it) {
        ids.push_back(it->id);
    }
    QSet<MediaId> used;
    for (const Sequence &sequence : project.sequences) {
        for (const auto *tracks : {&sequence.visualTracks, &sequence.audioTracks}) {
            for (const Track &track : *tracks) {
                for (const Clip &clip : track.clips) {
                    if (const MediaClipData *media = clip.media()) {
                        used.insert(media->mediaId);
                    }
                }
            }
        }
    }
    if (ids != m_ids) {
        const bool countChanges = ids.size() != m_ids.size();
        beginResetModel(); // imports and removals are rare, whole-list changes
        m_ids = std::move(ids);
        m_used = std::move(used);
        endResetModel();
        if (countChanges) {
            emit countChanged();
        }
    } else if (used != m_used) {
        m_used = std::move(used);
        emit dataChanged(index(0), index(rowCount() - 1), {UsedRole});
    }
}

} // namespace velacut::ui
