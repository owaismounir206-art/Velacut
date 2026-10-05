// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/project/Clip.h"
#include "core/project/Media.h"

#include <QObject>
#include <QSet>
#include <QString>

#include <atomic>
#include <deque>
#include <functional>
#include <optional>

class QThread;

namespace vedit::engine {

// Smooth slow motion (MediaClipData::smooth, SPEC §5.12): the part of a file a slowed clip plays, at a higher frame
// rate with the new frames computed by motion-compensated interpolation (FFmpeg's minterpolate: the CPU path; RIFE on
// Vulkan is not installed here), sound copied as it is. The clip then plays this copy at its speed, so each shown frame
// is a different picture. Copies live in the cache, by file, range and rate.
struct SmoothCopy
{
    Media source;
    RationalTime from; // seconds of the file (whole seconds, rounded outward: small trims reuse the copy)
    RationalTime to;
    Rational frameRate; // of the copy

    QString path() const;
    bool ready() const;
    // The copy as a media item of its own (another id, the copy's file, duration and rate), for the projection.
    Media asMedia() const;
};

// The copy a clip needs, if any: smooth, slower than 1×, forwards at a steady speed, a video with a known rate.
std::optional<SmoothCopy> smoothCopyFor(const Clip &clip, const Media &media);

// Makes the copy now (the `ffmpeg` program); `progress` gets 0…1. An error for the user, empty when done.
QString makeSmoothCopy(const SmoothCopy &copy, const std::function<void(double)> &progress = {},
                       const std::atomic<bool> *cancel = nullptr);

// Makes the copies the preview needs, one at a time, in background.
class SmoothCopyQueue : public QObject
{
    Q_OBJECT

public:
    explicit SmoothCopyQueue(QObject *parent = nullptr);
    ~SmoothCopyQueue() override;

    void request(const SmoothCopy &copy);
    bool busy() const { return m_thread != nullptr; }
    double progress() const { return m_progress; }

signals:
    // `mediaId`: the original media item (its clips are projected again).
    void ready(const vedit::MediaId &mediaId);
    void failed(const vedit::MediaId &mediaId, const QString &error);
    void busyChanged();
    void progressChanged();

private:
    void startNext();

    std::deque<SmoothCopy> m_queue;
    QSet<QString> m_known; // paths queued, being made or failed
    QThread *m_thread = nullptr;
    std::atomic<bool> m_cancel{false};
    double m_progress = 0.0;
};

} // namespace vedit::engine
