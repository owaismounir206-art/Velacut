// SPDX-License-Identifier: GPL-3.0-or-later
#include "AssetLibraryModel.h"

#include "fx/Library.h"

using namespace Qt::StringLiterals;

namespace vedit::ui {

AssetLibraryModel::AssetLibraryModel(QObject *parent)
    : QAbstractListModel(parent)
{
    refresh();
}

void AssetLibraryModel::setKind(Kind kind)
{
    if (kind != m_kind) {
        m_kind = kind;
        m_category.clear();
        emit kindChanged();
        emit categoryChanged();
        refresh();
    }
}

void AssetLibraryModel::setCategory(const QString &category)
{
    if (category != m_category) {
        m_category = category;
        emit categoryChanged();
        refresh();
    }
}

void AssetLibraryModel::setSearch(const QString &search)
{
    if (search != m_search) {
        m_search = search;
        emit searchChanged();
        refresh();
    }
}

QVariantList AssetLibraryModel::categories() const
{
    const fx::Library &library = fx::Library::core();
    const std::vector<fx::Category> &list = m_kind == Filters       ? library.filterCategories()
                                            : m_kind == Transitions ? library.transitionCategories()
                                                                    : library.textStyleCategories();
    QVariantList result;
    for (const fx::Category &category : list) {
        result << QVariantMap{{u"id"_s, category.id}, {u"name"_s, category.name.text()}};
    }
    return result;
}

std::vector<AssetLibraryModel::Item> AssetLibraryModel::itemsOfKind() const
{
    const fx::Library &library = fx::Library::core();
    std::vector<Item> items;
    const auto add = [&items](const auto &preset) {
        items.push_back(Item{preset.id, preset.name.text(), preset.name.en, preset.name.it, preset.category});
    };
    switch (m_kind) {
    case Filters:
        for (const fx::FilterPreset &preset : library.filters()) {
            add(preset);
        }
        break;
    case Transitions:
        for (const fx::TransitionPreset &preset : library.transitions()) {
            add(preset);
        }
        break;
    case TextStyles:
        for (const fx::TextStylePreset &preset : library.textStyles()) {
            add(preset);
        }
        break;
    }
    return items;
}

void AssetLibraryModel::refresh()
{
    const QString needle = m_search.trimmed();
    std::vector<Item> items;
    for (Item &item : itemsOfKind()) {
        if (!m_category.isEmpty() && item.category != m_category) {
            continue;
        }
        if (!needle.isEmpty() && !item.nameEn.contains(needle, Qt::CaseInsensitive) &&
            !item.nameIt.contains(needle, Qt::CaseInsensitive) && !item.id.contains(needle, Qt::CaseInsensitive)) {
            continue;
        }
        items.push_back(std::move(item));
    }
    const bool countChanges = items.size() != m_items.size();
    beginResetModel();
    m_items = std::move(items);
    endResetModel();
    if (countChanges) {
        emit countChanged();
    }
}

int AssetLibraryModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_items.size());
}

QVariant AssetLibraryModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= static_cast<int>(m_items.size())) {
        return {};
    }
    const Item &item = m_items[static_cast<size_t>(index.row())];
    switch (role) {
    case AssetIdRole:
        return item.id;
    case NameRole:
    case Qt::DisplayRole:
        return item.name;
    case CategoryRole:
        return item.category;
    default:
        return {};
    }
}

QHash<int, QByteArray> AssetLibraryModel::roleNames() const
{
    return {{AssetIdRole, "assetId"}, {NameRole, "name"}, {CategoryRole, "category"}};
}

QString AssetLibraryModel::nameOf(const QString &assetId) const
{
    for (const Item &item : itemsOfKind()) {
        if (item.id == assetId) {
            return item.name;
        }
    }
    return {};
}

} // namespace vedit::ui
