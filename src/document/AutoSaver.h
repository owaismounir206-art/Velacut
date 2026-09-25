// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/serialization/ProjectFile.h"

#include <QImage>
#include <QObject>
#include <QThreadPool>
#include <QTimer>

#include <memory>

namespace vedit {
class Project;
}

namespace vedit::document {

// Continuous save of a draft (docs/FILE_FORMAT.md §9.1): after a change, waits for 300 ms without changes but never
// more than 2 s during continuous editing; serializes on the calling (UI) thread and writes atomically on a single
// I/O thread. Unchanged content is not written again. A failed write keeps the state in memory, is reported and is
// retried with a growing delay.
class AutoSaver : public QObject
{
    Q_OBJECT

public:
    enum class State
    {
        Saved,
        Pending, // changes waiting for the debounce
        Saving,
        Failed,
    };

    static constexpr int kIdleDelayMs = 300;
    static constexpr int kMaxDelayMs = 2000;

    AutoSaver(Project &project, QString directory, QObject *parent = nullptr);
    ~AutoSaver() override;

    State state() const { return m_state; }
    QString error() const { return m_error; }
    // Number of completed writes of project.vproj (for tests and diagnostics).
    int writeCount() const { return m_writeCount; }

    // Saves as soon as possible (e.g. the window lost focus).
    void saveNow();
    // Written with the next save.
    void setThumbnail(const QImage &image);
    void setUiState(const QByteArray &json);
    // Writes everything now and waits (closing the project or the app).
    projectfile::WriteResult flush();

signals:
    void stateChanged();
    void saved();

private:
    struct Job;
    struct Outcome;

    void onProjectChanged();
    std::shared_ptr<Job> prepareJob();
    void startSave();
    void onJobFinished(const Job &job, const projectfile::WriteResult &result);
    static projectfile::WriteResult runJob(const Job &job);
    void setState(State state, const QString &error = {});

    Project &m_project;
    QString m_directory;
    QThreadPool m_io; // one thread: one writer per project
    QTimer m_idle;
    QTimer m_maxDelay;
    QTimer m_retry;
    int m_retryDelayMs = 1000;
    QByteArray m_savedContent; // last content written (without the modification date)
    QImage m_thumbnail;
    QByteArray m_uiState;
    bool m_writing = false;
    bool m_again = false;
    quint64 m_serial = 0;
    quint64 m_handled = 0; // last job whose result was applied
    int m_writeCount = 0;
    State m_state = State::Saved;
    QString m_error;
};

} // namespace vedit::document
