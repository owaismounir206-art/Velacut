// SPDX-License-Identifier: GPL-3.0-or-later
#include "BrandKitModel.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QUuid>

using namespace Qt::StringLiterals;

namespace velacut::ui {

namespace {

BrandKitModel *s_instance = nullptr;

constexpr int kMaxColors = 16;

} // namespace

BrandKitModel::BrandKitModel(QObject *parent)
    : QAbstractListModel(parent)
{
    load();
}

BrandKitModel *BrandKitModel::instance()
{
    return s_instance;
}

void BrandKitModel::setInstance(BrandKitModel *model)
{
    s_instance = model;
}

QString BrandKitModel::folder()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + u"/velacut/brandkits"_s;
}

QString BrandKitModel::kitFolder(const QString &id) const
{
    return folder() + u"/"_s + id;
}

void BrandKitModel::load()
{
    const QDir root(folder());
    // Kits removed in an earlier session (kept until then for "Undo"): gone now.
    for (const QString &entry : root.entryList({u".trash-*"_s}, QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot)) {
        QDir(root.filePath(entry)).removeRecursively();
    }
    for (const QString &entry : root.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        QFile file(root.filePath(entry) + u"/kit.json"_s);
        if (!file.open(QIODevice::ReadOnly)) {
            continue;
        }
        const QJsonObject json = QJsonDocument::fromJson(file.readAll()).object();
        if (json.value(u"format"_s).toString() == u"vedit.brandkit"_s) {
            m_kits.push_back({entry, json});
        }
    }
    std::sort(m_kits.begin(), m_kits.end(), [](const Kit &a, const Kit &b) {
        return a.json.value(u"name"_s).toString().localeAwareCompare(b.json.value(u"name"_s).toString()) < 0;
    });
    const QString wanted = QSettings().value(u"brandkit/current"_s).toString();
    m_current = m_kits.empty() ? -1 : 0;
    for (size_t i = 0; i < m_kits.size(); ++i) {
        if (m_kits[i].id == wanted) {
            m_current = static_cast<int>(i);
        }
    }
}

bool BrandKitModel::save(const Kit &kit) const
{
    QDir().mkpath(kitFolder(kit.id));
    QSaveFile file(kitFolder(kit.id) + u"/kit.json"_s); // atomic: a kit is never half written
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    file.write(QJsonDocument(kit.json).toJson(QJsonDocument::Indented));
    return file.commit();
}

BrandKitModel::Kit *BrandKitModel::currentKit()
{
    return m_current >= 0 && m_current < static_cast<int>(m_kits.size()) ? &m_kits[static_cast<size_t>(m_current)] : nullptr;
}

const BrandKitModel::Kit *BrandKitModel::currentKit() const
{
    return m_current >= 0 && m_current < static_cast<int>(m_kits.size()) ? &m_kits[static_cast<size_t>(m_current)] : nullptr;
}

void BrandKitModel::changed()
{
    if (const Kit *kit = currentKit()) {
        save(*kit);
    }
    emit kitChanged();
}

QUrl BrandKitModel::fileUrl(const QString &relative) const
{
    const Kit *kit = currentKit();
    if (!kit || relative.isEmpty()) {
        return {};
    }
    return QUrl::fromLocalFile(kitFolder(kit->id) + u"/"_s + relative);
}

int BrandKitModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_kits.size());
}

QVariant BrandKitModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= rowCount()) {
        return {};
    }
    const Kit &kit = m_kits[static_cast<size_t>(index.row())];
    switch (role) {
    case KitIdRole:
        return kit.id;
    case NameRole:
    case Qt::DisplayRole:
        return kit.json.value(u"name"_s).toString();
    default:
        return {};
    }
}

QHash<int, QByteArray> BrandKitModel::roleNames() const
{
    return {{KitIdRole, "kitId"}, {NameRole, "name"}};
}

void BrandKitModel::setCurrent(int index)
{
    if (index == m_current || index < 0 || index >= rowCount()) {
        return;
    }
    m_current = index;
    QSettings().setValue(u"brandkit/current"_s, m_kits[static_cast<size_t>(index)].id);
    emit currentChanged();
    emit kitChanged();
}

QString BrandKitModel::name() const
{
    const Kit *kit = currentKit();
    return kit ? kit->json.value(u"name"_s).toString() : QString();
}

QVariantList BrandKitModel::colors() const
{
    QVariantList list;
    if (const Kit *kit = currentKit()) {
        for (const QJsonValue &value : kit->json.value(u"colors"_s).toArray()) {
            list << QColor(value.toString());
        }
    }
    return list;
}

