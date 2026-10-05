// SPDX-License-Identifier: GPL-3.0-or-later
#include "Tasks.h"

#include "engine/analysis/Decoding.h"

#include <QFileInfo>

#include <algorithm>

namespace vedit::ai {

namespace {

constexpr int kLevelsPerSecond = 100;

} // namespace

PauseDetection::PauseDetection(QString path, QObject *parent)
    : AiTask(parent)
    , m_path(std::move(path))
{
}

QString PauseDetection::title() const
{
    return tr("Finding the pauses");
}

QString PauseDetection::run()
{
    const auto levels = engine::extractLevels(m_path, kLevelsPerSecond, cancelFlag(), [this](double share) { report(share); });
    if (!levels) {
        return isCanceled() ? QString() : tr("The sound of %1 cannot be read.").arg(QFileInfo(m_path).fileName());
    }
    m_pauses = findPauses(*levels, kLevelsPerSecond);
    return {};
}

SceneDetection::SceneDetection(QString path, QObject *parent)
    : AiTask(parent)
    , m_path(std::move(path))
{
}

QString SceneDetection::title() const
{
    return tr("Finding the scenes");
}

QString SceneDetection::run()
{
    std::vector<double> times;
    const auto differences = engine::extractFrameDifferences(m_path, &times, cancelFlag(), [this](double share) { report(share); });
    if (!differences) {
        return isCanceled() ? QString() : tr("The pictures of %1 cannot be read.").arg(QFileInfo(m_path).fileName());
    }
    m_cuts = findSceneCuts(*differences, times);
    return {};
}

CameraMotionAnalysis::CameraMotionAnalysis(QString path, double fromSeconds, double toSeconds, QObject *parent)
    : AiTask(parent)
    , m_path(std::move(path))
    , m_from(fromSeconds)
    , m_to(toSeconds)
{
}

QString CameraMotionAnalysis::title() const
{
    return tr("Steadying the shot");
}

QString CameraMotionAnalysis::run()
{
    const auto steps = engine::extractCameraSteps(m_path, m_from, m_to, &m_rate, cancelFlag(), [this](double share) { report(share); });
    if (!steps) {
        return isCanceled() ? QString() : tr("The pictures of %1 cannot be read.").arg(QFileInfo(m_path).fileName());
    }
    m_steps = *steps;
    return {};
}

SubjectTracking::SubjectTracking(std::vector<Part> parts, QObject *parent)
    : AiTask(parent)
    , m_parts(std::move(parts))
{
}

QString SubjectTracking::title() const
{
    return tr("Following the subject");
}

QString SubjectTracking::run()
{
    constexpr int kPointsPerSecond = 5;
    double total = 0.0;
    for (const Part &part : m_parts) {
        total += std::max(0.0, part.toSeconds - part.fromSeconds);
    }
    double done = 0.0;
    for (const Part &part : m_parts) {
        const double length = std::max(0.0, part.toSeconds - part.fromSeconds);
        Path path;
        const auto points = engine::extractSubjectPath(part.path, part.fromSeconds, part.toSeconds, kPointsPerSecond, &path.times,
                                                       cancelFlag(), [&](double share) {
            report(total > 0 ? (done + share * length) / total : 0.0);
        });
        if (isCanceled()) {
            return {};
        }
        if (points) {
            // Smoothed over about a second and a half.
            path.points = fx::smoothSubjectPath(*points, 0.75 * kPointsPerSecond);
        } else {
            path.times.clear();
        }
        m_paths.push_back(std::move(path));
        done += length;
    }
    return {};
}

} // namespace vedit::ai
