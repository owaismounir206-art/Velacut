// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QAbstractListModel>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

namespace velacut::ui {

// A list of QVariantMap items for a QML Repeater, updated with a diff by the value of one key field: items whose key
// stays are moved and changed in place, never removed and inserted again, so their delegates survive (a drag in
// progress on one goes on). The item is the role "modelData": delegates read it as with a plain list.
class KeyedListModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by the controllers")
    Q_PROPERTY(int count READ count NOTIFY countChanged FINAL)

public:
    explicit KeyedListModel(QString keyField, QObject *parent = nullptr);

    // The items in their new order.
    void setItems(const QVariantList &items);
    const QVariantList &items() const { return m_items; }
    int count() const { return static_cast<int>(m_items.size()); }

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

signals:
    void countChanged();

private:
    QString keyOf(const QVariant &item) const;

    QString m_keyField;
    QVariantList m_items;
};

} // namespace velacut::ui