QStringList BrandKitModel::fonts() const
{
    QStringList list;
    if (const Kit *kit = currentKit()) {
        for (const QJsonValue &value : kit->json.value(u"fonts"_s).toArray()) {
            list << value.toString();
        }
    }
    return list;
}

QVariantList BrandKitModel::logos() const
{
    QVariantList list;
    if (const Kit *kit = currentKit()) {
        for (const QJsonValue &value : kit->json.value(u"logos"_s).toArray()) {
            list << fileUrl(value.toString());
        }
    }
    return list;
}

QUrl BrandKitModel::intro() const
{
    const Kit *kit = currentKit();
    return kit ? fileUrl(kit->json.value(u"intro"_s).toString()) : QUrl();
}

QUrl BrandKitModel::outro() const
{
    const Kit *kit = currentKit();
    return kit ? fileUrl(kit->json.value(u"outro"_s).toString()) : QUrl();
}

QVariantList BrandKitModel::music() const
{
    QVariantList list;
    if (const Kit *kit = currentKit()) {
        for (const QJsonValue &value : kit->json.value(u"music"_s).toArray()) {
            list << QVariantMap{{u"name"_s, QFileInfo(value.toString()).completeBaseName()}, {u"url"_s, fileUrl(value.toString())}};
        }
    }
    return list;
}

int BrandKitModel::createKit(const QString &name)
{
    const QString trimmed = name.trimmed().isEmpty() ? tr("My brand") : name.trimmed();
    Kit kit{QUuid::createUuid().toString(QUuid::WithoutBraces), QJsonObject{{u"format"_s, u"vedit.brandkit"_s},
                                                                           {u"formatVersion"_s, 1},
                                                                           {u"name"_s, trimmed}}};
    if (!save(kit)) {
        return -1;
    }
    const int row = rowCount();
    beginInsertRows({}, row, row);
    m_kits.push_back(std::move(kit));
    endInsertRows();
    emit countChanged();
    setCurrent(row);
    return row;
}

void BrandKitModel::renameKit(const QString &name)
{
    Kit *kit = currentKit();
    if (!kit || name.trimmed().isEmpty()) {
        return;
    }
    kit->json.insert(u"name"_s, name.trimmed());
    const QModelIndex index = createIndex(m_current, 0);
    emit dataChanged(index, index);
    changed();
}

QString BrandKitModel::removeKit()
{
    Kit *kit = currentKit();
    if (!kit) {
        return {};
    }
    const QString id = kit->id;
    // Kept aside until the next start, so that "Undo" can bring it back.
    if (!QDir().rename(kitFolder(id), folder() + u"/.trash-"_s + id)) {
        return {};
    }
    beginRemoveRows({}, m_current, m_current);
    m_kits.erase(m_kits.begin() + m_current);
    endRemoveRows();
    emit countChanged();
    m_current = m_kits.empty() ? -1 : std::min(m_current, rowCount() - 1);
    emit currentChanged();
    emit kitChanged();
    return id;
}

bool BrandKitModel::restoreKit(const QString &kitId)
{
    const QString trashed = folder() + u"/.trash-"_s + kitId;
    if (kitId.isEmpty() || !QFileInfo(trashed).isDir() || !QDir().rename(trashed, kitFolder(kitId))) {
        return false;
    }
    QFile file(kitFolder(kitId) + u"/kit.json"_s);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    const int row = rowCount();
    beginInsertRows({}, row, row);
    m_kits.push_back({kitId, QJsonDocument::fromJson(file.readAll()).object()});
    endInsertRows();
    emit countChanged();
    setCurrent(row);
    return true;
}

void BrandKitModel::addColor(const QColor &color)
{
    Kit *kit = currentKit();
    if (!kit || !color.isValid()) {
        return;
    }
    QJsonArray colors = kit->json.value(u"colors"_s).toArray();
    const QString value = color.name(QColor::HexRgb).toUpper();
    if (colors.contains(value) || colors.size() >= kMaxColors) {
        return;
    }
    colors.append(value);
    kit->json.insert(u"colors"_s, colors);
    changed();
}

void BrandKitModel::removeColor(int index)
{
    Kit *kit = currentKit();
    QJsonArray colors = kit ? kit->json.value(u"colors"_s).toArray() : QJsonArray();
    if (!kit || index < 0 || index >= colors.size()) {
        return;
    }
    colors.removeAt(index);
    kit->json.insert(u"colors"_s, colors);
    changed();
}

void BrandKitModel::addFont(const QString &family)
{
    Kit *kit = currentKit();
    if (!kit || family.isEmpty()) {
        return;
    }
    QJsonArray fonts = kit->json.value(u"fonts"_s).toArray();
    if (!fonts.contains(family)) {
        fonts.append(family);
        kit->json.insert(u"fonts"_s, fonts);
        changed();
    }
}

