// SPDX-License-Identifier: GPL-3.0-or-later
#include "RenderJob.h"

#include "engine/gpu/GpuTransitions.h"

#include "common/Paths.h"
#include "core/serialization/ProjectFile.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QStorageInfo>
#include <QStandardPaths>
#include <QUuid>

#include <cmath>

Q_LOGGING_CATEGORY(lcRenderJob, "velacut.engine.renderjob")

using namespace Qt::StringLiterals;

namespace velacut::engine {

namespace {

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

} // namespace

RenderJob::RenderJob(QObject *parent)
    : QObject(parent)
    , m_executable(defaultRenderExecutable())
{
}

RenderJob::~RenderJob()
{
    if (m_process) {
        m_process->disconnect(this);
        m_process->terminate();
        if (!m_process->waitForFinished(5000)) {
            m_process->kill();
            m_process->waitForFinished(1000);
        }
        delete m_process;
        m_process = nullptr;
        removePartialFiles();
        QFile::remove(m_frozenProject);
        QFile::remove(m_jobFile);
    }
}

int RenderJob::removeStaleJobFiles(std::chrono::seconds olderThan)
{
    const QDir directory(paths::cacheDir() + u"/render"_s);
    const QDateTime limit = QDateTime::currentDateTimeUtc().addSecs(-olderThan.count());
    int removed = 0;
    for (const QFileInfo &file : directory.entryInfoList({u"*.json"_s, u"*.vproj"_s}, QDir::Files)) {
        if (file.lastModified().toUTC() < limit && QFile::remove(file.absoluteFilePath())) {
            ++removed;
        }
    }
    if (removed > 0) {
        qCInfo(lcRenderJob) << "removed" << removed << "files of interrupted exports";
    }
    return removed;
}

QString RenderJob::errorMessage(RenderError error)
{
    switch (error) {
    case RenderError::None:
        break;
    case RenderError::MltUnavailable:
        return tr("The video engine could not be started.");
    case RenderError::ProjectUnreadable:
        return tr("The project could not be prepared for the export.");
    case RenderError::SequenceMissing:
        return tr("The timeline to export no longer exists.");
    case RenderError::NothingToExport:
        return tr("The timeline is empty: add a clip first.");
    case RenderError::OutputNotWritable:
        return tr("The video cannot be saved in this folder. Choose another folder.");
    case RenderError::EncoderFailed:
        return tr("The export stopped because of an encoding error.");
    }
    return {};
}

bool RenderJob::start(const ProjectData &project, const SequenceId &sequenceId, const ExportSettings &settings)
{
    if (m_process) {
        return false;
    }
    m_settings = settings;
    m_warnings.clear();
    m_errorCode.clear();
    m_errorDetail.clear();
    m_done = false;
    m_cancelled = false;
    m_frame = 0;
    m_total = 0;
    m_buffer.clear();

    const Sequence *sequence = project.findSequence(sequenceId);
    const RationalTime duration = sequence ? sequence->duration(settings.frameRate) : RationalTime();
    if (!sequence || duration.isZero() || exportedDuration(settings, duration).isZero()) {
        emit failed(errorMessage(RenderError::NothingToExport), {});
        return false;
    }
    // Check the space before starting rather than failing at 90% (SPEC §5.15).
    const QString folder = QFileInfo(settings.outputPath).absolutePath();
    const QStorageInfo storage(folder);
    const qint64 needed = estimatedFileSize(settings, exportedDuration(settings, duration));
    if (storage.isValid() && storage.bytesAvailable() >= 0 && storage.bytesAvailable() < needed) {
        emit failed(tr("There is not enough free space in this folder: about %1 MB are needed, %2 MB are free.")
                        .arg(std::ceil(needed / 1e6))
                        .arg(std::floor(storage.bytesAvailable() / 1e6)),
                    folder);
        return false;
    }

    const QString directory = paths::cacheDir() + u"/render"_s;
    QDir().mkpath(directory);
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_frozenProject = directory + u"/"_s + id + u".vproj"_s;
    m_jobFile = directory + u"/"_s + id + u".json"_s;
    if (const auto written = projectfile::save(m_frozenProject, project); !written) {
        emit failed(errorMessage(RenderError::ProjectUnreadable), written.error);
        return false;
    }
    QJsonObject encoders;
    encoders.insert(u"video"_s, QJsonArray::fromStringList(m_hardwareEncoders));
    // The export uses the GPU path of the transitions when the editor does (it passed its self-check here).
    const GpuTransitions *gpu = GpuTransitions::instance();
    const QJsonObject job{{u"project"_s, m_frozenProject},
                          {u"sequence"_s, sequenceId.toString()},
                          {u"settings"_s, settings.toJson()},
                          {u"encoders"_s, encoders},
                          {u"gpuEffects"_s, gpu && gpu->available()}};
    if (const auto written = projectfile::writeAtomically(m_jobFile, QJsonDocument(job).toJson()); !written) {
        QFile::remove(m_frozenProject);
        emit failed(errorMessage(RenderError::ProjectUnreadable), written.error);
        return false;
    }

    m_process = new QProcess(this);
    m_process->setProcessChannelMode(QProcess::SeparateChannels);
    connect(m_process, &QProcess::readyReadStandardOutput, this, &RenderJob::onOutput);
    connect(m_process, &QProcess::readyReadStandardError, this, [this] {
        // Technical log of the child (MLT, FFmpeg): kept in our log, the last lines become the error detail.
        const QByteArray text = m_process->readAllStandardError();
        qCDebug(lcRenderJob).noquote() << text.trimmed();
        m_errorDetail = QString::fromUtf8(text.right(2000)).trimmed();
    });
    connect(m_process, &QProcess::finished, this, &RenderJob::onFinished);
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            onFinished(-1, QProcess::CrashExit);
        }
    });
    m_elapsed.start();
    qCInfo(lcRenderJob) << "starting" << m_executable << "for" << settings.outputPath;
    m_process->start(m_executable, {u"--job"_s, m_jobFile});
    emit runningChanged();
    emit progressChanged();
    return true;
}

