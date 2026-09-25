// SPDX-License-Identifier: GPL-3.0-or-later
#include "DraftStore.h"

#include "common/Paths.h"
#include "core/serialization/ProjectFile.h"
#include "document/Document.h"
#include "document/DraftLock.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QSet>
#include <QStandardPaths>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace vedit::document {

namespace {

std::optional<DraftInfo> readInfo(const QString &directory)
{
    const std::optional<ProjectId> id = ProjectId::fromString(QFileInfo(directory).fileName());
    if (!id || !QFileInfo::exists(directory + u"/project.vproj"_s)) {
        return std::nullopt;
    }
    DraftInfo info;
    info.id = *id;
    info.directory = directory;
    QFile file(directory + u"/draft.json"_s);
    const QJsonObject json = file.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(file.readAll()).object()
                                                             : QJsonObject();
    const std::optional<RationalTime> duration = RationalTime::fromString(json.value(u"duration"_s).toString());
    if (!json.isEmpty() && duration) {
        info.name = json.value(u"name"_s).toString();
        info.modifiedAt = QDateTime::fromString(json.value(u"modifiedAt"_s).toString(), Qt::ISODate);
        info.duration = *duration;
        const QJsonObject canvas = json.value(u"canvas"_s).toObject();
        info.canvas = QSize(canvas.value(u"width"_s).toInt(), canvas.value(u"height"_s).toInt());
    } else {
        // draft.json is only a cache: rebuilt from the project when missing or damaged.
        const ProjectLoadResult loaded = projectfile::load(directory + u"/project.vproj"_s);
        if (!loaded.ok()) {
            info.name = QFileInfo(directory).fileName();
            info.modifiedAt = QFileInfo(directory + u"/project.vproj"_s).lastModified();
        } else {
            const ProjectData &data = *loaded.project;
            info.name = data.name;
            info.modifiedAt = data.modifiedAt;
            if (const Sequence *sequence = data.mainSequence()) {
                info.duration = sequence->duration(data.settings.frameRate);
                info.canvas = QSize(sequence->canvas.width, sequence->canvas.height);
            }
        }
    }
    if (QFileInfo::exists(directory + u"/thumbnail.jpg"_s)) {
        info.thumbnailPath = directory + u"/thumbnail.jpg"_s;
    }
    info.openElsewhere = DraftLock::isHeld(directory);
    return info;
}

// Changes a closed draft's project file and keeps draft.json in step.
bool rewriteProject(const QString &directory, const std::function<void(ProjectData &)> &change, QString *error)
{
    if (DraftLock::isHeld(directory)) {
        if (error) {
            *error = QCoreApplication::translate("vedit::document::DraftStore", "This project is open: close it first.");
        }
        return false;
    }
    ProjectLoadResult loaded = projectfile::load(directory + u"/project.vproj"_s);
    if (!loaded.ok()) {
        if (error) {
            *error = loaded.error;
        }
        return false;
    }
    change(*loaded.project);
    if (const auto written = projectfile::save(directory + u"/project.vproj"_s, *loaded.project); !written) {
        if (error) {
            *error = written.error;
        }
        return false;
    }
    QFile::remove(directory + u"/draft.json"_s); // regenerated from the project on the next listing
    return true;
}

} // namespace

DraftStore::DraftStore(QString root)
    : m_root(std::move(root))
{
    QDir().mkpath(m_root);
}

QString DraftStore::defaultRoot()
{
    return paths::dataDir() + u"/drafts"_s;
}

QString DraftStore::directoryOf(const ProjectId &id) const
{
    return m_root + u"/"_s + id.toString();
}

QList<DraftInfo> DraftStore::list() const
{
    QList<DraftInfo> drafts;
    const QDir root(m_root);
    for (const QString &name : root.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (std::optional<DraftInfo> info = readInfo(root.filePath(name))) {
            drafts << *info;
        }
    }
    std::sort(drafts.begin(), drafts.end(),
              [](const DraftInfo &a, const DraftInfo &b) { return a.modifiedAt > b.modifiedAt; });
    return drafts;
}

std::optional<DraftInfo> DraftStore::info(const ProjectId &id) const
{
    return readInfo(directoryOf(id));
}

QString DraftStore::uniqueName(const QString &base) const
{
    QSet<QString> names;
    for (const DraftInfo &draft : list()) {
        names.insert(draft.name);
    }
    QString name = base;
    for (int n = 2; names.contains(name); ++n) {
        name = u"%1 (%2)"_s.arg(base).arg(n);
    }
    return name;
}

std::unique_ptr<Document> DraftStore::createDraft(QString *error) const
{
    // Named after the day, like CapCut; renamed with one click in the editor's top bar.
    const QString base = QLocale().toString(QDate::currentDate(), u"d MMMM"_s);
    ProjectData data = ProjectData::createEmpty(uniqueName(base));
    return Document::create(directoryOf(data.id), std::move(data), error);
}

std::unique_ptr<Document> DraftStore::openDraft(const ProjectId &id, QString *error) const
{
    return Document::open(directoryOf(id), error);
}

bool DraftStore::renameDraft(const ProjectId &id, const QString &name, QString *error) const
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty()) {
        if (error) {
            *error = QCoreApplication::translate("vedit::document::DraftStore", "The name cannot be empty.");
        }
        return false;
    }
    return rewriteProject(directoryOf(id), [&trimmed](ProjectData &data) { data.name = trimmed; }, error);
}

std::optional<ProjectId> DraftStore::duplicateDraft(const ProjectId &id, QString *error) const
{
    ProjectLoadResult loaded = projectfile::load(directoryOf(id) + u"/project.vproj"_s);
    if (!loaded.ok()) {
        if (error) {
            *error = loaded.error;
        }
        return std::nullopt;
    }
    ProjectData copy = std::move(*loaded.project);
    copy.id = ProjectId::create();
    copy.name = uniqueName(QCoreApplication::translate("vedit::document::DraftStore", "%1 copy").arg(copy.name));
    copy.createdAt = QDateTime::currentDateTimeUtc();
    copy.modifiedAt = copy.createdAt;
    const QString directory = directoryOf(copy.id);
    QDir().mkpath(directory);
    if (const auto written = projectfile::save(directory + u"/project.vproj"_s, copy); !written) {
        QDir(directory).removeRecursively(); // only the folder just created
        if (error) {
            *error = written.error;
        }
        return std::nullopt;
    }
    QFile::copy(directoryOf(id) + u"/thumbnail.jpg"_s, directory + u"/thumbnail.jpg"_s);
    QFile::copy(directoryOf(id) + u"/state.json"_s, directory + u"/state.json"_s);
    return copy.id;
}

bool DraftStore::removeDraft(const ProjectId &id, QString *error) const
{
    const QString directory = directoryOf(id);
    if (DraftLock::isHeld(directory)) {
        if (error) {
            *error = QCoreApplication::translate("vedit::document::DraftStore", "This project is open: close it first.");
        }
        return false;
    }
    // Qt picks the home trash only if the XDG data folder exists (verified: otherwise moveToTrash() fails, e.g.
    // on a fresh account); ~/.local/share/Trash is then created as needed.
    QDir().mkpath(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation));
    if (!QFile::moveToTrash(directory)) {
        if (error) {
            *error = QCoreApplication::translate("vedit::document::DraftStore", "The project could not be moved to the trash.");
        }
        return false;
    }
    return true;
}

} // namespace vedit::document
