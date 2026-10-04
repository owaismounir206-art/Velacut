// SPDX-License-Identifier: GPL-3.0-or-later
#include "PackagesModel.h"

using namespace Qt::StringLiterals;

namespace vedit::ui {

PackagesModel::PackagesModel(QObject *parent)
    : QAbstractListModel(parent)
{
    connect(&fx::PackageManager::instance(), &fx::PackageManager::libraryChanged, this, &PackagesModel::refresh);
    refresh();
}

void PackagesModel::refresh()
{
    const qsizetype before = static_cast<qsizetype>(m_packages.size());
    beginResetModel();
    m_packages = fx::PackageManager::instance().installedPackages();
    endResetModel();
    if (before != static_cast<qsizetype>(m_packages.size())) {
        emit countChanged();
    }
}

int PackagesModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_packages.size());
}

QVariant PackagesModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= rowCount()) {
        return {};
    }
    const fx::PackageInfo &pack = m_packages[static_cast<size_t>(index.row())];
    switch (role) {
    case PackIdRole:
        return pack.id;
    case NameRole:
    case Qt::DisplayRole:
        return pack.name.text().isEmpty() ? pack.id : pack.name.text();
    case VersionRole:
        return pack.version;
    case ItemsRole:
        return pack.items;
    case BuiltInRole:
        return pack.builtIn;
    default:
        return {};
    }
}

QHash<int, QByteArray> PackagesModel::roleNames() const
{
    return {{PackIdRole, "packId"}, {NameRole, "name"}, {VersionRole, "version"}, {ItemsRole, "items"}, {BuiltInRole, "builtIn"}};
}

QString PackagesModel::folder() const
{
    return fx::Library::userPacksFolder();
}

QString PackagesModel::install(const QUrl &source)
{
    QString error;
    if (!fx::PackageManager::instance().installPackage(source.isLocalFile() ? source.toLocalFile() : source.toString(), &error)) {
        return error;
    }
    return {};
}

QString PackagesModel::remove(const QString &packId)
{
    QString error;
    return fx::PackageManager::instance().removePackage(packId, &error) ? QString() : error;
}

} // namespace vedit::ui
