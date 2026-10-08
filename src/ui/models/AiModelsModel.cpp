// SPDX-License-Identifier: GPL-3.0-or-later
#include "AiModelsModel.h"

#include "ai/Speech.h"
#include "ai/Whisper.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QNetworkReply>
#include <QNetworkRequest>

using namespace Qt::StringLiterals;

namespace velacut::ui {

FileDownload::FileDownload(QNetworkAccessManager &network, const QUrl &url, QString destination, QObject *parent)
    : QObject(parent)
    , m_destination(std::move(destination))
{
    QDir().mkpath(QFileInfo(m_destination).absolutePath());
    m_file = new QFile(m_destination + u".part"_s, this);
    if (!m_file->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMetaObject::invokeMethod(this, [this] { emit finished(tr("%1 cannot be written.").arg(m_file->fileName())); },
                                  Qt::QueuedConnection);
        return;
    }
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    m_reply = network.get(request);
    connect(m_reply, &QNetworkReply::readyRead, this, &FileDownload::onReadyRead);
    connect(m_reply, &QNetworkReply::downloadProgress, this, [this](qint64 received, qint64 total) {
        m_received = received;
        m_total = total;
        emit progress();
    });
    connect(m_reply, &QNetworkReply::finished, this, &FileDownload::onFinished);
}

FileDownload::~FileDownload()
{
    if (m_reply) {
        m_reply->disconnect(this);
        m_reply->abort();
        m_reply->deleteLater();
    }
    if (m_file && m_file->isOpen()) {
        m_file->close();
        m_file->remove();
    }
}

void FileDownload::cancel()
{
    m_cancelled = true;
    if (m_reply) {
        m_reply->abort();
    }
}

void FileDownload::onReadyRead()
{
    if (m_reply && m_file->isOpen()) {
        m_file->write(m_reply->readAll());
    }
}

void FileDownload::onFinished()
{
    onReadyRead();
    const QString error = m_cancelled ? tr("Download cancelled.")
                        : m_reply->error() != QNetworkReply::NoError
                            ? tr("The download failed: %1").arg(m_reply->errorString())
                            : QString();
    m_reply->deleteLater();
    m_file->close();
    if (!error.isEmpty() || m_file->size() == 0) {
        m_file->remove();
        emit finished(error.isEmpty() ? tr("The download is empty.") : error);
        return;
    }
    QFile::remove(m_destination);
    if (!m_file->rename(m_destination)) {
        m_file->remove();
        emit finished(tr("%1 cannot be written.").arg(m_destination));
        return;
    }
    emit finished({});
}

AiModelsModel::AiModelsModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

AiModelsModel::~AiModelsModel() = default;

int AiModelsModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(ai::whisper::catalog().size());
}

QVariant AiModelsModel::data(const QModelIndex &index, int role) const
{
    const auto &models = ai::whisper::catalog();
    if (!index.isValid() || index.row() >= static_cast<int>(models.size())) {
        return {};
    }
    const ai::whisper::Model &model = models[static_cast<size_t>(index.row())];
    const FileDownload *download = m_downloads.value(model.id);
    switch (role) {
    case ModelIdRole:
        return model.id;
    case NameRole:
    case Qt::DisplayRole:
        return model.id == u"tiny"_s    ? tr("Speech — very fast")
             : model.id == u"base"_s    ? tr("Speech — fast")
             : model.id == u"small"_s   ? tr("Speech — accurate")
                                        : tr("Speech — most accurate");
    case DetailRole:
        return model.id == u"tiny"_s    ? tr("For a quick draft of the captions.")
             : model.id == u"base"_s    ? tr("Good for clear speech; the suggested one.")
             : model.id == u"small"_s   ? tr("Better with accents, noise and fast speech.")
                                        : tr("The best result, slowly: for long work done once.");
    case SizeRole:
        return QLocale().formattedDataSize(model.bytes, 0, QLocale::DataSizeSIFormat);
    case InstalledRole:
        return ai::whisper::installed(model.id);
    case DownloadingRole:
        return download != nullptr;
    case ProgressRole:
        return download && download->total() > 0 ? double(download->received()) / double(download->total()) : 0.0;
    default:
        return {};
    }
}

