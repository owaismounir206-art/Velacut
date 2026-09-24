// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QImage>
#include <QMutex>
#include <QObject>

#include <atomic>

namespace vedit::engine {

// Hands the most recent video frame from the MLT consumer thread to the preview item (docs/ARCHITECTURE.md
// §5.3): a single "latest frame" slot. Writers never wait for the UI; frames the UI had no time to show are
// counted as dropped.
class FrameSink : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QSize frameSize READ frameSize NOTIFY frameSizeChanged FINAL)

public:
    explicit FrameSink(QObject *parent = nullptr);

    // Any thread. The image must be RGBA8888 (straight alpha) and is shared, not copied.
    void push(const QImage &image, int position);
    // Any thread: the latest frame and a serial number that changes with every new frame.
    QImage latest(quint64 *serial = nullptr, int *position = nullptr) const;
    QSize frameSize() const;
    quint64 framesReceived() const { return m_received; }
    // Frames replaced before the UI took them.
    quint64 framesDropped() const { return m_dropped; }
    // Frames actually taken by a preview surface (i.e. drawn).
    quint64 framesDisplayed() const { return m_displayed; }
    // Called by the preview after taking a frame.
    void markConsumed(quint64 serial);

signals:
    // Emitted in the GUI thread (queued) when a new frame is available.
    void frameReady();
    void frameSizeChanged();

private:
    mutable QMutex m_mutex;
    QImage m_image;
    int m_position = -1;
    quint64 m_serial = 0;
    quint64 m_consumedSerial = 0;
    std::atomic<quint64> m_received{0};
    std::atomic<quint64> m_dropped{0};
    std::atomic<quint64> m_displayed{0};
    std::atomic<bool> m_notifyPending{false};
};

} // namespace vedit::engine
