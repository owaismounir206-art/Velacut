// SPDX-License-Identifier: GPL-3.0-or-later
#include "AssetLibraryModel.h"

#include "fx/Library.h"
#include "fx/PackageManager.h"

#include <QJsonArray>
#include <QJsonObject>

using namespace Qt::StringLiterals;

namespace vedit::ui {

AssetLibraryModel::AssetLibraryModel(QObject *parent)
    : QAbstractListModel(parent)
{
    // A pack installed or removed (Preferences → Packs): the new items show at once.
    connect(&fx::PackageManager::instance(), &fx::PackageManager::libraryChanged, this, [this] {
        refresh();
        emit kindChanged(); // the categories may have changed too
    });
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
                                            : m_kind == Animations  ? library.animationCategories()
                                            : m_kind == Stickers    ? library.stickerCategories()
                                            : m_kind == VideoEffects ? library.videoEffectCategories()
                                            : m_kind == Templates   ? library.templateCategories()
                                            : m_kind == CaptionStyles ? library.captionStyleCategories()
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
        items.push_back(Item{preset.id, preset.name.text(), preset.name.en, preset.name.it, preset.category, {}, false, {}});
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
    case CaptionStyles:
        for (const fx::CaptionStylePreset &preset : library.captionStyles()) {
            add(preset);
            items.back().animated = true; // the words light up one after the other under the pointer
        }
        break;
    case Animations:
        for (const fx::AnimationPreset &preset : library.animations()) {
            add(preset);
        }
        break;
    case VideoEffects:
        for (const fx::VideoEffectPreset &preset : library.videoEffects()) {
            add(preset);
        }
        break;
    case Stickers:
        for (const fx::StickerPreset &preset : library.stickers()) {
            items.push_back(Item{preset.id, preset.name.text(), preset.name.en, preset.name.it, preset.category, preset.path, preset.animated, {}});
        }
        break;
    case Templates:
        for (const fx::TemplatePreset &preset : library.templates()) {
            add(preset);
            QVariantList slotLengths; // ("slots" is a Qt keyword)
            double seconds = 0;
            for (const QJsonValue &slot : preset.spec.value(u"slots"_s).toArray()) {
                const double length = slot.toObject().value(u"seconds"_s).toDouble();
                slotLengths.append(length);
                seconds += length;
            }
            items.back().details = QVariantMap{{u"canvas"_s, preset.spec.value(u"canvas"_s).toString(u"16:9"_s)},
                                               {u"seconds"_s, seconds},
                                               {u"slots"_s, slotLengths},
                                               {u"texts"_s, preset.spec.value(u"texts"_s).toArray().size()}};
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
    case PathRole:
        return item.path;
    case AnimatedRole:
        return item.animated;
    case DetailsRole:
        return item.details;
    default:
        return {};
    }
}

QHash<int, QByteArray> AssetLibraryModel::roleNames() const
{
    return {
        {AssetIdRole, "assetId"},
        {NameRole, "name"},
        {CategoryRole, "category"},
        {PathRole, "path"},
        {AnimatedRole, "animated"},
        {DetailsRole, "details"},
    };
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
