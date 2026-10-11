// SPDX-License-Identifier: GPL-3.0-or-later
#include "MediaCopyQueue.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QStandardPaths>

#include <algorithm>

Q_LOGGING_CATEGORY(lcCopies, "velacut.engine.copies")

using namespace Qt::StringLiterals;

namespace velacut::engine {

QString defaultRenderExecutable()
{
    const QString nextToApp = QCoreApplication::applicationDirPath() + u"/velacut-render"_s;
    if (QFile::exists(nextToApp)) {
        return nextToApp;
    }
    const QString inPath = QStandardPaths::findExecutable(u"velacut-render"_s);
    if (!inPath.isEmpty()) {
        return inPath;
    }
    return nextToApp;
}

MediaCopyQueue::MediaCopyQueue(QString what, Output output, Arguments arguments, QObject *parent)
    : QObject(parent)
    , m_what(std::move(what))
    , m_output(std::move(output))
    , m_arguments(std::move(arguments))
    , m_executable(defaultRenderExecutable())
{
}

MediaCopyQueue::~MediaCopyQueue()
{
    if (m_process) {
        m_process->disconnect(this);
        m_process->terminate(); // velacut-render removes its partial file
        if (!m_process->waitForFinished(5000)) {
            m_process->kill();
            m_process->waitForFinished(1000);
        }
        delete m_process;
    }
}

bool MediaCopyQueue::isReady(const Media &media) const
{
    return media.fingerprint.isValid() && QFileInfo::exists(m_output(media));
}

bool MediaCopyQueue::queued(const MediaId &mediaId) const
{
    return std::any_of(m_queue.begin(), m_queue.end(), [&](const Media &media) { return media.id == mediaId; });
}

void MediaCopyQueue::request(const Media &media)
{
    if (media.kind == MediaKind::Image || m_known.contains(media.id) || isReady(media)) {
        return;
    }
    m_known.insert(media.id);
    m_queue.push_back(media);
    if (!m_process) {
        startNext();
    }
}

void MediaCopyQueue::startNext()
{
    if (m_queue.empty()) {
        emit busyChanged();
        return;
    }
    const bool wasBusy = busy();
    m_current = m_queue.front();
    m_queue.pop_front();
    m_buffer.clear();
    m_progress = 0.0;
    m_process = new QProcess(this);
    m_process->setProcessChannelMode(QProcess::ForwardedErrorChannel);
    connect(m_process, &QProcess::readyReadStandardOutput, this, &MediaCopyQueue::onOutput);
    connect(m_process, &QProcess::finished, this, &MediaCopyQueue::onFinished);
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            onFinished(-1, QProcess::CrashExit);
        }
    });
    qCInfo(lcCopies).noquote() << "preparing the" << m_what << "of" << m_current.path;
    m_process->start(m_executable, m_arguments(m_current, m_output(m_current)));
    if (!wasBusy) {
        emit busyChanged();
    }
    emit progressChanged();
}

void MediaCopyQueue::onOutput()
{
    m_buffer += m_process->readAllStandardOutput();
    qsizetype end = 0;
    while ((end = m_buffer.indexOf('\n')) >= 0) {
        const QJsonObject event = QJsonDocument::fromJson(m_buffer.left(end)).object();
        m_buffer.remove(0, end + 1);
        if (event.value(u"event"_s).toString() == u"progress"_s) {
            const int total = event.value(u"total"_s).toInt();
            m_progress = total > 0 ? event.value(u"frame"_s).toDouble() / total : 0.0;
            emit progressChanged();
        }
    }
}

void MediaCopyQueue::onFinished(int exitCode, QProcess::ExitStatus status)
{
    if (!m_process) {
        return;
    }
    onOutput();
    m_process->deleteLater();
    m_process = nullptr;
    const Media media = m_current;
    if (status == QProcess::NormalExit && exitCode == 0 && isReady(media)) {
        qCInfo(lcCopies).noquote() << m_what << "ready:" << media.path;
        emit ready(media.id);
    } else {
        // Stays in m_known: not retried in this session; the preview keeps reading the original.
        qCWarning(lcCopies).noquote() << m_what << "failed for" << media.path << exitCode;
        emit failed(media.id);
    }
    startNext();
}

} // namespace velacut::engine
