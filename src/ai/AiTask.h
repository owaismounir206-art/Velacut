// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>
#include <QString>

#include <atomic>
#include <memory>

class QThread;

namespace velacut::ai {

// A local AI function (SPEC §2 principle 4, §5.12): it runs away from the interface thread, reports its progress, can be
// cancelled at any time, and gives a result that the editor turns into ordinary, editable model changes (cuts, clips,
// keyframes, captions) — never a "baked" video. Subclasses implement run() and keep their result for the caller.
class AiTask : public QObject
{
    Q_OBJECT
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged FINAL)
    Q_PROPERTY(bool running READ running NOTIFY runningChanged FINAL)

public:
    explicit AiTask(QObject *parent = nullptr);
    // Cancels the work and waits for it.
    ~AiTask() override;

    // What it does, for the progress shown to the user ("Finding the pauses").
    virtual QString title() const = 0;

    void start();
    void cancel();
    double progress() const { return m_progress; }
    bool running() const { return m_thread != nullptr; }

signals:
    void progressChanged();
    void runningChanged();
    // Exactly one of these after start(), on the task's thread.
    void finished();
    void failed(const QString &error);
    void canceled();

protected:
    // In a worker thread: the work, checking isCanceled() often and calling report(). Returns an error for the user,
    // empty on success (the result is kept by the subclass, read after finished()).
    virtual QString run() = 0;
    bool isCanceled() const { return m_cancel->load(); }
    // Thread-safe; the share done, 0…1.
    void report(double share);
    // For the decoding functions (engine/analysis/Decoding.h).
    const std::atomic<bool> *cancelFlag() const { return m_cancel.get(); }

private:
    void done(const QString &error);
    void stopThread();

    std::shared_ptr<std::atomic<bool>> m_cancel = std::make_shared<std::atomic<bool>>(false);
    QThread *m_thread = nullptr;
    double m_progress = 0.0;
};

} // namespace velacut::ai
