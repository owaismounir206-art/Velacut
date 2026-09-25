// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "document/DraftStore.h"

#include <QAbstractListModel>
#include <QtQml/qqmlregistration.h>

namespace vedit::ui {

// The drafts of the home screen (SPEC 0bis rule 2): thumbnail, name, duration, date; rename, duplicate, delete.
class DraftsModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by the controllers")
    Q_PROPERTY(int count READ count NOTIFY countChanged FINAL)

public:
    enum Role
    {
        DraftIdRole = Qt::UserRole + 1,
        NameRole,
        DurationTextRole,
        ModifiedTextRole,
        ThumbnailRole, // file URL, empty if none yet
        AspectRatioRole,
        OpenElsewhereRole,
    };

    explicit DraftsModel(const document::DraftStore &store, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    int count() const { return rowCount(); }

    Q_INVOKABLE void refresh();
    // Each returns an error message for the user, or an empty string.
    Q_INVOKABLE QString rename(const QString &draftId, const QString &name);
    Q_INVOKABLE QString duplicate(const QString &draftId);
    Q_INVOKABLE QString remove(const QString &draftId);

    // "Today, 14:05", "Yesterday, 09:30", "12 Sep 2026".
    static QString modifiedText(const QDateTime &modified, const QDateTime &now);

signals:
    void countChanged();

private:
    const document::DraftStore &m_store;
    QList<document::DraftInfo> m_drafts;
};

} // namespace vedit::ui