void RenderJob::cancel()
{
    if (m_process && m_process->state() != QProcess::NotRunning) {
        m_cancelled = true;
        m_process->terminate(); // SIGTERM: velacut-render stops and removes its partial file
    }
}

int RenderJob::secondsLeft() const
{
    if (!m_process || m_frame <= 0 || m_total <= 0 || m_elapsed.elapsed() < 1000) {
        return -1;
    }
    const double perFrame = static_cast<double>(m_elapsed.elapsed()) / m_frame;
    return static_cast<int>(std::ceil(perFrame * (m_total - m_frame) / 1000.0));
}

void RenderJob::onOutput()
{
    m_buffer += m_process->readAllStandardOutput();
    qsizetype end = 0;
    while ((end = m_buffer.indexOf('\n')) >= 0) {
        const QByteArray line = m_buffer.left(end);
        m_buffer.remove(0, end + 1);
        const QJsonObject event = QJsonDocument::fromJson(line).object();
        if (!event.isEmpty()) {
            handleEvent(event);
        }
    }
}

void RenderJob::handleEvent(const QJsonObject &event)
{
    const QString type = event.value(u"event"_s).toString();
    if (type == u"progress"_s) {
        m_frame = event.value(u"frame"_s).toInt();
        m_total = event.value(u"total"_s).toInt();
        emit progressChanged();
    } else if (type == u"warning"_s) {
        m_warnings << event.value(u"message"_s).toString();
    } else if (type == u"done"_s) {
        m_done = true;
    } else if (type == u"cancelled"_s) {
        m_cancelled = true;
    } else if (type == u"error"_s) {
        m_errorCode = event.value(u"code"_s).toString();
        m_errorDetail = event.value(u"detail"_s).toString();
    }
}

void RenderJob::onFinished(int exitCode, QProcess::ExitStatus status)
{
    if (!m_process) {
        return;
    }
    onOutput();
    const bool crashed = status == QProcess::CrashExit && !m_cancelled;
    qCInfo(lcRenderJob) << "velacut-render finished with" << exitCode << (crashed ? "(crash)" : "");
    const bool done = m_done && exitCode == 0;
    const bool cancelledRun = m_cancelled && !done;
    const QString errorCode = m_errorCode;
    const QString detail = m_errorDetail;
    finish();
    if (!done) {
        removePartialFiles(); // normally already removed by velacut-render, unless it was killed
    }
    if (done) {
        emit finished(m_settings.outputPath, m_warnings);
    } else if (cancelledRun) {
        emit cancelled();
    } else if (crashed || errorCode.isEmpty()) {
        emit failed(tr("The export stopped unexpectedly."), detail);
    } else {
        emit failed(errorMessage(renderErrorFromCode(errorCode)), detail);
    }
}

void RenderJob::finish()
{
    m_process->deleteLater();
    m_process = nullptr;
    QFile::remove(m_frozenProject);
    QFile::remove(m_jobFile);
    emit runningChanged();
    emit progressChanged();
}

void RenderJob::removePartialFiles() const
{
    // Left only if velacut-render was killed: ".<name>.part-XXXXXXXX.<ext>" next to the output (see Renderer), with
    // ".source.mp4" for a GIF, or the hidden folder ".<name>.part-XXXXXXXX" of pictures.
    const QFileInfo output(m_settings.outputPath);
    QDir folder(output.absolutePath());
    if (m_settings.format == ExportFormat::Images) {
        for (const QString &name : folder.entryList({u"."_s + output.fileName() + u".part-*"_s}, QDir::Dirs | QDir::Hidden)) {
            QDir(folder.filePath(name)).removeRecursively();
        }
        return;
    }
    for (const QString &name : folder.entryList({u"."_s + output.completeBaseName() + u".part-*."_s + output.suffix() + u'*'},
                                                QDir::Files | QDir::Hidden)) {
        folder.remove(name);
    }
}

} // namespace velacut::engine
