// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDateTime>
#include <QElapsedTimer>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

#include <memory>

namespace velacut::ui {

class EditorController;

class RecordController : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(RecordController)
    QML_UNCREATABLE("Provided by Editor.recorder")

    Q_PROPERTY(int mode READ mode WRITE setMode NOTIFY modeChanged FINAL)
    Q_PROPERTY(int status READ status NOTIFY statusChanged FINAL)
    Q_PROPERTY(bool recording READ isRecording NOTIFY recordingChanged FINAL)
    Q_PROPERTY(bool paused READ isPaused NOTIFY pausedChanged FINAL)
    Q_PROPERTY(int countdown READ countdown NOTIFY countdownChanged FINAL)
    Q_PROPERTY(double durationSeconds READ durationSeconds NOTIFY durationChanged FINAL)
    Q_PROPERTY(float audioLevel READ audioLevel NOTIFY audioLevelChanged FINAL)
    Q_PROPERTY(bool active READ isActive NOTIFY activeChanged FINAL)

    // Teleprompter properties
    Q_PROPERTY(bool teleprompterVisible READ teleprompterVisible WRITE setTeleprompterVisible NOTIFY teleprompterVisibleChanged FINAL)
    Q_PROPERTY(QString teleprompterText READ teleprompterText WRITE setTeleprompterText NOTIFY teleprompterTextChanged FINAL)
    Q_PROPERTY(double teleprompterSpeed READ teleprompterSpeed WRITE setTeleprompterSpeed NOTIFY teleprompterSpeedChanged FINAL)
    Q_PROPERTY(bool teleprompterMirrored READ teleprompterMirrored WRITE setTeleprompterMirrored NOTIFY teleprompterMirroredChanged FINAL)
    Q_PROPERTY(bool teleprompterPlaying READ teleprompterPlaying WRITE setTeleprompterPlaying NOTIFY teleprompterPlayingChanged FINAL)
    Q_PROPERTY(double teleprompterScroll READ teleprompterScroll WRITE setTeleprompterScroll NOTIFY teleprompterScrollChanged FINAL)

    // Video capture options
    Q_PROPERTY(bool webcamInCorner READ webcamInCorner WRITE setWebcamInCorner NOTIFY webcamInCornerChanged FINAL)

public:
    enum Mode
    {
        VoiceOver = 0,
        Screen = 1,
        Webcam = 2,
        ScreenAndWebcam = 3,
    };
    Q_ENUM(Mode)

    enum Status
    {
        Idle = 0,
        CountingDown = 1,
        Recording = 2,
        Paused = 3,
        Saving = 4,
    };
    Q_ENUM(Status)

    explicit RecordController(EditorController &editor);
    ~RecordController() override;

    int mode() const { return static_cast<int>(m_mode); }
    void setMode(int mode);

    int status() const { return static_cast<int>(m_status); }
    bool isRecording() const { return m_status == Status::Recording || m_status == Status::Paused; }
    bool isPaused() const { return m_status == Status::Paused; }
    int countdown() const { return m_countdown; }
    double durationSeconds() const { return m_durationSeconds; }
    float audioLevel() const { return m_audioLevel; }
    bool isActive() const { return m_active; }

    bool teleprompterVisible() const { return m_teleprompterVisible; }
    void setTeleprompterVisible(bool visible);

    QString teleprompterText() const { return m_teleprompterText; }
    void setTeleprompterText(const QString &text);

    double teleprompterSpeed() const { return m_teleprompterSpeed; }
    void setTeleprompterSpeed(double speed);

    bool teleprompterMirrored() const { return m_teleprompterMirrored; }
    void setTeleprompterMirrored(bool mirrored);

    bool teleprompterPlaying() const { return m_teleprompterPlaying; }
    void setTeleprompterPlaying(bool playing);

    double teleprompterScroll() const { return m_teleprompterScroll; }
    void setTeleprompterScroll(double scroll);

    bool webcamInCorner() const { return m_webcamInCorner; }
    void setWebcamInCorner(bool inCorner);

    void setTestMode(bool testMode) { m_testMode = testMode; }
    bool testMode() const { return m_testMode; }

    QString lastSavedPath() const { return m_lastSavedPath; }

    Q_INVOKABLE void open(int mode = 0);
    Q_INVOKABLE void close();
    Q_INVOKABLE void startCountdown();
    Q_INVOKABLE void startRecording();
    Q_INVOKABLE void pauseRecording();
    Q_INVOKABLE void resumeRecording();
    Q_INVOKABLE void stopRecording();
    Q_INVOKABLE void cancelRecording();

signals:
    void modeChanged();
    void statusChanged();
    void recordingChanged();
    void pausedChanged();
    void countdownChanged();
    void durationChanged();
    void audioLevelChanged();
    void activeChanged();
    void teleprompterVisibleChanged();
    void teleprompterTextChanged();
    void teleprompterSpeedChanged();
    void teleprompterMirroredChanged();
    void teleprompterPlayingChanged();
    void teleprompterScrollChanged();
    void webcamInCornerChanged();
    void recordingFinished(const QString &savedPath);

private:
    void tickDuration();
    void tickCountdown();
    void tickTeleprompter();
    void generateFallbackFile(const QString &path, Mode mode, double durationSeconds);

    EditorController &m_editor;
    Mode m_mode = Mode::VoiceOver;
    Status m_status = Status::Idle;
    bool m_active = false;
    int m_countdown = 3;
    double m_durationSeconds = 0.0;
    float m_audioLevel = 0.0f;

    bool m_teleprompterVisible = false;
    QString m_teleprompterText;
    double m_teleprompterSpeed = 60.0;
    bool m_teleprompterMirrored = false;
    bool m_teleprompterPlaying = true;
    double m_teleprompterScroll = 0.0;

    bool m_webcamInCorner = true;
    bool m_testMode = false;

    QString m_outputPath;
    QString m_lastSavedPath;
    std::unique_ptr<QProcess> m_process;
    QTimer m_durationTimer;
    QTimer m_countdownTimer;
    QTimer m_teleprompterTimer;
    QElapsedTimer m_elapsedTimer;
    qint64 m_accumulatedMs = 0;
};

} // namespace velacut::ui
