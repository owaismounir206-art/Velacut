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

namespace {

// Display (what is seen, rotated by the file's metadata) ↔ stored frame, in shares of each.
std::pair<double, double> toStored(double x, double y, int rotation)
{
    switch (rotation) {
    case 90:
        return {y, 1.0 - x};
    case 180:
        return {1.0 - x, 1.0 - y};
    case 270:
        return {1.0 - y, x};
    default:
        return {x, y};
    }
}

std::pair<double, double> toDisplay(double x, double y, int rotation)
{
    switch (rotation) {
    case 90:
        return {1.0 - y, x};
    case 180:
        return {1.0 - x, 1.0 - y};
    case 270:
        return {y, 1.0 - x};
    default:
        return {x, y};
    }
}

} // namespace

MotionTracking::MotionTracking(QString path, double fromSeconds, double toSeconds, double x, double y, double radius, int rotation,
                               double storedAspect, QObject *parent)
    : AiTask(parent)
    , m_path(std::move(path))
    , m_from(fromSeconds)
    , m_to(toSeconds)
    , m_x(x)
    , m_y(y)
    , m_radius(radius)
    , m_rotation(rotation)
    , m_aspect(storedAspect)
{
}

QString MotionTracking::title() const
{
    return tr("Tracking the movement");
}

QString MotionTracking::run()
{
    const auto [sx, sy] = toStored(m_x, m_y, m_rotation);
    const auto points = engine::extractTrackedPath(m_path, m_from, m_to, sx, sy, m_radius, m_aspect, cancelFlag(),
                                                   [this](double share) { report(share); });
    if (!points) {
        return isCanceled() ? QString() : tr("The pictures of %1 cannot be read.").arg(QFileInfo(m_path).fileName());
    }
    m_points = *points;
    for (engine::TrackedPoint &point : m_points) {
        const auto [dx, dy] = toDisplay(point.x, point.y, m_rotation);
        point.x = dx;
        point.y = dy;
    }
    return {};
}

BackgroundRemoval::BackgroundRemoval(engine::CutoutCopy copy, QObject *parent)
    : AiTask(parent)
    , m_copy(std::move(copy))
{
}

QString BackgroundRemoval::title() const
{
    return tr("Removing the background");
}

QString BackgroundRemoval::run()
{
    if (m_copy.ready()) {
        return {};
    }
    return engine::makeCutoutCopy(m_copy, [this](double share) { report(share); }, cancelFlag());
}

} // namespace vedit::ai
