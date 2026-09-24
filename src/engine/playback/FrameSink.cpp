// SPDX-License-Identifier: GPL-3.0-or-later
#include "FrameSink.h"

#include <QMetaObject>

namespace vedit::engine {

FrameSink::FrameSink(QObject *parent)
    : QObject(parent)
{
}

void FrameSink::push(const QImage &image, int position)
{
    bool sizeChanged = false;
    {
        QMutexLocker lock(&m_mutex);
        if (m_serial != m_consumedSerial) {
            ++m_dropped;
        }
        sizeChanged = image.size() != m_image.size();
        m_image = image;
        m_position = position;
        ++m_serial;
    }
    ++m_received;
    if (sizeChanged) {
        QMetaObject::invokeMethod(this, &FrameSink::frameSizeChanged, Qt::QueuedConnection);
    }
    // Coalesce notifications: at most one pending frameReady() in the GUI event queue.
    if (!m_notifyPending.exchange(true)) {
        QMetaObject::invokeMethod(
            this,
            [this] {
                m_notifyPending = false;
                emit frameReady();
            },
            Qt::QueuedConnection);
    }
}

QImage FrameSink::latest(quint64 *serial, int *position) const
{
    QMutexLocker lock(&m_mutex);
    if (serial) {
        *serial = m_serial;
    }
    if (position) {
        *position = m_position;
    }
    return m_image;
}

QSize FrameSink::frameSize() const
{
    QMutexLocker lock(&m_mutex);
    return m_image.size();
}

void FrameSink::markConsumed(quint64 serial)
{
    QMutexLocker lock(&m_mutex);
    if (serial != m_consumedSerial) {
        ++m_displayed;
    }
    m_consumedSerial = serial;
}

} // namespace vedit::engine
