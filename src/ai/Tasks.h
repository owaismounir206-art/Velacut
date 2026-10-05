// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ai/AiTask.h"
#include "ai/Analysis.h"
#include "engine/analysis/Cutout.h"
#include "engine/analysis/Decoding.h"
#include "fx/Reframe.h"
#include "fx/Stabilization.h"

#include <vector>

namespace vedit::ai {

// "Remove pauses": the quiet stretches of a media file's sound (its own time).
class PauseDetection : public AiTask
{
    Q_OBJECT

public:
    explicit PauseDetection(QString path, QObject *parent = nullptr);
    QString title() const override;
    const std::vector<SourceRange> &pauses() const { return m_pauses; }

protected:
    QString run() override;

private:
    QString m_path;
    std::vector<SourceRange> m_pauses;
};

// "Split scenes": where a video changes shot (seconds of the file).
class SceneDetection : public AiTask
{
    Q_OBJECT

public:
    explicit SceneDetection(QString path, QObject *parent = nullptr);
    QString title() const override;
    const std::vector<double> &cuts() const { return m_cuts; }

protected:
    QString run() override;

private:
    QString m_path;
    std::vector<double> m_cuts;
};

// "Stabilize": how the camera moved over a part of a video (the frames from `fromSeconds` to `toSeconds` of the file).
class CameraMotionAnalysis : public AiTask
{
    Q_OBJECT

public:
    CameraMotionAnalysis(QString path, double fromSeconds, double toSeconds, QObject *parent = nullptr);
    QString title() const override;
    const std::vector<fx::CameraStep> &steps() const { return m_steps; }
    Rational frameRate() const { return m_rate; }
    double fromSeconds() const { return m_from; }

protected:
    QString run() override;

private:
    QString m_path;
    double m_from = 0.0;
    double m_to = 0.0;
    std::vector<fx::CameraStep> m_steps;
    Rational m_rate{30};
};

// "Auto reframe": where the subject is over parts of videos, a steady path for each.
class SubjectTracking : public AiTask
{
    Q_OBJECT

public:
    struct Part
    {
        QString path;
        double fromSeconds = 0.0;
        double toSeconds = 0.0;
    };
    struct Path
    {
        std::vector<double> times; // seconds of the file
        std::vector<fx::SubjectPoint> points;
    };

    explicit SubjectTracking(std::vector<Part> parts, QObject *parent = nullptr);
    QString title() const override;
    // One per part, in order (empty when a part could not be read).
    const std::vector<Path> &paths() const { return m_paths; }

protected:
    QString run() override;

private:
    std::vector<Part> m_parts;
    std::vector<Path> m_paths;
};

// "Track": where a point of a video goes (engine::extractTrackedPath), in shares of the frame as displayed.
class MotionTracking : public AiTask
{
    Q_OBJECT

public:
    // `rotation`: of the video's display (degrees clockwise); `aspect`: of the stored frames.
    MotionTracking(QString path, double fromSeconds, double toSeconds, double x, double y, double radius, int rotation,
                   double storedAspect, QObject *parent = nullptr);
    QString title() const override;
    const std::vector<engine::TrackedPoint> &points() const { return m_points; }

protected:
    QString run() override;

private:
    QString m_path;
    double m_from;
    double m_to;
    double m_x;
    double m_y;
    double m_radius;
    int m_rotation;
    double m_aspect;
    std::vector<engine::TrackedPoint> m_points;
};

// "Remove background": the cut-out copy of the part of a video a clip plays (engine/analysis/Cutout.h, rembg).
class BackgroundRemoval : public AiTask
{
    Q_OBJECT

public:
    explicit BackgroundRemoval(engine::CutoutCopy copy, QObject *parent = nullptr);
    QString title() const override;

protected:
    QString run() override;

private:
    engine::CutoutCopy m_copy;
};

} // namespace vedit::ai