QHash<int, QByteArray> AiModelsModel::roleNames() const
{
    return {{ModelIdRole, "modelId"},     {NameRole, "name"},
            {DetailRole, "detail"},       {SizeRole, "size"},
            {InstalledRole, "installed"}, {DownloadingRole, "downloading"},
            {ProgressRole, "progress"}};
}

bool AiModelsModel::whisperInstalled() const
{
    return !ai::whisper::executable().isEmpty();
}

QString AiModelsModel::whisperPath() const
{
    return ai::whisper::executable();
}

QString AiModelsModel::whisperInstallCommand() const
{
    return ai::whisper::installCommand();
}

QString AiModelsModel::folder() const
{
    return ai::whisper::modelsFolder();
}

int AiModelsModel::rowOf(const QString &modelId) const
{
    const auto &models = ai::whisper::catalog();
    for (size_t i = 0; i < models.size(); ++i) {
        if (models[i].id == modelId) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

bool AiModelsModel::piperInstalled() const
{
    return !ai::piper::executable().isEmpty();
}

QString AiModelsModel::piperInstallCommand() const
{
    return ai::piper::installCommand();
}

QVariantList AiModelsModel::voices() const
{
    QVariantList result;
    for (const QString &path : ai::piper::voices()) {
        result << QVariantMap{{u"path"_s, path}, {u"name"_s, ai::piper::voiceName(path)}};
    }
    return result;
}

QString AiModelsModel::addVoice(const QUrl &file)
{
    const QString error = ai::piper::addVoice(file.isLocalFile() ? file.toLocalFile() : file.toString());
    if (error.isEmpty()) {
        emit voicesChanged();
    }
    return error;
}

QString AiModelsModel::removeVoice(const QString &path)
{
    const QString error = ai::piper::removeVoice(path);
    emit voicesChanged();
    return error;
}

void AiModelsModel::download(const QString &modelId)
{
    const ai::whisper::Model *model = ai::whisper::model(modelId);
    const int row = rowOf(modelId);
    if (!model || row < 0 || m_downloads.contains(modelId) || ai::whisper::installed(modelId)) {
        return;
    }
    const QUrl url = m_sourceOverride.isEmpty() ? QUrl(model->url) : m_sourceOverride.resolved(QUrl(model->file));
    auto *download = new FileDownload(m_network, url, ai::whisper::modelPath(modelId), this);
    m_downloads.insert(modelId, download);
    connect(download, &FileDownload::progress, this, [this, row] {
        emit dataChanged(index(row), index(row), {ProgressRole});
    });
    connect(download, &FileDownload::finished, this, [this, modelId, row, download](const QString &error) {
        m_downloads.remove(modelId);
        download->deleteLater();
        emit dataChanged(index(row), index(row));
        emit message(error.isEmpty() ? tr("Model downloaded: speech recognition is ready.") : error);
    });
    emit dataChanged(index(row), index(row));
}

void AiModelsModel::cancel(const QString &modelId)
{
    if (FileDownload *download = m_downloads.value(modelId)) {
        download->cancel();
    }
}

QString AiModelsModel::remove(const QString &modelId)
{
    const int row = rowOf(modelId);
    if (row < 0 || m_downloads.contains(modelId)) {
        return {};
    }
    if (!QFile::remove(ai::whisper::modelPath(modelId)) && ai::whisper::installed(modelId)) {
        return tr("The model cannot be removed.");
    }
    emit dataChanged(index(row), index(row));
    return {};
}

void AiModelsModel::refresh()
{
    emit componentsChanged();
}

} // namespace velacut::ui
