// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QAbstractListModel>
#include <QHash>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QtQml/qqmlregistration.h>

class QFile;
class QNetworkReply;

namespace vedit::ui {

// One download of a file to `destination`, written next to it and renamed when complete (never half a model).
// Any URL Qt can read (https for the models, file:// in the tests).
class FileDownload : public QObject
{
    Q_OBJECT

public:
    FileDownload(QNetworkAccessManager &network, const QUrl &url, QString destination, QObject *parent = nullptr);
    ~FileDownload() override;
    void cancel();
    qint64 received() const { return m_received; }
    qint64 total() const { return m_total; }

signals:
    void progress();
    // Empty error: done.
    void finished(const QString &error);

private:
    void onReadyRead();
    void onFinished();

    QPointer<QNetworkReply> m_reply;
    QString m_destination;
    QFile *m_file = nullptr;
    qint64 m_received = 0;
    qint64 m_total = 0;
    bool m_cancelled = false;
};

// The AI models of Preferences → AI models (SPEC §1: downloaded only when asked, with the size shown first): the
// speech models of whisper.cpp, installed or not, being downloaded, removable; and whether whisper.cpp itself is
// installed (with the command that installs it).
class AiModelsModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool whisperInstalled READ whisperInstalled NOTIFY componentsChanged FINAL)
    Q_PROPERTY(QString whisperPath READ whisperPath NOTIFY componentsChanged FINAL)
    Q_PROPERTY(QString whisperInstallCommand READ whisperInstallCommand CONSTANT FINAL)
    Q_PROPERTY(QString folder READ folder CONSTANT FINAL)

public:
    enum Role
    {
        ModelIdRole = Qt::UserRole + 1,
        NameRole,
        DetailRole,
        SizeRole,       // "142 MB"
        InstalledRole,
        DownloadingRole,
        ProgressRole,   // 0…1
    };

    explicit AiModelsModel(QObject *parent = nullptr);
    ~AiModelsModel() override;

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    bool whisperInstalled() const;
    QString whisperPath() const;
    QString whisperInstallCommand() const;
    QString folder() const;

    Q_INVOKABLE void download(const QString &modelId);
    Q_INVOKABLE void cancel(const QString &modelId);
    // An error for the user, empty when removed.
    Q_INVOKABLE QString remove(const QString &modelId);
    // Looks again for whisper.cpp (installed while vedit runs).
    Q_INVOKABLE void refresh();
    // Tests: where the models are downloaded from instead of Hugging Face.
    void setSourceOverride(const QUrl &base) { m_sourceOverride = base; }

signals:
    void componentsChanged();
    void message(const QString &text);

private:
    int rowOf(const QString &modelId) const;

    QNetworkAccessManager m_network;
    QHash<QString, FileDownload *> m_downloads; // children
    QUrl m_sourceOverride;
};

} // namespace vedit::ui
