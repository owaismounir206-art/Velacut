// SPDX-License-Identifier: GPL-3.0-or-later
#include "AudioLibraryModel.h"

#include "common/DevSandbox.h"
#include "engine/analysis/MediaImporter.h"
#include "ui/models/MediaPoolModel.h"

#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QPointer>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QThreadPool>

using namespace Qt::StringLiterals;

namespace velacut::ui {

AudioLibraryModel::AudioLibraryModel(QString folder, QString probeExecutable, QObject *parent)
    : QAbstractListModel(parent)
    , m_folder(std::move(folder))
    , m_importer(std::make_unique<engine::MediaImporter>())
{
    m_importer->setExecutable(probeExecutable);
    connect(m_importer.get(), &engine::MediaImporter::imported, this, [this](const Media &media) {
        m_probed.insert(media.path, media);
        const qsizetype row = m_files.indexOf(media.path);
        if (row >= 0) {
            emit dataChanged(index(static_cast<int>(row)), index(static_cast<int>(row)));
        }
    });
    connect(m_importer.get(), &engine::MediaImporter::failed, this, [this](const QString &path, const QString &) {
        // Not playable: not listed.
        const qsizetype row = m_files.indexOf(path);
        if (row >= 0) {
            beginRemoveRows({}, static_cast<int>(row), static_cast<int>(row));
            m_files.removeAt(row);
            endRemoveRows();
            emit countChanged();
        }
    });
    connect(m_importer.get(), &engine::MediaImporter::finished, this, [this] {
        m_loading = false;
        emit loadingChanged();
    });
}

AudioLibraryModel::~AudioLibraryModel() = default;

QString AudioLibraryModel::defaultFolder()
{
    // Tests point the library to their own folder.
    if (const QString folder = qEnvironmentVariable("VELACUT_MUSIC_DIR"); !folder.isEmpty()) {
        return folder;
    }
    // xdg-user-dirs: XDG_MUSIC_DIR="$HOME/Musica" (localized name), in the user's real configuration.
    QFile file(hostConfigHome() + u"/user-dirs.dirs"_s);
    if (file.open(QIODevice::ReadOnly)) {
        static const QRegularExpression line(u"^XDG_MUSIC_DIR=\"(.*)\"\\s*$"_s, QRegularExpression::MultilineOption);
        const QRegularExpressionMatch match = line.match(QString::fromUtf8(file.readAll()));
        if (match.hasMatch()) {
            QString path = match.captured(1);
            path.replace(u"$HOME"_s, QDir::homePath());
            if (QFileInfo(path).isDir()) {
                return path;
            }
        }
    }
    return QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
}

int AudioLibraryModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_files.size());
}

QVariant AudioLibraryModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_files.size()) {
        return {};
    }
    const QString &path = m_files[index.row()];
    const auto probed = m_probed.constFind(path);
    switch (role) {
    case PathRole:
        return path;
    case NameRole:
        return QFileInfo(path).completeBaseName();
    case DurationTextRole:
        return probed != m_probed.constEnd() && probed->info.duration
                   ? MediaPoolModel::durationText(probed->info.duration->toSecondsDouble())
                   : QString();
    case ReadyRole:
        return probed != m_probed.constEnd();
    default:
        return {};
    }
}

QHash<int, QByteArray> AudioLibraryModel::roleNames() const
{
    return {{PathRole, "path"}, {NameRole, "name"}, {DurationTextRole, "durationText"}, {ReadyRole, "ready"}};
}

void AudioLibraryModel::load()
{
    if (m_loaded || m_folder.isEmpty()) {
        return;
    }
    m_loaded = true;
    m_loading = true;
    emit loadingChanged();
    const quint64 generation = ++m_generation;
    QPointer<AudioLibraryModel> self(this);
    const QString folder = m_folder;
    // Listing a big folder can take a while on a slow disk: not on the UI thread.
    QThreadPool::globalInstance()->start([self, folder, generation] {
        QStringList files;
        QDirIterator it(folder,
                        {u"*.mp3"_s, u"*.flac"_s, u"*.ogg"_s, u"*.opus"_s, u"*.m4a"_s, u"*.aac"_s, u"*.wav"_s, u"*.wma"_s},
                        QDir::Files | QDir::Readable, QDirIterator::Subdirectories | QDirIterator::FollowSymlinks);
        while (it.hasNext() && files.size() < kMaxFiles) {
            files << it.next();
        }
        files.sort(Qt::CaseInsensitive);
        QMetaObject::invokeMethod(self.get(), [self, files, generation] {
            if (self && generation == self->m_generation) {
                self->setFiles(files);
            }
        });
    });
}

void AudioLibraryModel::reload()
{
    m_loaded = false;
    m_probed.clear();
    load();
}

void AudioLibraryModel::setFiles(const QStringList &files)
{
    beginResetModel();
    m_files = files;
    endResetModel();
    emit countChanged();
    if (files.isEmpty()) {
        m_loading = false;
        emit loadingChanged();
        return;
    }
    m_importer->import(files);
}

std::optional<Media> AudioLibraryModel::media(int row) const
{
    if (row < 0 || row >= m_files.size()) {
        return std::nullopt;
    }
    const auto probed = m_probed.constFind(m_files[row]);
    if (probed == m_probed.constEnd()) {
        return std::nullopt;
    }
    return *probed;
}

} // namespace velacut::ui
