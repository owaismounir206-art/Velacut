// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/project/ChangeSet.h"
#include "core/project/Id.h"

#include <QAbstractListModel>

#include <functional>
#include <QtQml/qqmlregistration.h>

namespace velacut {
class Project;
struct Media;
}

namespace velacut::ui {

// The media of the project for the media pool (SPEC §5.1): most recently imported first.
class MediaPoolModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by the controllers")
    Q_PROPERTY(int count READ count NOTIFY countChanged FINAL)

public:
    enum Role
    {
        MediaIdRole = Qt::UserRole + 1,
        NameRole,
        KindRole,        // "video", "audio", "image"
        DurationTextRole, // "0:12" (empty for images)
        AspectRatioRole, // width / height as displayed (1 for audio)
        UsedRole,        // on the timeline
        ProxyRole,       // the light copy for the preview: -1 none, 0…1 being made, 1 ready
    };

    explicit MediaPoolModel(Project &project, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    int count() const { return rowCount(); }

    // Where the state of the proxies comes from (the player), and a change of it.
    void setProxyState(std::function<double(const Media &)> state) { m_proxyState = std::move(state); }
    void refreshProxies();

    // "1:05", "12:30", "1:02:03".
    static QString durationText(double seconds);

signals:
    void countChanged();

private:
    void onProjectChanged(const ChangeSet &changes);
    void reload();

    Project &m_project;
    std::vector<MediaId> m_ids;
    QSet<MediaId> m_used;
    std::function<double(const Media &)> m_proxyState;
};

} // namespace velacut::ui