void BrandKitModel::removeFont(int index)
{
    Kit *kit = currentKit();
    QJsonArray fonts = kit ? kit->json.value(u"fonts"_s).toArray() : QJsonArray();
    if (!kit || index < 0 || index >= fonts.size()) {
        return;
    }
    fonts.removeAt(index);
    kit->json.insert(u"fonts"_s, fonts);
    changed();
}

QString BrandKitModel::copyIn(const QUrl &file, const QString &subfolder, QString *error)
{
    const Kit *kit = currentKit();
    const QString source = file.isLocalFile() ? file.toLocalFile() : file.toString();
    if (!kit) {
        *error = tr("Create a brand kit first.");
        return {};
    }
    if (!QFileInfo(source).isFile()) {
        *error = tr("The file does not exist: %1").arg(source);
        return {};
    }
    const QString directory = kitFolder(kit->id) + u"/"_s + subfolder;
    QDir().mkpath(directory);
    const QFileInfo info(source);
    QString name = info.fileName();
    for (int n = 2; QFileInfo::exists(directory + u"/"_s + name); ++n) {
        name = u"%1 (%2).%3"_s.arg(info.completeBaseName()).arg(n).arg(info.suffix());
    }
    if (!QFile::copy(source, directory + u"/"_s + name)) {
        *error = tr("The file could not be copied into the kit.");
        return {};
    }
    return subfolder + u"/"_s + name;
}

void BrandKitModel::removeFile(const QString &relative)
{
    if (const Kit *kit = currentKit(); kit && !relative.isEmpty() && !relative.contains(u".."_s)) {
        QFile::remove(kitFolder(kit->id) + u"/"_s + relative);
    }
}

QString BrandKitModel::addLogo(const QUrl &file)
{
    QString error;
    const QString relative = copyIn(file, u"logos"_s, &error);
    if (relative.isEmpty()) {
        return error;
    }
    Kit *kit = currentKit();
    QJsonArray logos = kit->json.value(u"logos"_s).toArray();
    logos.append(relative);
    kit->json.insert(u"logos"_s, logos);
    changed();
    return {};
}

void BrandKitModel::removeLogo(int index)
{
    Kit *kit = currentKit();
    QJsonArray logos = kit ? kit->json.value(u"logos"_s).toArray() : QJsonArray();
    if (!kit || index < 0 || index >= logos.size()) {
        return;
    }
    removeFile(logos.at(index).toString());
    logos.removeAt(index);
    kit->json.insert(u"logos"_s, logos);
    changed();
}

QString BrandKitModel::setIntro(const QUrl &file)
{
    QString error;
    const QString relative = copyIn(file, u"intro"_s, &error);
    if (relative.isEmpty()) {
        return error;
    }
    removeFile(currentKit()->json.value(u"intro"_s).toString());
    currentKit()->json.insert(u"intro"_s, relative);
    changed();
    return {};
}

QString BrandKitModel::setOutro(const QUrl &file)
{
    QString error;
    const QString relative = copyIn(file, u"outro"_s, &error);
    if (relative.isEmpty()) {
        return error;
    }
    removeFile(currentKit()->json.value(u"outro"_s).toString());
    currentKit()->json.insert(u"outro"_s, relative);
    changed();
    return {};
}

void BrandKitModel::clearIntro()
{
    if (Kit *kit = currentKit()) {
        removeFile(kit->json.value(u"intro"_s).toString());
        kit->json.remove(u"intro"_s);
        changed();
    }
}

void BrandKitModel::clearOutro()
{
    if (Kit *kit = currentKit()) {
        removeFile(kit->json.value(u"outro"_s).toString());
        kit->json.remove(u"outro"_s);
        changed();
    }
}

QString BrandKitModel::addMusic(const QUrl &file)
{
    QString error;
    const QString relative = copyIn(file, u"music"_s, &error);
    if (relative.isEmpty()) {
        return error;
    }
    Kit *kit = currentKit();
    QJsonArray music = kit->json.value(u"music"_s).toArray();
    music.append(relative);
    kit->json.insert(u"music"_s, music);
    changed();
    return {};
}

void BrandKitModel::removeMusic(int index)
{
    Kit *kit = currentKit();
    QJsonArray music = kit ? kit->json.value(u"music"_s).toArray() : QJsonArray();
    if (!kit || index < 0 || index >= music.size()) {
        return;
    }
    removeFile(music.at(index).toString());
    music.removeAt(index);
    kit->json.insert(u"music"_s, music);
    changed();
}

} // namespace velacut::ui
