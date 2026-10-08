// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/project/Media.h"
#include "engine/analysis/MediaProbe.h"

#include <QObject>
#include <QProcess>
#include <QStringList>

namespace velacut::engine {

// Reads media files for the import in the velacut-render process (--probe), one file after the other, in the order
// given. If a file crashes the probe it is reported as damaged and the others go on in a new process.
class MediaImporter : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged FINAL)

public:
    explicit MediaImporter(QObject *parent = nullptr);
    ~MediaImporter() override;

    // Default: velacut-render next to the running executable.
    void setExecutable(const QString &path) { m_executable = path; }

    // Adds files to the queue (processed in order).
    void import(const QStringList &paths);
    bool busy() const { return m_process != nullptr; }

    // For the user, translated.
    static QString errorMessage(ProbeError error, const QString &fileName);

signals:
    void imported(const velacut::Media &media);
    void failed(const QString &path, const QString &message);
    // The queue is empty.
    void finished();
    void busyChanged();

private:
    void startProcess();
    void onOutput();
    void onFinished(int exitCode, QProcess::ExitStatus status);

    QString m_executable;
    QProcess *m_process = nullptr;
    QStringList m_running; // given to the running process, not yet reported
    QStringList m_queue;   // waiting for the next process
    QString m_current;     // being probed now
    QByteArray m_buffer;
};

} // namespace velacut::engine
