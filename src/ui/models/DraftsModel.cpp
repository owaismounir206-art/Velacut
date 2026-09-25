// SPDX-License-Identifier: GPL-3.0-or-later
#include "DraftsModel.h"

#include "ui/models/MediaPoolModel.h"

#include <QLocale>
#include <QUrl>

using namespace Qt::StringLiterals;

namespace vedit::ui {

DraftsModel::DraftsModel(const document::DraftStore &store, QObject *parent)
    : QAbstractListModel(parent)
    , m_store(store)
{
    refresh();
}

int DraftsModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_drafts.size());
}

QString DraftsModel::modifiedText(const QDateTime &modified, const QDateTime &now)
{
    const QDateTime local = modified.toLocalTime();
    const QLocale locale;
    const QString time = locale.toString(local.time(), QLocale::ShortFormat);
    const qint64 days = local.date().daysTo(now.toLocalTime().date());
    if (days == 0) {
        return tr("Today, %1").arg(time);
    }
    if (days == 1) {
        return tr("Yesterday, %1").arg(time);
    }
    return locale.toString(local.date(), u"d MMM yyyy"_s);
}

QVariant DraftsModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= static_cast<int>(m_drafts.size())) {
        return {};
    }
    const document::DraftInfo &draft = m_drafts[index.row()];
    switch (role) {
    case DraftIdRole:
        return draft.id.toString();
    case NameRole:
        return draft.name;
    case DurationTextRole:
        return MediaPoolModel::durationText(draft.duration.toSecondsDouble());
    case ModifiedTextRole:
        return modifiedText(draft.modifiedAt, QDateTime::currentDateTime());
    case ThumbnailRole:
        return draft.thumbnailPath.isEmpty() ? QString() : QUrl::fromLocalFile(draft.thumbnailPath).toString();
    case AspectRatioRole:
        return draft.canvas.height() > 0 ? static_cast<double>(draft.canvas.width()) / draft.canvas.height() : 16.0 / 9.0;
    case OpenElsewhereRole:
        return draft.openElsewhere;
    default:
        return {};
    }
}

QHash<int, QByteArray> DraftsModel::roleNames() const
{
    return {{DraftIdRole, "draftId"},         {NameRole, "name"},           {DurationTextRole, "durationText"},
            {ModifiedTextRole, "modifiedText"}, {ThumbnailRole, "thumbnail"}, {AspectRatioRole, "aspectRatio"},
            {OpenElsewhereRole, "openElsewhere"}};
}

void DraftsModel::refresh()
{
    const int before = count();
    beginResetModel();
    m_drafts = m_store.list();
    endResetModel();
    if (count() != before) {
        emit countChanged();
    }
}

QString DraftsModel::rename(const QString &draftId, const QString &name)
{
    const std::optional<ProjectId> id = ProjectId::fromString(draftId);
    QString error;
    if (!id || !m_store.renameDraft(*id, name, &error)) {
        return error;
    }
    refresh();
    return {};
}

QString DraftsModel::duplicate(const QString &draftId)
{
    const std::optional<ProjectId> id = ProjectId::fromString(draftId);
    QString error;
    if (!id || !m_store.duplicateDraft(*id, &error)) {
        return error;
    }
    refresh();
    return {};
}

QString DraftsModel::remove(const QString &draftId)
{
    const std::optional<ProjectId> id = ProjectId::fromString(draftId);
    QString error;
    if (!id || !m_store.removeDraft(*id, &error)) {
        return error;
    }
    refresh();
    return {};
}

} // namespace vedit::ui
