// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/project/Media.h"

#include <QByteArray>
#include <QObject>
#include <QProcess>
#include <QSet>
#include <QStringList>

#include <deque>
#include <functional>

namespace velacut::engine {

// Copies of media files for the preview made in velacut-render, one at a time, in background (backwards copies,
// proxies). A copy is ready when its file exists; one that fails is not tried again in this session.
class MediaCopyQueue : public QObject
{
    Q_OBJECT

public:
    using Output = std::function<QString(const Media &)>;
    using Arguments = std::function<QStringList(const Media &, const QString &output)>;
    MediaCopyQueue(QString what, Output output, Arguments arguments, QObject *parent = nullptr);
    ~MediaCopyQueue() override;

    void setExecutable(const QString &path) { m_executable = path; }
    // Queues `media` unless its copy is ready or already queued.
    void request(const Media &media);
    bool isReady(const Media &media) const;
    bool busy() const { return m_process != nullptr; }
    // The media item being copied now, and 0…1 of it.
    MediaId current() const { return m_process ? m_current.id : MediaId(); }
    double progress() const { return m_progress; }
    bool queued(const MediaId &mediaId) const;

signals:
    void ready(const velacut::MediaId &mediaId);
    void failed(const velacut::MediaId &mediaId);
    void busyChanged();
    void progressChanged();

private:
    void startNext();
    void onOutput();
    void onFinished(int exitCode, QProcess::ExitStatus status);

    QString m_what; // for the log
    Output m_output;
    Arguments m_arguments;
    QString m_executable;
    std::deque<Media> m_queue;
    QSet<MediaId> m_known; // queued, running or failed
    QProcess *m_process = nullptr;
    Media m_current;
    QByteArray m_buffer;
    double m_progress = 0.0;
};

// velacut-render next to the running executable, else on the PATH.
QString defaultRenderExecutable();

} // namespace velacut::engine
