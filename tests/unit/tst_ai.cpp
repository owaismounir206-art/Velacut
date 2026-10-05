// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/AiTask.h"
#include "ai/Analysis.h"

#include <QSignalSpy>
#include <QTest>
#include <QThread>

namespace {

// Counts to `steps`, reporting its progress; stops when cancelled.
class CountingTask : public vedit::ai::AiTask
{
public:
    explicit CountingTask(int steps)
        : m_steps(steps)
    {
    }
    QString title() const override { return QStringLiteral("Counting"); }
    int counted = 0;

protected:
    QString run() override
    {
        for (int i = 0; i < m_steps && !isCanceled(); ++i) {
            QThread::msleep(2);
            ++counted;
            report(double(i + 1) / m_steps);
        }
        return counted == 3 ? QStringLiteral("three is not allowed") : QString();
    }

private:
    int m_steps;
};

} // namespace

using namespace vedit;
using namespace vedit::ai;

class TestAi : public QObject
{
    Q_OBJECT

private slots:
    void pausesFollowTheRecording()
    {
        // 100 windows per second: 1 s of speech (−20 dB), 1 s of room noise (−55 dB), 1 s of speech, 0.3 s of noise,
        // 0.5 s of speech, then 1 s of noise at the end.
        std::vector<float> levels;
        const auto add = [&levels](int windows, float level) { levels.insert(levels.end(), static_cast<size_t>(windows), level); };
        add(100, -20.0f);
        add(100, -55.0f);
        add(100, -20.0f);
        add(30, -55.0f);
        add(50, -20.0f);
        add(100, -55.0f);
        const std::vector<SourceRange> pauses = findPauses(levels, 100);
        QCOMPARE(pauses.size(), 2u);
        // The pause in the middle keeps 0.15 s on each side; the short one (0.3 s) stays.
        QCOMPARE(pauses[0].start, RationalTime(115, Rational(100)));
        QCOMPARE(pauses[0].end, RationalTime(185, Rational(100)));
        // At the end: all of it goes after the breathing room.
        QCOMPARE(pauses[1].start, RationalTime(395, Rational(100)));
        QCOMPARE(pauses[1].end, RationalTime(480, Rational(100)));
        // A louder room: same pauses (the threshold follows the noise floor).
        for (float &level : levels) {
            level = level < -50.0f ? -38.0f : -12.0f;
        }
        QCOMPARE(findPauses(levels, 100).size(), 2u);
        // All at one level: nothing to tell apart.
        QVERIFY(findPauses(std::vector<float>(500, -30.0f), 100).empty());
    }

    void tasksReportFinishFailAndCancel()
    {
        CountingTask quick(50);
        QSignalSpy finished(&quick, &ai::AiTask::finished);
        quick.start();
        QVERIFY(quick.running());
        QVERIFY(finished.wait(5000));
        QVERIFY(!quick.running());
        QCOMPARE(quick.counted, 50);
        QCOMPARE(quick.progress(), 1.0);

        CountingTask failing(3);
        QSignalSpy failed(&failing, &ai::AiTask::failed);
        failing.start();
        QVERIFY(failed.wait(5000));
        QCOMPARE(failed.first().first().toString(), QStringLiteral("three is not allowed"));

        CountingTask endless(1000000);
        QSignalSpy canceled(&endless, &ai::AiTask::canceled);
        QSignalSpy endlessFinished(&endless, &ai::AiTask::finished);
        endless.start();
        QTRY_VERIFY(endless.progress() > 0.0 || endless.counted > 0);
        endless.cancel();
        QVERIFY(canceled.wait(5000));
        QCOMPARE(endlessFinished.count(), 0);

        // Destroyed while running: it stops and waits.
        auto running = std::make_unique<CountingTask>(1000000);
        running->start();
        QThread::msleep(20);
        running.reset();
    }

    void scenesAreCutsThatStandOut()
    {
        std::vector<float> differences;
        std::vector<double> times;
        for (int i = 0; i < 150; ++i) {
            times.push_back(i / 30.0);
            // A slow pan (0.03 per frame), a cut at frame 45, a whip of 3 frames at 90 (not a cut: it is not alone),
            // a cut at 120.
            float value = 0.03f;
            if (i == 45 || i == 120) {
                value = 0.6f;
            } else if (i >= 90 && i < 93) {
                value = 0.4f;
            }
            differences.push_back(i == 0 ? 0.0f : value);
        }
        const std::vector<double> cuts = findSceneCuts(differences, times);
        QCOMPARE(cuts.size(), 2u);
        QCOMPARE(cuts[0], 45 / 30.0);
        QCOMPARE(cuts[1], 120 / 30.0);
        // Two cuts closer than the minimum: only the first.
        differences[50] = 0.7f;
        QCOMPARE(findSceneCuts(differences, times).size(), 2u);
    }
};

QTEST_GUILESS_MAIN(TestAi)
#include "tst_ai.moc"
