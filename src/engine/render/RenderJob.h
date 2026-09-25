// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/project/ProjectData.h"
#include "engine/render/ExportSettings.h"

#include <QElapsedTimer>
#include <QObject>
#include <QProcess>
#include <QStringList>

namespace vedit::engine {

// One export run by vedit-render in a separate process (a crash of a codec or driver cannot close the editor,
// and editing goes on while it exports). The project is frozen: a copy is written to the cache and the process
// renders that copy, so later edits do not affect the running export.
class RenderJob : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool running READ running NOTIFY runningChanged FINAL)
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged FINAL)
    Q_PROPERTY(int secondsLeft READ secondsLeft NOTIFY progressChanged FINAL)
    Q_PROPERTY(QString outputPath READ outputPath NOTIFY runningChanged FINAL)

public:
    explicit RenderJob(QObject *parent = nullptr);
    ~RenderJob() override;

    // Default: vedit-render next to the running executable.
    void setExecutable(const QString &path) { m_executable = path; }

    // Starts the export; on a problem found before starting (no disk space, nothing to export…) emits failed()
    // and returns false.
    bool start(const ProjectData &project, const SequenceId &sequenceId, const ExportSettings &settings);
    Q_INVOKABLE void cancel();

    bool running() const { return m_process != nullptr; }
    double progress() const { return m_total > 0 ? static_cast<double>(m_frame) / m_total : 0.0; }
    // Estimated from the speed so far; -1 while unknown.
    int secondsLeft() const;
    QString outputPath() const { return m_settings.outputPath; }

    static QString errorMessage(RenderError error);

signals:
    void runningChanged();
    void progressChanged();
    void finished(const QString &outputPath, const QStringList &warnings);
    // `message` for the user (translated), `detail` technical (for "Copia dettagli" and the log).
    void failed(const QString &message, const QString &detail);
    void cancelled();

private:
    void onOutput();
    void onFinished(int exitCode, QProcess::ExitStatus status);
    void handleEvent(const QJsonObject &event);
    void finish();
    void removePartialFiles() const;

    QString m_executable;
    QProcess *m_process = nullptr;
    ExportSettings m_settings;
    QString m_frozenProject;
    QString m_jobFile;
    QByteArray m_buffer;
    QStringList m_warnings;
    QString m_errorCode;
    QString m_errorDetail;
    bool m_done = false;
    bool m_cancelled = false;
    int m_frame = 0;
    int m_total = 0;
    QElapsedTimer m_elapsed;
};

} // namespace vedit::engine
