// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "fx/PackageManager.h"

#include <QAbstractListModel>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

namespace vedit::ui {

// The asset packs for Preferences → Packs (SPEC §5.13, §5.16): installed packs, install from a folder or a .zip,
// remove. Backed by the process-wide fx::PackageManager.
class PackagesModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged FINAL)
    Q_PROPERTY(QString folder READ folder CONSTANT FINAL)

public:
    enum Role
    {
        PackIdRole = Qt::UserRole + 1,
        NameRole,
        VersionRole,
        ItemsRole,
        BuiltInRole,
    };

    explicit PackagesModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    QString folder() const;

    // "" when installed, else what went wrong (for the snackbar).
    Q_INVOKABLE QString install(const QUrl &source);
    Q_INVOKABLE QString remove(const QString &packId);

signals:
    void countChanged();

private:
    void refresh();

    std::vector<fx::PackageInfo> m_packages;
};

} // namespace vedit::ui
