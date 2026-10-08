// SPDX-License-Identifier: GPL-3.0-or-later
#include "AutoSaver.h"

#include "core/project/Project.h"
#include "core/serialization/ProjectJson.h"

#include <QBuffer>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QPointer>

Q_LOGGING_CATEGORY(lcAutoSave, "velacut.document.autosave")

using namespace Qt::StringLiterals;

namespace velacut::document {

struct AutoSaver::Job
{
    quint64 serial = 0;
    QString directory;
    QByteArray project;  // null: unchanged
    QByteArray content;  // what `project` contains, without the date (to compare with the next save)
    QByteArray draft;    // draft.json (home screen cache)
    QImage thumbnail;    // null: unchanged
    QByteArray uiState;  // null: unchanged
};

namespace {

QByteArray draftInfo(const ProjectData &data, const QString &modifiedAt)
{
    const Sequence *sequence = data.mainSequence();
    QJsonObject info{{u"name"_s, data.name},
                     {u"modifiedAt"_s, modifiedAt},
                     {u"formatVersion"_s, kProjectFormatVersion}};
    if (sequence) {
        info.insert(u"duration"_s, sequence->duration(data.settings.frameRate).toString());
        info.insert(u"canvas"_s, QJsonObject{{u"width"_s, sequence->canvas.width}, {u"height"_s, sequence->canvas.height}});
    }
    return QJsonDocument(info).toJson(QJsonDocument::Indented);
}

} // namespace

AutoSaver::AutoSaver(Project &project, QString directory, QObject *parent)
    : QObject(parent)
    , m_project(project)
    , m_directory(std::move(directory))
{
    m_io.setMaxThreadCount(1);
    m_idle.setSingleShot(true);
    m_idle.setInterval(kIdleDelayMs);
    m_maxDelay.setSingleShot(true);
    m_maxDelay.setInterval(kMaxDelayMs);
    m_retry.setSingleShot(true);
    connect(&m_idle, &QTimer::timeout, this, &AutoSaver::startSave);
    connect(&m_maxDelay, &QTimer::timeout, this, &AutoSaver::startSave);
    connect(&m_retry, &QTimer::timeout, this, &AutoSaver::startSave);
    connect(&m_project, &Project::changed, this, &AutoSaver::onProjectChanged);
    // The project on disk is the one just opened or created.
    m_savedContent = projectjson::toBytes(m_project.data());
}

AutoSaver::~AutoSaver()
{
    m_io.waitForDone();
}

void AutoSaver::setState(State state, const QString &error)
{
    if (state != m_state || error != m_error) {
        m_state = state;
        m_error = error;
        emit stateChanged();
    }
}

void AutoSaver::onProjectChanged()
{
    if (m_state != State::Failed) {
        setState(State::Pending);
    }
    m_idle.start(); // restarts: 300 ms without changes
    if (!m_maxDelay.isActive()) {
        m_maxDelay.start(); // but at most 2 s after the first unsaved change
    }
}

void AutoSaver::saveNow()
{
    startSave();
}

void AutoSaver::setThumbnail(const QImage &image)
{
    m_thumbnail = image;
    if (!m_idle.isActive()) {
        m_idle.start();
    }
}

void AutoSaver::setUiState(const QByteArray &json)
{
    m_uiState = json;
}

std::shared_ptr<AutoSaver::Job> AutoSaver::prepareJob()
{
    const ProjectData &data = m_project.data();
    auto job = std::make_shared<Job>();
    job->serial = ++m_serial;
    job->directory = m_directory;
    // Serialization on this thread (the model is not thread-safe); the file gets the save time as modifiedAt.
    QJsonObject json = projectjson::toJson(data);
    job->content = QJsonDocument(json).toJson(QJsonDocument::Indented);
    if (job->content != m_savedContent) {
        const QString now = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
        json.insert(u"modifiedAt"_s, now);
        job->project = QJsonDocument(json).toJson(QJsonDocument::Indented);
        job->draft = draftInfo(data, now);
    }
    job->thumbnail = std::exchange(m_thumbnail, QImage());
    job->uiState = std::exchange(m_uiState, QByteArray());
    return job;
}

projectfile::WriteResult AutoSaver::runJob(const Job &job)
{
    // I/O thread (or the caller of flush()).
    if (!job.project.isNull()) {
        if (auto result = projectfile::writeAtomically(job.directory + u"/project.vproj"_s, job.project); !result) {
            return result;
        }
        // Cache for the home screen: a failure here loses nothing (regenerated from project.vproj).
        projectfile::writeAtomically(job.directory + u"/draft.json"_s, job.draft);
    }
    if (!job.thumbnail.isNull()) {
        QByteArray jpeg;
        QBuffer buffer(&jpeg);
        buffer.open(QIODevice::WriteOnly);
        job.thumbnail.save(&buffer, "JPG", 85);
        projectfile::writeAtomically(job.directory + u"/thumbnail.jpg"_s, jpeg);
    }
    if (!job.uiState.isNull()) {
        projectfile::writeAtomically(job.directory + u"/state.json"_s, job.uiState);
    }
    projectfile::WriteResult ok;
    ok.ok = true;
    return ok;
}

void AutoSaver::startSave()
{
    m_idle.stop();
    m_maxDelay.stop();
    m_retry.stop();
    if (m_writing) {
        m_again = true; // one more save when the running one ends, with the latest state
        return;
    }
    const std::shared_ptr<Job> job = prepareJob();
    if (job->project.isNull() && job->thumbnail.isNull() && job->uiState.isNull()) {
        if (m_state != State::Failed) {
            setState(State::Saved);
        }
        return;
    }
    m_writing = true;
    setState(State::Saving, m_error);
    QPointer<AutoSaver> self(this);
    m_io.start([self, job] {
        const projectfile::WriteResult result = runJob(*job);
        QMetaObject::invokeMethod(
            self.get(),
            [self, job, result] {
                if (self) {
                    self->onJobFinished(*job, result);
                }
            },
            Qt::QueuedConnection);
    });
}

void AutoSaver::onJobFinished(const Job &job, const projectfile::WriteResult &result)
{
    if (job.serial <= m_handled) {
        return; // superseded by flush()
    }
    m_handled = job.serial;
    m_writing = false;
    if (result && !job.project.isNull()) {
        m_savedContent = job.content;
        ++m_writeCount;
    }
    if (!result) {
        qCWarning(lcAutoSave) << "save failed:" << result.error << "- retrying in" << m_retryDelayMs << "ms";
        setState(State::Failed, result.error);
        m_retry.start(m_retryDelayMs);
        m_retryDelayMs = std::min(m_retryDelayMs * 2, 30000);
        return;
    }
    m_retryDelayMs = 1000;
    setState(State::Saved);
    emit saved();
    if (std::exchange(m_again, false)) {
        startSave();
    }
}

projectfile::WriteResult AutoSaver::flush()
{
    m_idle.stop();
    m_maxDelay.stop();
    m_retry.stop();
    m_io.waitForDone(); // the running write (its result is applied below or by its queued callback)
    const std::shared_ptr<Job> job = prepareJob();
    m_again = false;
    m_writing = false;
    m_handled = job->serial; // results of earlier jobs still queued are obsolete
    const projectfile::WriteResult result = runJob(*job);
    if (result) {
        if (!job->project.isNull()) {
            m_savedContent = job->content;
            ++m_writeCount;
        }
        m_retryDelayMs = 1000;
        setState(State::Saved);
    } else {
        setState(State::Failed, result.error);
    }
    return result;
}

} // namespace velacut::document
