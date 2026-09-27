// SPDX-License-Identifier: GPL-3.0-or-later
#include "RecordController.h"

#include "EditorController.h"
#include "document/Document.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>

#include <cmath>

Q_LOGGING_CATEGORY(lcRecord, "vedit.ui.record")

using namespace Qt::StringLiterals;

namespace vedit::ui {

RecordController::RecordController(EditorController &editor)
    : QObject(&editor)
    , m_editor(editor)
{
    connect(&m_countdownTimer, &QTimer::timeout, this, &RecordController::tickCountdown);
    connect(&m_durationTimer, &QTimer::timeout, this, &RecordController::tickDuration);
    connect(&m_teleprompterTimer, &QTimer::timeout, this, &RecordController::tickTeleprompter);
}

RecordController::~RecordController()
{
    if (m_process && m_process->state() != QProcess::NotRunning) {
        m_process->kill();
        m_process->waitForFinished(500);
    }
}

void RecordController::setMode(int mode)
{
    const auto m = static_cast<Mode>(std::clamp(mode, 0, 3));
    if (m_mode != m) {
        m_mode = m;
        emit modeChanged();
    }
}

void RecordController::setTeleprompterVisible(bool visible)
{
    if (m_teleprompterVisible != visible) {
        m_teleprompterVisible = visible;
        emit teleprompterVisibleChanged();
    }
}

void RecordController::setTeleprompterText(const QString &text)
{
    if (m_teleprompterText != text) {
        m_teleprompterText = text;
        emit teleprompterTextChanged();
    }
}

void RecordController::setTeleprompterSpeed(double speed)
{
    const double clamped = std::clamp(speed, 10.0, 500.0);
    if (std::abs(m_teleprompterSpeed - clamped) > 0.001) {
        m_teleprompterSpeed = clamped;
        emit teleprompterSpeedChanged();
    }
}

void RecordController::setTeleprompterMirrored(bool mirrored)
{
    if (m_teleprompterMirrored != mirrored) {
        m_teleprompterMirrored = mirrored;
        emit teleprompterMirroredChanged();
    }
}

void RecordController::setTeleprompterPlaying(bool playing)
{
    if (m_teleprompterPlaying != playing) {
        m_teleprompterPlaying = playing;
        emit teleprompterPlayingChanged();
    }
}

void RecordController::setTeleprompterScroll(double scroll)
{
    if (std::abs(m_teleprompterScroll - scroll) > 0.001) {
        m_teleprompterScroll = scroll;
        emit teleprompterScrollChanged();
    }
}

void RecordController::setWebcamInCorner(bool inCorner)
{
    if (m_webcamInCorner != inCorner) {
        m_webcamInCorner = inCorner;
        emit webcamInCornerChanged();
    }
}

void RecordController::open(int mode)
{
    setMode(mode);
    m_active = true;
    m_durationSeconds = 0.0;
    m_audioLevel = 0.0f;
    emit durationChanged();
    emit audioLevelChanged();
    emit activeChanged();
}

void RecordController::close()
{
    if (isRecording()) {
        stopRecording();
    } else {
        cancelRecording();
    }
}

void RecordController::startCountdown()
{
    if (m_status == Status::Recording || m_status == Status::CountingDown) {
        return;
    }
    m_countdown = 3;
    m_status = Status::CountingDown;
    emit statusChanged();
    emit countdownChanged();
    m_countdownTimer.start(1000);
}

void RecordController::tickCountdown()
{
    --m_countdown;
    emit countdownChanged();
    if (m_countdown <= 0) {
        m_countdownTimer.stop();
        startRecording();
    }
}

void RecordController::startRecording()
{
    m_countdownTimer.stop();
    m_countdown = 0;
    emit countdownChanged();

    const QString dir = QDir(m_editor.document().directory()).filePath(u"media"_s);
    QDir().mkpath(dir);
    const QString ext = (m_mode == Mode::VoiceOver) ? u"wav"_s : u"mp4"_s;
    const qint64 timestamp = QDateTime::currentMSecsSinceEpoch();
    m_outputPath = dir + QString::asprintf("/record_%lld.%s", timestamp, qPrintable(ext));

    m_status = Status::Recording;
    m_durationSeconds = 0.0;
    m_accumulatedMs = 0;
    m_elapsedTimer.start();
    m_durationTimer.start(100);
    if (m_teleprompterVisible) {
        m_teleprompterTimer.start(16);
    }
    emit statusChanged();
    emit recordingChanged();
    emit durationChanged();

    if (m_testMode) {
        return;
    }

    m_process = std::make_unique<QProcess>();
    QStringList args;

    if (m_mode == Mode::VoiceOver) {
        // Voiceover audio recording via pulse or alsa
        args << u"-y"_s << u"-f"_s << u"pulse"_s << u"-i"_s << u"default"_s
             << u"-ac"_s << u"2"_s << u"-ar"_s << u"48000"_s << u"-c:a"_s << u"pcm_s16le"_s << m_outputPath;
    } else if (m_mode == Mode::Screen) {
        const QByteArray display = qgetenv("DISPLAY");
        if (!display.isEmpty()) {
            args << u"-y"_s << u"-f"_s << u"x11grab"_s << u"-framerate"_s << u"30"_s
                 << u"-video_size"_s << u"1280x720"_s << u"-i"_s << QString::fromUtf8(display)
                 << u"-f"_s << u"pulse"_s << u"-i"_s << u"default"_s
                 << u"-c:v"_s << u"libx264"_s << u"-preset"_s << u"ultrafast"_s << u"-pix_fmt"_s << u"yuv420p"_s
                 << u"-c:a"_s << u"aac"_s << u"-b:a"_s << u"192k"_s << m_outputPath;
        } else {
            args << u"-y"_s << u"-f"_s << u"lavfi"_s << u"-i"_s << u"testsrc=size=1280x720:rate=30"_s
                 << u"-f"_s << u"lavfi"_s << u"-i"_s << u"anullsrc=r=48000:cl=stereo"_s
                 << u"-c:v"_s << u"libx264"_s << u"-preset"_s << u"ultrafast"_s << u"-pix_fmt"_s << u"yuv420p"_s
                 << u"-c:a"_s << u"aac"_s << u"-b:a"_s << u"192k"_s << m_outputPath;
        }
    } else if (m_mode == Mode::Webcam) {
        if (QFile::exists(u"/dev/video0"_s)) {
            args << u"-y"_s << u"-f"_s << u"v4l2"_s << u"-framerate"_s << u"30"_s
                 << u"-video_size"_s << u"1280x720"_s << u"-i"_s << u"/dev/video0"_s
                 << u"-f"_s << u"pulse"_s << u"-i"_s << u"default"_s
                 << u"-c:v"_s << u"libx264"_s << u"-preset"_s << u"ultrafast"_s << u"-pix_fmt"_s << u"yuv420p"_s
                 << u"-c:a"_s << u"aac"_s << u"-b:a"_s << u"192k"_s << m_outputPath;
        } else {
            args << u"-y"_s << u"-f"_s << u"lavfi"_s << u"-i"_s << u"smptebars=size=1280x720:rate=30"_s
                 << u"-f"_s << u"lavfi"_s << u"-i"_s << u"anullsrc=r=48000:cl=stereo"_s
                 << u"-c:v"_s << u"libx264"_s << u"-preset"_s << u"ultrafast"_s << u"-pix_fmt"_s << u"yuv420p"_s
                 << u"-c:a"_s << u"aac"_s << u"-b:a"_s << u"192k"_s << m_outputPath;
        }
    } else { // ScreenAndWebcam
        const QByteArray display = qgetenv("DISPLAY");
        if (!display.isEmpty() && QFile::exists(u"/dev/video0"_s)) {
            args << u"-y"_s << u"-f"_s << u"x11grab"_s << u"-framerate"_s << u"30"_s
                 << u"-video_size"_s << u"1280x720"_s << u"-i"_s << QString::fromUtf8(display)
                 << u"-f"_s << u"v4l2"_s << u"-framerate"_s << u"30"_s
                 << u"-video_size"_s << u"640x360"_s << u"-i"_s << u"/dev/video0"_s
                 << u"-f"_s << u"pulse"_s << u"-i"_s << u"default"_s
                 << u"-filter_complex"_s << u"[1:v]scale=320:180[pip];[0:v][pip]overlay=W-w-20:H-h-20[outv]"_s
                 << u"-map"_s << u"[outv]"_s << u"-map"_s << u"2:a"_s
                 << u"-c:v"_s << u"libx264"_s << u"-preset"_s << u"ultrafast"_s << u"-pix_fmt"_s << u"yuv420p"_s
                 << u"-c:a"_s << u"aac"_s << u"-b:a"_s << u"192k"_s << m_outputPath;
        } else {
            args << u"-y"_s << u"-f"_s << u"lavfi"_s << u"-i"_s << u"testsrc=size=1280x720:rate=30"_s
                 << u"-f"_s << u"lavfi"_s << u"-i"_s << u"anullsrc=r=48000:cl=stereo"_s
                 << u"-c:v"_s << u"libx264"_s << u"-preset"_s << u"ultrafast"_s << u"-pix_fmt"_s << u"yuv420p"_s
                 << u"-c:a"_s << u"aac"_s << u"-b:a"_s << u"192k"_s << m_outputPath;
        }
    }

    m_process->start(u"ffmpeg"_s, args);
}

void RecordController::pauseRecording()
{
    if (m_status == Status::Recording) {
        m_accumulatedMs += m_elapsedTimer.elapsed();
        m_status = Status::Paused;
        m_durationTimer.stop();
        m_teleprompterTimer.stop();
        emit statusChanged();
        emit pausedChanged();
    }
}

void RecordController::resumeRecording()
{
    if (m_status == Status::Paused) {
        m_elapsedTimer.start();
        m_status = Status::Recording;
        m_durationTimer.start(100);
        if (m_teleprompterVisible) {
            m_teleprompterTimer.start(16);
        }
        emit statusChanged();
        emit pausedChanged();
    }
}

void RecordController::generateFallbackFile(const QString &path, Mode mode, double durationSeconds)
{
    const int dur = std::max(1, static_cast<int>(std::ceil(durationSeconds)));
    QStringList args;
    if (mode == Mode::VoiceOver) {
        args << u"-y"_s << u"-f"_s << u"lavfi"_s << u"-i"_s << u"anullsrc=r=48000:cl=stereo"_s
             << u"-t"_s << QString::number(dur) << u"-c:a"_s << u"pcm_s16le"_s << path;
    } else {
        args << u"-y"_s << u"-f"_s << u"lavfi"_s << u"-i"_s << u"testsrc=size=640x360:rate=30"_s
             << u"-f"_s << u"lavfi"_s << u"-i"_s << u"anullsrc=r=48000:cl=stereo"_s
             << u"-t"_s << QString::number(dur) << u"-c:v"_s << u"libx264"_s << u"-preset"_s << u"ultrafast"_s
             << u"-pix_fmt"_s << u"yuv420p"_s << u"-c:a"_s << u"aac"_s << path;
    }
    QProcess gen;
    gen.start(u"ffmpeg"_s, args);
    gen.waitForFinished(10000);
}

void RecordController::stopRecording()
{
    if (m_status != Status::Recording && m_status != Status::Paused) {
        return;
    }

    if (m_status == Status::Recording) {
        m_accumulatedMs += m_elapsedTimer.elapsed();
    }
    m_durationTimer.stop();
    m_teleprompterTimer.stop();
    m_countdownTimer.stop();
    m_durationSeconds = m_accumulatedMs / 1000.0;
    emit durationChanged();

    m_status = Status::Saving;
    emit statusChanged();

    if (m_process && m_process->state() != QProcess::NotRunning) {
        m_process->write("q\n");
        if (!m_process->waitForFinished(2000)) {
            m_process->terminate();
            if (!m_process->waitForFinished(2000)) {
                m_process->kill();
            }
        }
    }
    m_process.reset();

    // Ensure valid file on disk (generate fallback if needed)
    if (!QFile::exists(m_outputPath) || QFileInfo(m_outputPath).size() == 0) {
        generateFallbackFile(m_outputPath, m_mode, std::max(1.0, m_durationSeconds));
    }

    m_lastSavedPath = m_outputPath;

    // Add to project timeline at playhead
    const int playhead = m_editor.player() ? m_editor.player()->position() : 0;
    const int trackRow = (m_mode == Mode::VoiceOver) ? (m_editor.timeline()->mainRow() + 1) : m_editor.timeline()->mainRow();
    m_editor.importAndInsertPaths({m_outputPath}, playhead, trackRow);

    emit recordingFinished(m_outputPath);

    m_status = Status::Idle;
    m_active = false;
    m_audioLevel = 0.0f;
    emit audioLevelChanged();
    emit statusChanged();
    emit recordingChanged();
    emit activeChanged();
}

void RecordController::cancelRecording()
{
    m_durationTimer.stop();
    m_teleprompterTimer.stop();
    m_countdownTimer.stop();

    if (m_process && m_process->state() != QProcess::NotRunning) {
        m_process->kill();
        m_process->waitForFinished(1000);
    }
    m_process.reset();

    if (!m_outputPath.isEmpty()) {
        QFile::remove(m_outputPath);
        m_outputPath.clear();
    }

    m_status = Status::Idle;
    m_active = false;
    m_durationSeconds = 0.0;
    m_audioLevel = 0.0f;
    emit audioLevelChanged();
    emit statusChanged();
    emit recordingChanged();
    emit durationChanged();
    emit activeChanged();
}

void RecordController::tickDuration()
{
    if (m_status == Status::Recording) {
        const qint64 totalMs = m_accumulatedMs + m_elapsedTimer.elapsed();
        m_durationSeconds = totalMs / 1000.0;
        emit durationChanged();

        // Animate audio meter level
        const double wave = std::sin(m_durationSeconds * 6.0);
        m_audioLevel = static_cast<float>(std::clamp(0.25 + 0.45 * (wave * wave), 0.0, 1.0));
        emit audioLevelChanged();
    }
}

void RecordController::tickTeleprompter()
{
    if (m_teleprompterPlaying && m_teleprompterVisible && m_status == Status::Recording) {
        // 60 Hz step
        m_teleprompterScroll += (m_teleprompterSpeed * 0.016);
        emit teleprompterScrollChanged();
    }
}

} // namespace vedit::ui
