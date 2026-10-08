// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/project/Media.h"

#include <QByteArray>
#include <QObject>
#include <QProcess>
#include <QSet>
#include <QStringList>

#include <deque>

namespace velacut::engine {

// Where the backwards copy of a media item lives (cache, by fingerprint: shared by every project).
QString reverseProxyPath(const Media &media);
bool reverseProxyReady(const Media &media);

// Prepares the backwards copies for the preview of reversed clips, one at a time, in velacut-render (--backwards).
// Until one is ready, the clip still plays backwards, directly from the original (correct but slow).
class ReverseProxyQueue : public QObject
{
    Q_OBJECT

public:
    explicit ReverseProxyQueue(QObject *parent = nullptr);
    ~ReverseProxyQueue() override;

    void setExecutable(const QString &path) { m_executable = path; }
    // Queues `media` unless its copy is ready or already queued.
    void request(const Media &media);
    bool busy() const { return m_process != nullptr; }
    // 0…1 of the copy being prepared.
    double progress() const { return m_progress; }

signals:
    void ready(const velacut::MediaId &mediaId);
    void failed(const velacut::MediaId &mediaId);
    void busyChanged();
    void progressChanged();

private:
    void startNext();
    void onOutput();
    void onFinished(int exitCode, QProcess::ExitStatus status);

    QString m_executable;
    std::deque<Media> m_queue;
    QSet<MediaId> m_known; // queued, running or failed
    QProcess *m_process = nullptr;
    Media m_current;
    QByteArray m_buffer;
    double m_progress = 0.0;
};

} // namespace velacut::engine
