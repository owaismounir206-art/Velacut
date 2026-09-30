// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QAbstractListModel>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <vector>

namespace vedit::ui {

// The items of an asset library of the core pack (filters, transitions, text styles, animations) for the panels on the
// left, filtered by category chip and by search text (name in either language, or id).
class AssetLibraryModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(Kind kind READ kind WRITE setKind NOTIFY kindChanged FINAL)
    Q_PROPERTY(QString category READ category WRITE setCategory NOTIFY categoryChanged FINAL)
    Q_PROPERTY(QString search READ search WRITE setSearch NOTIFY searchChanged FINAL)
    // [{id, name}] of the kind, in the order of the pack.
    Q_PROPERTY(QVariantList categories READ categories NOTIFY kindChanged FINAL)
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged FINAL)

public:
    enum Kind
    {
        Filters,
        Transitions,
        TextStyles,
        Animations,
        Stickers,
    };
    Q_ENUM(Kind)

    enum Role
    {
        AssetIdRole = Qt::UserRole + 1,
        NameRole,
        CategoryRole,
        PathRole,
        AnimatedRole,
    };

    explicit AssetLibraryModel(QObject *parent = nullptr);

    Kind kind() const { return m_kind; }
    void setKind(Kind kind);
    QString category() const { return m_category; }
    void setCategory(const QString &category);
    QString search() const { return m_search; }
    void setSearch(const QString &search);
    QVariantList categories() const;

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Name of an item of the kind (for messages and the search results).
    Q_INVOKABLE QString nameOf(const QString &assetId) const;

signals:
    void kindChanged();
    void categoryChanged();
    void searchChanged();
    void countChanged();

private:
    struct Item
    {
        QString id;
        QString name;
        QString nameEn;
        QString nameIt;
        QString category;
        QString path;
        bool animated = false;
    };
    std::vector<Item> itemsOfKind() const;
    void refresh();

    Kind m_kind = Filters;
    QString m_category;
    QString m_search;
    std::vector<Item> m_items;
};

} // namespace vedit::ui
