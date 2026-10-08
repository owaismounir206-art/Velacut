// SPDX-License-Identifier: GPL-3.0-or-later
#include "AiTask.h"

#include <QPointer>
#include <QThread>

#include <algorithm>

namespace velacut::ai {

AiTask::AiTask(QObject *parent)
    : QObject(parent)
{
}

AiTask::~AiTask()
{
    m_cancel->store(true);
    stopThread();
}

void AiTask::start()
{
    if (m_thread) {
        return;
    }
    m_cancel->store(false);
    m_progress = 0.0;
    emit progressChanged();
    m_thread = QThread::create([this] {
        const QString error = run();
        QMetaObject::invokeMethod(this, [this, error] { done(error); }, Qt::QueuedConnection);
    });
    m_thread->start(QThread::LowPriority);
    emit runningChanged();
}

void AiTask::cancel()
{
    m_cancel->store(true);
}

void AiTask::report(double share)
{
    const double value = std::clamp(share, 0.0, 1.0);
    QMetaObject::invokeMethod(this, [this, value] {
        if (m_thread && value > m_progress + 0.005) {
            m_progress = value;
            emit progressChanged();
        }
    }, Qt::QueuedConnection);
}

void AiTask::done(const QString &error)
{
    stopThread();
    emit runningChanged();
    if (m_cancel->load()) {
        emit canceled();
    } else if (!error.isEmpty()) {
        emit failed(error);
    } else {
        m_progress = 1.0;
        emit progressChanged();
        emit finished();
    }
}

void AiTask::stopThread()
{
    if (m_thread) {
        m_thread->wait();
        delete m_thread;
        m_thread = nullptr;
    }
}

} // namespace velacut::ai
