// SPDX-License-Identifier: GPL-3.0-or-later
#include "KeyedListModel.h"

#include <QSet>

namespace velacut::ui {

namespace {
constexpr int kModelDataRole = Qt::UserRole + 1;
}

KeyedListModel::KeyedListModel(QString keyField, QObject *parent)
    : QAbstractListModel(parent)
    , m_keyField(std::move(keyField))
{
}

QString KeyedListModel::keyOf(const QVariant &item) const
{
    return item.toMap().value(m_keyField).toString();
}

void KeyedListModel::setItems(const QVariantList &items)
{
    const int before = count();
    QSet<QString> wanted;
    for (const QVariant &item : items) {
        wanted.insert(keyOf(item));
    }
    // Items that go away (from the end, so the rows above keep their numbers).
    for (int row = count() - 1; row >= 0; --row) {
        if (!wanted.contains(keyOf(m_items.at(row)))) {
            beginRemoveRows({}, row, row);
            m_items.removeAt(row);
            endRemoveRows();
        }
    }
    // Then row by row in the new order: the right item is here, further down (moved up), or new (inserted).
    for (int row = 0; row < items.size(); ++row) {
        const QString key = keyOf(items.at(row));
        if (row < count() && keyOf(m_items.at(row)) == key) {
            if (m_items.at(row) != items.at(row)) {
                m_items[row] = items.at(row);
                emit dataChanged(index(row), index(row));
            }
            continue;
        }
        int found = -1;
        for (int other = row + 1; other < count(); ++other) {
            if (keyOf(m_items.at(other)) == key) {
                found = other;
                break;
            }
        }
        if (found >= 0) {
            beginMoveRows({}, found, found, {}, row);
            m_items.move(found, row);
            endMoveRows();
            if (m_items.at(row) != items.at(row)) {
                m_items[row] = items.at(row);
                emit dataChanged(index(row), index(row));
            }
        } else {
            beginInsertRows({}, row, row);
            m_items.insert(row, items.at(row));
            endInsertRows();
        }
    }
    if (count() != before) {
        emit countChanged();
    }
}

int KeyedListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : count();
}

QVariant KeyedListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= count()) {
        return {};
    }
    return role == kModelDataRole ? m_items.at(index.row()) : QVariant();
}

QHash<int, QByteArray> KeyedListModel::roleNames() const
{
    return {{kModelDataRole, "modelData"}};
}

} // namespace velacut::ui
