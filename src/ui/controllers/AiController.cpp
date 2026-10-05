// SPDX-License-Identifier: GPL-3.0-or-later
#include "AiController.h"

#include "ai/Tasks.h"
#include "core/commands/Edit.h"
#include "core/edit/TimelineEditor.h"
#include "fx/Stabilization.h"
#include "ui/controllers/EditorController.h"

#include <algorithm>
#include <cmath>
#include <map>

using namespace Qt::StringLiterals;

namespace vedit::ui {

AiController::AiController(EditorController &editor)
    : QObject(&editor)
    , m_editor(editor)
{
}

AiController::~AiController() = default;

QString AiController::title() const
{
    return m_task ? m_task->title() : QString();
}

double AiController::progress() const
{
    return m_task ? m_task->progress() : 0.0;
}

std::optional<AiController::Target> AiController::target(bool needsVideo, bool needsAudio) const
{
    const std::optional<ClipId> clipId = m_editor.clipForLibrary();
    if (!clipId) {
        return std::nullopt;
    }
    const Clip *clip = m_editor.data().findClip(*clipId);
    const MediaClipData *media = clip ? clip->media() : nullptr;
    const Media *item = media ? m_editor.data().findMedia(media->mediaId) : nullptr;
    if (!item || item->kind == MediaKind::Image) {
        return std::nullopt;
    }
    if ((needsVideo && (!item->info.video || media->streams == Streams::AudioOnly)) ||
        (needsAudio && (!item->info.audio || media->streams == Streams::VideoOnly))) {
        return std::nullopt;
    }
    return Target{*clipId, item->path, item->fingerprint.value};
}

bool AiController::canRemovePauses() const
{
    return !busy() && target(false, true).has_value();
}

bool AiController::canSplitScenes() const
{
    return !busy() && target(true, false).has_value();
}

void AiController::run(std::unique_ptr<ai::AiTask> task)
{
    m_task = std::move(task);
    connect(m_task.get(), &ai::AiTask::progressChanged, this, &AiController::progressChanged);
    const auto finish = [this] {
        // Deleted later: this runs inside one of the task's signals.
        if (m_task) {
            m_task.release()->deleteLater();
        }
        emit busyChanged();
    };
    connect(m_task.get(), &ai::AiTask::failed, this, [this, finish](const QString &error) {
        finish();
        emit m_editor.message(error, false);
    });
    connect(m_task.get(), &ai::AiTask::canceled, this, finish);
    connect(m_task.get(), &ai::AiTask::finished, this, finish);
    m_task->start();
    emit busyChanged();
    emit progressChanged();
}

bool AiController::removePauses()
{
    if (busy()) {
        return false;
    }
    const std::optional<Target> what = target(false, true);
    if (!what) {
        emit m_editor.message(tr("Select a video or a sound with speech first."), false);
        return false;
    }
    if (const auto known = m_pauses.find(what->fingerprint); known != m_pauses.end()) {
        applyPauses(what->clip, *known);
        return true;
    }
    auto task = std::make_unique<ai::PauseDetection>(what->path);
    ai::PauseDetection *detection = task.get();
    connect(detection, &ai::AiTask::finished, this, [this, detection, what = *what] {
        m_pauses.insert(what.fingerprint, detection->pauses());
        applyPauses(what.clip, detection->pauses());
    });
    run(std::move(task));
    return true;
}

bool AiController::splitScenes()
{
    if (busy()) {
        return false;
    }
    const std::optional<Target> what = target(true, false);
    if (!what) {
        emit m_editor.message(tr("Select a video first."), false);
        return false;
    }
    if (const auto known = m_scenes.find(what->fingerprint); known != m_scenes.end()) {
        applyScenes(what->clip, *known);
        return true;
    }
    auto task = std::make_unique<ai::SceneDetection>(what->path);
    ai::SceneDetection *detection = task.get();
    connect(detection, &ai::AiTask::finished, this, [this, detection, what = *what] {
        m_scenes.insert(what.fingerprint, detection->cuts());
        applyScenes(what.clip, detection->cuts());
    });
    run(std::move(task));
    return true;
}

bool AiController::canStabilize() const
{
    if (busy()) {
        return false;
    }
    const std::optional<Target> what = target(true, false);
    const Clip *clip = what ? m_editor.data().findClip(what->clip) : nullptr;
    return clip && clip->media() && !clip->media()->reversed && !clip->media()->curve;
}

bool AiController::stabilize()
{
    if (busy()) {
        return false;
    }
    const std::optional<Target> what = target(true, false);
    const Clip *clip = what ? m_editor.data().findClip(what->clip) : nullptr;
    const MediaClipData *media = clip ? clip->media() : nullptr;
    if (!media) {
        emit m_editor.message(tr("Select a video first."), false);
        return false;
    }
    if (media->reversed || media->curve) {
        emit m_editor.message(tr("Stabilize works on clips played forwards at a steady speed."), false);
        return false;
    }
    // The part of the file the clip plays, with half a second around it (a trim later still has corrections).
    const double from = std::max(0.0, media->sourceIn.toSecondsDouble() - 0.5);
    const double to = media->sourceIn.toSecondsDouble() + clip->duration.toSecondsDouble() * media->speed + 0.5;
    auto task = std::make_unique<ai::CameraMotionAnalysis>(what->path, from, to);
    ai::CameraMotionAnalysis *analysis = task.get();
    const ClipId clipId = what->clip;
    connect(analysis, &ai::AiTask::finished, this, [this, analysis, clipId] {
        if (!m_editor.data().findClip(clipId)) {
            emit m_editor.message(tr("The clip was removed meanwhile."), false);
            return;
        }
        const QString motion = QString::fromLatin1(fx::encodeCameraSteps(analysis->steps()));
        const QString rate = analysis->frameRate().toString();
        const QString start = Rational(std::llround(analysis->fromSeconds() * 1000.0), 1000).toString();
        EditResult result = TimelineEditor(m_editor.data(), m_editor.data().mainSequenceId)
                                .updateClips({clipId},
                                             [&](Clip &c) {
                                                 auto it = std::find_if(c.effects.begin(), c.effects.end(), [](const Effect &e) {
                                                     return e.type == u"vedit.stabilize"_s;
                                                 });
                                                 if (it == c.effects.end()) {
                                                     Effect effect;
                                                     effect.id = EffectId::create();
                                                     effect.type = u"vedit.stabilize"_s;
                                                     effect.params[u"strength"_s] = Param(0.6);
                                                     c.effects.insert(c.effects.begin(), effect);
                                                     it = c.effects.begin();
                                                 }
                                                 it->enabled = true;
                                                 it->params[u"motion"_s] = Param(motion);
                                                 it->params[u"motionRate"_s] = Param(rate);
                                                 it->params[u"motionStart"_s] = Param(start);
                                             },
                                             tr("Stabilize"));
        if (m_editor.push(std::move(result))) {
            emit m_editor.message(tr("Clip stabilized: change how much in Video"), true);
            emit m_editor.propertiesRequested(u"video"_s);
        }
    });
    run(std::move(task));
    return true;
}

bool AiController::autoReframe(int preset)
{
    if (busy()) {
        return false;
    }
    const Sequence *sequence = m_editor.data().mainSequence();
    const std::optional<Canvas> canvas = m_editor.canvasFor(preset);
    if (!sequence || !canvas) {
        return false;
    }
    // The videos of the main track, each with the part of its file it plays.
    std::vector<ClipId> clips;
    std::vector<ai::SubjectTracking::Part> parts;
    for (const Clip &clip : sequence->visualTracks.front().clips) {
        const MediaClipData *media = clip.media();
        const Media *item = media ? m_editor.data().findMedia(media->mediaId) : nullptr;
        if (!item || item->kind != MediaKind::Video || !item->info.video || media->curve) {
            continue;
        }
        const double first = media->sourceIn.toSecondsDouble();
        const double length = clip.duration.toSecondsDouble() * media->speed;
        clips.push_back(clip.id);
        parts.push_back({item->path, std::max(0.0, first - 0.5), first + length + 0.5});
    }
    if (clips.empty()) {
        m_editor.setCanvasPreset(preset); // nothing to follow: just the format
        return true;
    }
    auto task = std::make_unique<ai::SubjectTracking>(parts);
    ai::SubjectTracking *tracking = task.get();
    const Canvas target = *canvas;
    connect(tracking, &ai::AiTask::finished, this, [this, tracking, clips, target] {
        applyReframe(target, clips, tracking->paths());
    });
    run(std::move(task));
    return true;
}

void AiController::applyReframe(const Canvas &canvas, const std::vector<ClipId> &clips,
                                const std::vector<ai::SubjectTracking::Path> &paths)
{
    const Sequence *sequence = m_editor.data().mainSequence();
    if (!sequence) {
        return;
    }
    const Rational rate = m_editor.data().settings.frameRate;
    std::map<ClipId, Transform> transforms;
    for (size_t i = 0; i < clips.size() && i < paths.size(); ++i) {
        const Clip *clip = m_editor.data().findClip(clips[i]);
        const MediaClipData *media = clip ? clip->media() : nullptr;
        const Media *item = media ? m_editor.data().findMedia(media->mediaId) : nullptr;
        if (!item || !item->info.video) {
            continue;
        }
        // The picture covering the new canvas, in canvas widths and heights.
        const bool turned = item->info.video->rotation == 90 || item->info.video->rotation == 270;
        const double width = turned ? item->info.video->height : item->info.video->width;
        const double height = turned ? item->info.video->width : item->info.video->height;
        const double cover = std::max(canvas.width / width, canvas.height / height);
        const double spanX = width * cover / canvas.width;
        const double spanY = height * cover / canvas.height;
        const auto positionOf = [&](const fx::SubjectPoint &point) {
            const double limitX = (spanX - 1.0) / 2.0;
            const double limitY = (spanY - 1.0) / 2.0;
            return Vec2{std::clamp((0.5 - point.x) * spanX, -limitX, limitX), std::clamp((0.5 - point.y) * spanY, -limitY, limitY)};
        };
        Transform transform = clip->transform;
        transform.fit = FitMode::Cover;
        transform.scale = Param(Vec2{1.0, 1.0});
        const ai::SubjectTracking::Path &path = paths[i];
        // A keyframe every half second, in the clip's keyframe time (seconds of the file for videos).
        std::vector<Keyframe> keyframes;
        double lastSeconds = -1e9;
        double lowX = 1e9, highX = -1e9, lowY = 1e9, highY = -1e9;
        for (size_t k = 0; k < path.points.size() && k < path.times.size(); ++k) {
            if (path.times[k] - lastSeconds < 0.5 - 1e-6) {
                continue;
            }
            lastSeconds = path.times[k];
            const Vec2 position = positionOf(path.points[k]);
            lowX = std::min(lowX, position.x);
            highX = std::max(highX, position.x);
            lowY = std::min(lowY, position.y);
            highY = std::max(highY, position.y);
            const RationalTime time = RationalTime::fromSeconds(Rational(std::llround(path.times[k] * 1000.0), 1000), rate,
                                                                Rounding::NearestEven);
            if (!keyframes.empty() && !(time > keyframes.back().time)) {
                continue;
            }
            keyframes.push_back(Keyframe{time, position, Interpolation::Linear, Easing::preset(Easing::Preset::EaseInOut)});
        }
        if (keyframes.empty()) {
            transform.position = Param(Vec2{0.0, 0.0});
        } else if (highX - lowX < 0.01 && highY - lowY < 0.01) {
            transform.position = Param(keyframes.front().value); // the subject stays put: no movement at all
        } else {
            Param position;
            position.setKeyframes(std::move(keyframes));
            transform.position = position;
        }
        transforms.emplace(clips[i], transform);
    }
    EditResult result = TimelineEditor(m_editor.data(), m_editor.data().mainSequenceId)
                            .updateClips(clips,
                                         [&transforms](Clip &c) {
                                             if (const auto it = transforms.find(c.id); it != transforms.end()) {
                                                 c.transform = it->second;
                                             }
                                         },
                                         tr("Auto reframe"));
    if (!result.ok()) {
        emit m_editor.message(result.error, false);
        return;
    }
    if (!(canvas == sequence->canvas)) {
        result.script.insert(result.script.begin(), edits::setCanvas(m_editor.data().mainSequenceId, sequence->canvas, canvas));
    }
    if (m_editor.push(std::move(result))) {
        emit m_editor.message(tr("Reframed: the videos follow their subject (keyframes in Video)"), true);
    }
}

void AiController::cancel()
{
    if (m_task) {
        m_task->cancel();
    }
}

void AiController::applyPauses(const ClipId &clipId, const std::vector<ai::SourceRange> &pauses)
{
    const Clip *clip = m_editor.data().findClip(clipId);
    const MediaClipData *media = clip ? clip->media() : nullptr;
    if (!media) {
        emit m_editor.message(tr("The clip was removed meanwhile."), false);
        return;
    }
    // The analysis covers the whole file: the pauses in the part the clip plays.
    const Rational rate = m_editor.data().settings.frameRate;
    const RationalTime from = media->sourceIn.rescaled(rate, Rounding::NearestEven);
    const RationalTime to = from + RationalTime(std::llround(static_cast<double>(clip->duration.value()) * media->speed), rate);
    std::vector<std::pair<RationalTime, RationalTime>> ranges;
    for (const ai::SourceRange &pause : pauses) {
        if (pause.end > from && pause.start < to) {
            ranges.emplace_back(pause.start, pause.end);
        }
    }
    if (ranges.empty()) {
        emit m_editor.message(tr("No pauses found in this clip."), false);
        return;
    }
    EditResult result = TimelineEditor(m_editor.data(), m_editor.data().mainSequenceId).removeSourceRanges(clipId, ranges);
    if (!result.ok()) {
        emit m_editor.message(result.error, false);
        return;
    }
    if (m_editor.push(std::move(result))) {
        emit m_editor.message(tr("Pauses removed"), true);
    }
}

void AiController::applyScenes(const ClipId &clipId, const std::vector<double> &cuts)
{
    const Clip *clip = m_editor.data().findClip(clipId);
    const MediaClipData *media = clip ? clip->media() : nullptr;
    if (!media) {
        emit m_editor.message(tr("The clip was removed meanwhile."), false);
        return;
    }
    if (media->reversed || media->curve) {
        emit m_editor.message(tr("Scenes are found in clips played forwards at a steady speed."), false);
        return;
    }
    // Seconds of the file → times of the timeline, for the part of the file the clip plays.
    const Rational rate = m_editor.data().settings.frameRate;
    std::vector<RationalTime> times;
    for (const double seconds : cuts) {
        const RationalTime source = RationalTime::fromSeconds(Rational(std::llround(seconds * 1000.0), 1000), rate,
                                                              Rounding::NearestEven);
        const double offset = static_cast<double>((source - media->sourceIn.rescaled(rate, Rounding::NearestEven)).value()) /
                              media->speed;
        times.push_back(clip->start + RationalTime(std::llround(offset), rate));
    }
    EditResult result = TimelineEditor(m_editor.data(), m_editor.data().mainSequenceId).splitClipAt(clipId, times);
    if (!result.ok()) {
        emit m_editor.message(tr("No scene changes found in this clip."), false);
        return;
    }
    if (m_editor.push(std::move(result))) {
        emit m_editor.message(tr("Clip split at each scene"), true);
    }
}

} // namespace vedit::ui
