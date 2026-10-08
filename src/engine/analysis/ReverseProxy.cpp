// SPDX-License-Identifier: GPL-3.0-or-later
#include "ReverseProxy.h"

#include "common/Paths.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcReverse, "velacut.engine.reverse")

using namespace Qt::StringLiterals;

namespace velacut::engine {

QString reverseProxyPath(const Media &media)
{
    return paths::cacheDir() + u"/proxy/"_s + media.fingerprint.value + u"-reverse.mp4"_s;
}

bool reverseProxyReady(const Media &media)
{
    return media.fingerprint.isValid() && QFileInfo::exists(reverseProxyPath(media));
}

ReverseProxyQueue::ReverseProxyQueue(QObject *parent)
    : QObject(parent)
    , m_executable(QCoreApplication::applicationDirPath() + u"/velacut-render"_s)
{
}

ReverseProxyQueue::~ReverseProxyQueue()
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

void ReverseProxyQueue::request(const Media &media)
{
    if (media.kind == MediaKind::Image || m_known.contains(media.id) || reverseProxyReady(media)) {
        return;
    }
    m_known.insert(media.id);
    m_queue.push_back(media);
    if (!m_process) {
        startNext();
    }
}

void ReverseProxyQueue::startNext()
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
    connect(m_process, &QProcess::readyReadStandardOutput, this, &ReverseProxyQueue::onOutput);
    connect(m_process, &QProcess::finished, this, &ReverseProxyQueue::onFinished);
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            onFinished(-1, QProcess::CrashExit);
        }
    });
    qCInfo(lcReverse) << "preparing the backwards copy of" << m_current.path;
    m_process->start(m_executable, {u"--backwards"_s, m_current.path, u"--output"_s, reverseProxyPath(m_current)});
    if (!wasBusy) {
        emit busyChanged();
    }
    emit progressChanged();
}

void ReverseProxyQueue::onOutput()
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

void ReverseProxyQueue::onFinished(int exitCode, QProcess::ExitStatus status)
{
    if (!m_process) {
        return;
    }
    onOutput();
    m_process->deleteLater();
    m_process = nullptr;
    const Media media = m_current;
    if (status == QProcess::NormalExit && exitCode == 0 && reverseProxyReady(media)) {
        qCInfo(lcReverse) << "backwards copy ready:" << media.path;
        emit ready(media.id);
    } else {
        // Stays in m_known: not retried in this session; the clip keeps playing from the original.
        qCWarning(lcReverse) << "backwards copy failed for" << media.path << exitCode;
        emit failed(media.id);
    }
    startNext();
}

} // namespace velacut::engine
