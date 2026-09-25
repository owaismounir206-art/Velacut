// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/project/Media.h"

#include <QAbstractListModel>
#include <QtQml/qqmlregistration.h>
#include <QHash>

#include <memory>
#include <optional>

namespace vedit::engine {
class MediaImporter;
}

namespace vedit::ui {

// Local music library (SPEC 0bis rule 4, usability test 2): the audio files of the user's Music folder, read by the
// probe process in background; each one can be added under the video with its "+".
class AudioLibraryModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by the controllers")
    Q_PROPERTY(QString folder READ folder CONSTANT FINAL)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged FINAL)
    Q_PROPERTY(int count READ count NOTIFY countChanged FINAL)

public:
    enum Role
    {
        PathRole = Qt::UserRole + 1,
        NameRole,
        DurationTextRole,
        ReadyRole, // probed: can be added
    };

    static constexpr int kMaxFiles = 1000;

    AudioLibraryModel(QString folder, QString probeExecutable, QObject *parent = nullptr);
    ~AudioLibraryModel() override;
    // The Music folder of the desktop (xdg-user-dirs, e.g. ~/Musica), read from the user's real configuration.
    static QString defaultFolder();

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    QString folder() const { return m_folder; }
    bool loading() const { return m_loading; }
    int count() const { return rowCount(); }

    // Scans the folder (once; again after reload()).
    Q_INVOKABLE void load();
    Q_INVOKABLE void reload();
    std::optional<Media> media(int row) const;

signals:
    void loadingChanged();
    void countChanged();

private:
    void setFiles(const QStringList &files);

    QString m_folder;
    std::unique_ptr<engine::MediaImporter> m_importer;
    QStringList m_files;
    QHash<QString, Media> m_probed; // by path
    bool m_loading = false;
    bool m_loaded = false;
    quint64 m_generation = 0;
};

} // namespace vedit::ui
