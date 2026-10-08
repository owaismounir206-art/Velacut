// SPDX-License-Identifier: GPL-3.0-or-later
#include "MediaImporter.h"

#include "core/serialization/ProjectJson.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcImport, "velacut.engine.import")

using namespace Qt::StringLiterals;

namespace velacut::engine {

MediaImporter::MediaImporter(QObject *parent)
    : QObject(parent)
    , m_executable(QCoreApplication::applicationDirPath() + u"/velacut-render"_s)
{
}

MediaImporter::~MediaImporter()
{
    if (m_process) {
        m_process->disconnect(this);
        m_process->kill();
        m_process->waitForFinished(1000);
        delete m_process;
    }
}

QString MediaImporter::errorMessage(ProbeError error, const QString &fileName)
{
    switch (error) {
    case ProbeError::None:
        break;
    case ProbeError::Missing:
        return tr("“%1” no longer exists.").arg(fileName);
    case ProbeError::Unreadable:
        return tr("“%1” cannot be read: check that you have permission to open it.").arg(fileName);
    case ProbeError::Unsupported:
        return tr("“%1” is not a video, audio or image file that velacut can open.").arg(fileName);
    case ProbeError::Damaged:
        return tr("“%1” seems to be damaged and was not imported.").arg(fileName);
    }
    return {};
}

void MediaImporter::import(const QStringList &paths)
{
    m_queue += paths;
    if (!m_process) {
        startProcess();
    }
}

void MediaImporter::startProcess()
{
    const bool wasBusy = busy();
    m_running = std::exchange(m_queue, {});
    m_current.clear();
    m_buffer.clear();
    m_process = new QProcess(this);
    m_process->setProcessChannelMode(QProcess::ForwardedErrorChannel);
    connect(m_process, &QProcess::readyReadStandardOutput, this, &MediaImporter::onOutput);
    connect(m_process, &QProcess::finished, this, &MediaImporter::onFinished);
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart) {
            return; // crashes arrive through finished()
        }
        qCWarning(lcImport) << "cannot start" << m_executable;
        const QStringList all = m_running + m_queue;
        m_running.clear();
        m_queue.clear();
        m_process->deleteLater();
        m_process = nullptr;
        for (const QString &path : all) {
            emit failed(path, tr("“%1” was not imported: a part of velacut is missing (velacut-render). Reinstall velacut.")
                                  .arg(QFileInfo(path).fileName()));
        }
        emit busyChanged();
        emit finished();
    });
    m_process->start(m_executable, QStringList{u"--probe"_s} + m_running);
    if (!wasBusy) {
        emit busyChanged();
    }
}

void MediaImporter::onOutput()
{
    m_buffer += m_process->readAllStandardOutput();
    qsizetype end = 0;
    while ((end = m_buffer.indexOf('\n')) >= 0) {
        const QJsonObject event = QJsonDocument::fromJson(m_buffer.left(end)).object();
        m_buffer.remove(0, end + 1);
        const QString type = event.value(u"event"_s).toString();
        const QString path = event.value(u"path"_s).toString();
        if (type == u"probing"_s) {
            m_current = path;
        } else if (type == u"media"_s || type == u"media-error"_s) {
            m_running.removeOne(path);
            m_current.clear();
            QString error;
            const std::optional<Media> media = type == u"media"_s
                                                   ? projectjson::mediaFromJson(event.value(u"media"_s).toObject(), &error)
                                                   : std::nullopt;
            if (media) {
                emit imported(*media);
            } else {
                const ProbeError code = type == u"media"_s ? ProbeError::Unsupported
                                                            : probeErrorFromCode(event.value(u"code"_s).toString());
                qCInfo(lcImport) << "not imported:" << path << event.value(u"detail"_s).toString() << error;
                emit failed(path, errorMessage(code, QFileInfo(path).fileName()));
            }
        }
    }
}

void MediaImporter::onFinished(int exitCode, QProcess::ExitStatus status)
{
    if (!m_process) {
        return;
    }
    onOutput();
    m_process->deleteLater();
    m_process = nullptr;
    if (status == QProcess::CrashExit || exitCode != 0) {
        // The file being read crashed the probe: rejected; the files after it are read by a new process.
        const QString damaged = !m_current.isEmpty() ? m_current : (m_running.isEmpty() ? QString() : m_running.first());
        if (!damaged.isEmpty()) {
            qCWarning(lcImport) << "the probe stopped on" << damaged << "exit" << exitCode << status;
            m_running.removeOne(damaged);
            emit failed(damaged, errorMessage(ProbeError::Damaged, QFileInfo(damaged).fileName()));
        }
    }
    m_queue = m_running + m_queue;
    m_running.clear();
    m_current.clear();
    if (m_queue.isEmpty()) {
        emit busyChanged();
        emit finished();
        return;
    }
    startProcess(); // still busy: no busyChanged
}

} // namespace velacut::engine
