// SPDX-License-Identifier: GPL-3.0-or-later
#include "AiController.h"

#include "ai/Tasks.h"
#include "core/commands/Edit.h"
#include "core/edit/TimelineEditor.h"
#include "engine/timeline/ClipPlacement.h"
#include "core/serialization/ProjectJson.h"
#include "fx/Library.h"
#include "ui/controllers/CaptionsController.h"
#include "ui/controllers/TranscriptController.h"
#include "fx/Stabilization.h"
#include "ui/controllers/EditorController.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>

using namespace Qt::StringLiterals;

namespace velacut::ui {

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
    if (m_task) {
        // Started from the result of the task finishing now (analysis, then tracking): that one goes once its signal
        // is over.
        m_task.release()->deleteLater();
    }
    m_task = std::move(task);
    connect(m_task.get(), &ai::AiTask::progressChanged, this, &AiController::progressChanged);
    const ai::AiTask *current = m_task.get();
    const auto finish = [this, current] {
        // Deleted later: this runs inside one of the task's signals. A task started meanwhile stays.
        if (m_task.get() == current) {
            m_task.release()->deleteLater();
            emit busyChanged();
        }
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

bool AiController::canSeparateVoice() const
{
    if (busy()) {
        return false;
    }
    const std::optional<Target> what = target(false, true);
    const Clip *clip = what ? m_editor.data().findClip(what->clip) : nullptr;
    return clip && clip->media() && !clip->media()->reversed && !clip->media()->curve;
}

bool AiController::separateVoice()
{
    if (busy()) {
        return false;
    }
    const std::optional<Target> what = target(false, true);
    const Clip *clip = what ? m_editor.data().findClip(what->clip) : nullptr;
    if (!clip || !clip->media()) {
        emit m_editor.message(tr("Select a video or a sound first."), false);
        return false;
    }
    if (clip->media()->reversed || clip->media()->curve) {
        emit m_editor.message(tr("Voice and music are separated on clips played forwards at a steady speed."), false);
        return false;
    }
    const bool cached = QFileInfo(ai::VoiceSeparation::voicePathOf(what->fingerprint)).size() > 0;
    if (!cached && ai::demucs::executable().isEmpty()) {
        emit m_editor.message(tr("Separating voice and music needs Demucs, which is not installed: “%1”.").arg(ai::demucs::installCommand()),
                              false);
        return false;
    }
    auto task = std::make_unique<ai::VoiceSeparation>(what->path, what->fingerprint);
    ai::VoiceSeparation *separation = task.get();
    const ClipId clipId = what->clip;
    connect(separation, &ai::AiTask::finished, this, [this, separation, clipId] {
        const QString voice = separation->voicePath();
        const QString music = separation->musicPath();
        m_editor.importThen({voice, music}, [this, clipId, voice, music](const QHash<QString, MediaId> &media) {
            const Clip *clip = m_editor.data().findClip(clipId);
            if (!clip || !clip->media() || !media.contains(voice) || !media.contains(music)) {
                emit m_editor.message(tr("The separated sounds could not be added."), false);
                return;
            }
            // Both sounds under the clip, playing the same part of the file at the same speed; the clip goes quiet.
            const MergeKey step{u"separate"_s, static_cast<quint64>(QDateTime::currentMSecsSinceEpoch())};
            const RationalTime start = clip->start;
            const TimeRange part{clip->media()->sourceIn, RationalTime(std::llround(clip->duration.value() * clip->media()->speed),
                                                                       clip->duration.rate())};
            const double speed = clip->media()->speed;
            for (const QString &path : {voice, music}) {
                EditResult insert = TimelineEditor(m_editor.data(), m_editor.data().mainSequenceId)
                                        .insertMedia(media.value(path), start, part);
                const ClipId added = insert.primaryClip;
                if (!m_editor.push(std::move(insert), step)) {
                    return;
                }
                if (speed != 1.0) {
                    m_editor.push(TimelineEditor(m_editor.data(), m_editor.data().mainSequenceId).setSpeed(added, speed), step);
                }
            }
            m_editor.push(TimelineEditor(m_editor.data(), m_editor.data().mainSequenceId)
                              .updateClips({clipId}, [](Clip &c) { c.media()->audio.muted = true; }, tr("Separate voice and music")),
                          step);
            emit m_editor.message(tr("Voice and music separated: two sounds under the clip"), true);
        });
    });
    run(std::move(task));
    return true;
}

bool AiController::canRemoveBackground() const
{
    if (busy()) {
        return false;
    }
    const std::optional<Target> what = target(true, false);
    const Clip *clip = what ? m_editor.data().findClip(what->clip) : nullptr;
    return clip && clip->media() && !clip->media()->cutout && !clip->media()->reversed && !clip->media()->curve;
}

bool AiController::removeBackground()
{
    if (busy()) {
        return false;
    }
    const std::optional<Target> what = target(true, false);
    const Clip *clip = what ? m_editor.data().findClip(what->clip) : nullptr;
    const Media *media = clip && clip->media() ? m_editor.data().findMedia(clip->media()->mediaId) : nullptr;
    const std::optional<engine::CutoutCopy> copy = media ? engine::cutoutCopyFor(*clip, *media, true) : std::nullopt;
    if (!copy) {
        emit m_editor.message(tr("Select a video played forwards at a steady speed first."), false);
        return false;
    }
    const ClipId clipId = what->clip;
    const auto apply = [this, clipId] {
        if (m_editor.push(TimelineEditor(m_editor.data(), m_editor.data().mainSequenceId)
                              .updateClips({clipId}, [](Clip &c) { c.media()->cutout = true; }, tr("Remove background")))) {
            emit m_editor.message(tr("Background removed: what is under the clip shows through"), true);
        }
    };
    if (copy->ready()) {
        apply();
        return true;
    }
    if (engine::rembg::executable().isEmpty()) {
        emit m_editor.message(tr("Removing the background needs rembg, which is not installed: “%1”.").arg(engine::rembg::installCommand()),
                              false);
        return false;
    }
    if (!engine::rembg::hasModel() && !m_rembgDownloadExplained) {
        // Never a silent download (SPEC §1): say it, and go on only with a second click.
        m_rembgDownloadExplained = true;
        emit m_editor.message(tr("The first time, rembg downloads its model (about 170 MB). Click “Remove background” again to go on."),
                              false);
        return false;
    }
    auto task = std::make_unique<ai::BackgroundRemoval>(*copy);
    connect(task.get(), &ai::AiTask::finished, this, apply);
    run(std::move(task));
    return true;
}

namespace {

// The video clip under `overlay` at its start (the nearest lower track that has one), if any.
const Clip *videoUnder(const ProjectData &data, const Clip &overlay)
{
    const Sequence *sequence = data.mainSequence();
    if (!sequence) {
        return nullptr;
    }
    int overlayTrack = -1;
    for (size_t t = 0; t < sequence->visualTracks.size(); ++t) {
        if (sequence->visualTracks[t].findClip(overlay.id)) {
            overlayTrack = static_cast<int>(t);
        }
    }
    for (int t = overlayTrack - 1; t >= 0; --t) {
        for (const Clip &clip : sequence->visualTracks[static_cast<size_t>(t)].clips) {
            const MediaClipData *media = clip.media();
            const Media *item = media ? data.findMedia(media->mediaId) : nullptr;
            if (item && item->kind == MediaKind::Video && clip.start <= overlay.start && clip.end() > overlay.start) {
                return &clip;
            }
        }
    }
    return nullptr;
}

} // namespace

bool AiController::canTrackMotion() const
{
    const std::optional<ClipId> focus = m_editor.focusClip();
    const Clip *clip = focus ? m_editor.data().findClip(*focus) : nullptr;
    return !busy() && clip && (clip->text() || clip->sticker()) && videoUnder(m_editor.data(), *clip);
}

bool AiController::trackMotion()
{
    if (busy()) {
        return false;
    }
    const std::optional<ClipId> focus = m_editor.focusClip();
    const Clip *overlay = focus ? m_editor.data().findClip(*focus) : nullptr;
    if (!overlay || (!overlay->text() && !overlay->sticker())) {
        emit m_editor.message(tr("Select a text or a sticker over a video first."), false);
        return false;
    }
    const Clip *under = videoUnder(m_editor.data(), *overlay);
    const MediaClipData *media = under ? under->media() : nullptr;
    const Media *item = media ? m_editor.data().findMedia(media->mediaId) : nullptr;
    const ParamValue rotation = under ? under->transform.rotation.staticValue() : ParamValue(0.0);
    const bool turned = under && (under->transform.rotation.isAnimated() ||
                                  (std::holds_alternative<double>(rotation) && std::abs(std::get<double>(rotation)) > 0.01));
    if (!item || !item->info.video || media->reversed || media->curve || turned) {
        emit m_editor.message(tr("Put the text or sticker over a video played forwards (not turned) to follow it."), false);
        return false;
    }
    const Canvas canvas = m_editor.data().mainSequence()->canvas;
    const QSize size(canvas.width, canvas.height);
    const engine::CanvasBox overlayBox = engine::canvasBox(*overlay, nullptr, size);
    const engine::CanvasBox videoBox = engine::canvasBox(*under, item, size);
    const double u = (overlayBox.centre.x() - videoBox.centre.x()) / videoBox.size.width() + 0.5;
    const double v = (overlayBox.centre.y() - videoBox.centre.y()) / videoBox.size.height() + 0.5;
    if (u < 0.0 || u > 1.0 || v < 0.0 || v > 1.0) {
        emit m_editor.message(tr("Place the text or sticker on the part of the video to follow."), false);
        return false;
    }
    // The part of the file under the overlay.
    const RationalTime from = std::max(overlay->start, under->start);
    const RationalTime to = std::min(overlay->end(), under->end());
    const auto sourceSeconds = [&](const RationalTime &t) {
        return media->sourceIn.toSecondsDouble() + (t - under->start).toSecondsDouble() * media->speed;
    };
    const int fileRotation = item->info.video->rotation;
    const double storedAspect = (fileRotation == 90 || fileRotation == 270) ? double(item->info.video->height) / item->info.video->width
                                                                    : double(item->info.video->width) / item->info.video->height;
    auto task = std::make_unique<ai::MotionTracking>(item->path, sourceSeconds(from), sourceSeconds(to), u, v, 0.08, fileRotation,
                                                     storedAspect);
    ai::MotionTracking *tracking = task.get();
    const ClipId overlayId = overlay->id;
    const ClipId underId = under->id;
    const QPointF start = overlayBox.centre;
    connect(tracking, &ai::AiTask::finished, this, [this, tracking, overlayId, underId, start, size] {
        const Clip *overlay = m_editor.data().findClip(overlayId);
        const Clip *under = m_editor.data().findClip(underId);
        const MediaClipData *media = under ? under->media() : nullptr;
        const Media *item = media ? m_editor.data().findMedia(media->mediaId) : nullptr;
        if (!overlay || !item) {
            emit m_editor.message(tr("The clip was removed meanwhile."), false);
            return;
        }
        const engine::CanvasBox videoBox = engine::canvasBox(*under, item, size);
        const Rational rate = m_editor.data().settings.frameRate;
        const Vec2 base = std::holds_alternative<Vec2>(overlay->transform.position.staticValue())
                              ? std::get<Vec2>(overlay->transform.position.staticValue())
                              : Vec2{0.0, 0.0};
        std::vector<Keyframe> keyframes;
        int lost = 0;
        const auto &points = tracking->points();
        for (size_t i = 0; i < points.size(); ++i) {
            const engine::TrackedPoint &point = points[i];
            lost += point.lost ? 1 : 0;
            if (point.lost || (i % 2 != 0 && i + 1 != points.size())) {
                continue; // a keyframe every other frame is smooth enough
            }
            // Back on the timeline, from the start of the overlay.
            const double timeline = under->start.toSecondsDouble() + (point.seconds - media->sourceIn.toSecondsDouble()) / media->speed;
            const RationalTime local = RationalTime::fromSeconds(
                Rational(std::llround((timeline - overlay->start.toSecondsDouble()) * 1000.0), 1000), rate, Rounding::NearestEven);
            if (local.isNegative() || local > overlay->duration || (!keyframes.empty() && !(local > keyframes.back().time))) {
                continue;
            }
            const double x = videoBox.centre.x() + (point.x - 0.5) * videoBox.size.width();
            const double y = videoBox.centre.y() + (point.y - 0.5) * videoBox.size.height();
            keyframes.push_back(Keyframe{local, Vec2{base.x + (x - start.x()) / size.width(), base.y + (y - start.y()) / size.height()},
                                         Interpolation::Linear, Easing::preset(Easing::Preset::Linear)});
        }
        if (keyframes.size() < 2) {
            emit m_editor.message(tr("Nothing to follow was found under it: try another place."), false);
            return;
        }
        Param position;
        position.setKeyframes(std::move(keyframes));
        if (m_editor.push(TimelineEditor(m_editor.data(), m_editor.data().mainSequenceId)
                              .updateClips({overlayId}, [&position](Clip &c) { c.transform.position = position; }, tr("Track motion")))) {
            emit m_editor.message(lost * 3 > static_cast<int>(points.size())
                                      ? tr("It follows the movement, but lost it for a while: check the keyframes in Video")
                                      : tr("It follows the movement (keyframes in Video)"),
                                  true);
        }
    });
    run(std::move(task));
    return true;
}

QString AiController::voiceName() const
{
    const QStringList voices = ai::piper::voices();
    return voices.isEmpty() || ai::piper::executable().isEmpty() ? QString() : ai::piper::voiceName(voices.front());
}

bool AiController::synthesizeAll(const QStringList &texts, std::function<void(const QStringList &)> done)
{
    if (busy() || voiceName().isEmpty()) {
        return false;
    }
    synthesizeNext(texts, {}, std::move(done));
    return true;
}

void AiController::synthesizeNext(QStringList texts, QStringList done, std::function<void(const QStringList &)> finished)
{
    if (texts.isEmpty()) {
        finished(done);
        return;
    }
    const QString text = texts.takeFirst();
    auto task = std::make_unique<ai::SpeechSynthesis>(text.simplified(), ai::piper::voices().front());
    ai::SpeechSynthesis *speech = task.get();
    const auto next = [this, speech, texts, done, finished](bool ok) {
        QStringList files = done;
        files << (ok ? speech->outputPath() : QString());
        // After this task is cleaned up (the next one starts from a clean state).
        QMetaObject::invokeMethod(this, [this, texts, files, finished] { synthesizeNext(texts, files, finished); }, Qt::QueuedConnection);
    };
    connect(speech, &ai::AiTask::finished, this, [next] { next(true); });
    connect(speech, &ai::AiTask::failed, this, [next] { next(false); });
    connect(speech, &ai::AiTask::canceled, this, [finished, done] { finished(done); });
    run(std::move(task));
}

bool AiController::canFindHighlights() const
{
    if (busy()) {
        return false;
    }
    const std::optional<Target> what = target(false, false);
    const Clip *clip = what ? m_editor.data().findClip(what->clip) : nullptr;
    return clip && clip->media() && !clip->media()->reversed && !clip->media()->curve && clip->duration.toSecondsDouble() >= 30.0;
}

bool AiController::withHighlightInput(const std::function<void(const ClipId &, const ai::HighlightInput &)> &then)
{
    if (busy()) {
        return false;
    }
    const std::optional<Target> what = target(false, false);
    const Clip *clip = what ? m_editor.data().findClip(what->clip) : nullptr;
    if (!clip || !clip->media() || clip->media()->reversed || clip->media()->curve) {
        emit m_editor.message(tr("Select a long video played forwards at a steady speed first."), false);
        return false;
    }
    if (clip->duration.toSecondsDouble() < 30.0) {
        emit m_editor.message(tr("This works on clips of at least 30 seconds."), false);
        return false;
    }
    const ClipId clipId = what->clip;
    if (const auto known = m_highlightInputs.constFind(what->fingerprint); known != m_highlightInputs.constEnd()) {
        then(clipId, *known);
        return true;
    }
    auto task = std::make_unique<ai::HighlightAnalysis>(what->path);
    ai::HighlightAnalysis *analysis = task.get();
    const QString fingerprint = what->fingerprint;
    connect(analysis, &ai::AiTask::finished, this, [this, analysis, clipId, fingerprint, then] {
        m_highlightInputs.insert(fingerprint, analysis->input());
        then(clipId, analysis->input());
    });
    run(std::move(task));
    return true;
}

namespace {

// The highlight input of the part of the file a clip plays, in seconds from the start of that part, with the words
// said there when the file was transcribed.
ai::HighlightInput clipPart(const ai::HighlightInput &file, const Clip &clip, const ai::Transcript *transcript)
{
    const MediaClipData &media = *clip.media();
    const double from = media.sourceIn.toSecondsDouble();
    const double length = clip.duration.toSecondsDouble() * media.speed;
    ai::HighlightInput part;
    part.levelsPerSecond = file.levelsPerSecond;
    part.seconds = length;
    const auto first = static_cast<size_t>(std::max(0.0, from * file.levelsPerSecond));
    const auto last = std::min(file.levels.size(), static_cast<size_t>((from + length) * file.levelsPerSecond));
    if (last > first) {
        part.levels.assign(file.levels.begin() + static_cast<std::ptrdiff_t>(first), file.levels.begin() + static_cast<std::ptrdiff_t>(last));
    }
    for (const double cut : file.cuts) {
        if (cut > from && cut < from + length) {
            part.cuts.push_back(cut - from);
        }
    }
    if (transcript) {
        for (const ai::Transcript::Word &word : transcript->words) {
            const double t = word.from / 1000.0;
            if (t >= from && t < from + length) {
                part.words.emplace_back(t - from, word.to / 1000.0 - from);
            }
        }
    }
    return part;
}

// The transform of a video filling `canvas` with its position following the subject found along `path` (a keyframe
// every half second, in the clip's keyframe time: seconds of the file for videos), or centred when nothing moves.
Transform followSubject(Transform transform, const Media &item, const Canvas &canvas, const ai::SubjectTracking::Path &path,
                        Rational rate)
{
    // The picture covering the new canvas, in canvas widths and heights.
    const bool turned = item.info.video->rotation == 90 || item.info.video->rotation == 270;
    const double width = turned ? item.info.video->height : item.info.video->width;
    const double height = turned ? item.info.video->width : item.info.video->height;
    const double cover = std::max(canvas.width / width, canvas.height / height);
    const double spanX = width * cover / canvas.width;
    const double spanY = height * cover / canvas.height;
    const auto positionOf = [&](const fx::SubjectPoint &point) {
        const double limitX = (spanX - 1.0) / 2.0;
        const double limitY = (spanY - 1.0) / 2.0;
        return Vec2{std::clamp((0.5 - point.x) * spanX, -limitX, limitX), std::clamp((0.5 - point.y) * spanY, -limitY, limitY)};
    };
    transform.fit = FitMode::Cover;
    transform.scale = Param(Vec2{1.0, 1.0});
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
    return transform;
}

} // namespace

bool AiController::highlights()
{
    return withHighlightInput([this](const ClipId &clipId, const ai::HighlightInput &file) {
        const Clip *clip = m_editor.data().findClip(clipId);
        const Media *item = clip && clip->media() ? m_editor.data().findMedia(clip->media()->mediaId) : nullptr;
        if (!item) {
            emit m_editor.message(tr("The clip was removed meanwhile."), false);
            return;
        }
        const ai::HighlightInput part = clipPart(file, *clip, transcriptOf(*item));
        const double target = part.seconds > 120.0 ? 60.0 : std::max(10.0, part.seconds * 0.3);
        const std::vector<ai::Span> keep = ai::findHighlights(part, target);
        if (keep.empty()) {
            emit m_editor.message(tr("No highlights found in this clip."), false);
            return;
        }
        // Everything else goes: the ranges between the kept pieces, in the file's time.
        const double from = clip->media()->sourceIn.toSecondsDouble();
        const auto ms = [](double seconds) { return RationalTime(std::llround(seconds * 1000.0), Rational(1000)); };
        std::vector<std::pair<RationalTime, RationalTime>> cuts;
        double cursor = 0.0;
        for (const ai::Span &span : keep) {
            if (span.from > cursor + 0.05) {
                cuts.emplace_back(ms(from + cursor), ms(from + span.from));
            }
            cursor = span.to;
        }
        if (cursor < part.seconds - 0.05) {
            cuts.emplace_back(ms(from + cursor), ms(from + part.seconds));
        }
        if (m_editor.push(TimelineEditor(m_editor.data(), m_editor.data().mainSequenceId).removeSourceRanges({{clipId, cuts}}, tr("Highlights")))) {
            double total = 0.0;
            for (const ai::Span &span : keep) {
                total += span.length();
            }
            emit m_editor.message(tr("Highlights: %n s kept from the best moments", nullptr, static_cast<int>(std::lround(total))), true);
        }
    });
}

bool AiController::makeShortClips()
{
    return withHighlightInput([this](const ClipId &clipId, const ai::HighlightInput &file) {
        const Clip *clip = m_editor.data().findClip(clipId);
        const Media *item = clip && clip->media() ? m_editor.data().findMedia(clip->media()->mediaId) : nullptr;
        if (!item) {
            emit m_editor.message(tr("The clip was removed meanwhile."), false);
            return;
        }
        const ai::Transcript *transcript = transcriptOf(*item);
        const ai::HighlightInput part = clipPart(file, *clip, transcript);
        const int count = std::clamp(static_cast<int>(part.seconds / 120.0), 3, 10);
        const std::vector<ai::Span> spans = ai::findShortClips(part, count, 30.0);
        if (spans.empty()) {
            emit m_editor.message(tr("The clip is too short for short clips (they last 15–60 s)."), false);
            return;
        }
        // Each clip reframed to 9:16 following its subject (the same tracking as "Auto reframe").
        const std::optional<Canvas> canvas = m_editor.canvasFor(static_cast<int>(CanvasPreset::Portrait9x16));
        if (!item->info.video || item->kind != MediaKind::Video || !canvas) {
            writeShortClips(clipId, spans, {});
            return;
        }
        const double from = clip->media()->sourceIn.toSecondsDouble();
        std::vector<ai::SubjectTracking::Part> parts;
        for (const ai::Span &span : spans) {
            parts.push_back({item->path, std::max(0.0, from + span.from - 0.5), from + span.to + 0.5});
        }
        auto task = std::make_unique<ai::SubjectTracking>(parts);
        ai::SubjectTracking *tracking = task.get();
        const Canvas target = *canvas;
        connect(tracking, &ai::AiTask::finished, this, [this, tracking, clipId, spans, target] {
            const Clip *shot = m_editor.data().findClip(clipId);
            const Media *source = shot && shot->media() ? m_editor.data().findMedia(shot->media()->mediaId) : nullptr;
            std::vector<Transform> framing;
            if (source) {
                for (const ai::SubjectTracking::Path &path : tracking->paths()) {
                    framing.push_back(followSubject(Transform{}, *source, target, path, m_editor.data().settings.frameRate));
                }
            }
            writeShortClips(clipId, spans, framing);
        });
        run(std::move(task));
    });
}

void AiController::writeShortClips(const ClipId &clipId, const std::vector<ai::Span> &spans, const std::vector<Transform> &framing)
{
    const Clip *clip = m_editor.data().findClip(clipId);
    const Media *item = clip && clip->media() ? m_editor.data().findMedia(clip->media()->mediaId) : nullptr;
    if (!item) {
        emit m_editor.message(tr("The clip was removed meanwhile."), false);
        return;
    }
    const int made = m_editor.makeShortClipDrafts(clipId, spans, transcriptOf(*item), framing);
    if (made > 0) {
        emit m_editor.message(tr("%n short clip(s) made: they are on the home screen, ready to edit", nullptr, made), false);
    } else {
        emit m_editor.message(tr("The short clips could not be written."), false);
    }
}

bool AiController::canReadAloud() const
{
    const std::optional<ClipId> focus = m_editor.focusClip();
    const Clip *clip = focus ? m_editor.data().findClip(*focus) : nullptr;
    return !busy() && clip && (clip->text() || clip->subtitle());
}

bool AiController::readAloud()
{
    if (busy()) {
        return false;
    }
    const std::optional<ClipId> focus = m_editor.focusClip();
    const Clip *clip = focus ? m_editor.data().findClip(*focus) : nullptr;
    const QString text = !clip ? QString() : clip->text() ? clip->text()->text : clip->subtitle() ? clip->subtitle()->text : QString();
    if (text.trimmed().isEmpty()) {
        emit m_editor.message(tr("Select a text first."), false);
        return false;
    }
    if (ai::piper::executable().isEmpty()) {
        emit m_editor.message(tr("Reading aloud needs Piper, which is not installed: “%1”.").arg(ai::piper::installCommand()), false);
        return false;
    }
    const QStringList voices = ai::piper::voices();
    if (voices.isEmpty()) {
        emit m_editor.message(tr("Add a Piper voice first (Preferences → AI models)."), false);
        return false;
    }
    auto task = std::make_unique<ai::SpeechSynthesis>(text.simplified(), voices.front());
    ai::SpeechSynthesis *speech = task.get();
    const ClipId clipId = clip->id;
    connect(speech, &ai::AiTask::finished, this, [this, speech, clipId] {
        const QString file = speech->outputPath();
        m_editor.importThen({file}, [this, clipId, file](const QHash<QString, MediaId> &media) {
            const Clip *clip = m_editor.data().findClip(clipId);
            if (!clip || !media.contains(file)) {
                emit m_editor.message(tr("The speech could not be added."), false);
                return;
            }
            if (m_editor.push(TimelineEditor(m_editor.data(), m_editor.data().mainSequenceId).insertMedia(media.value(file), clip->start))) {
                emit m_editor.message(tr("The text is read aloud under it"), true);
            }
        });
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
        transforms.emplace(clips[i], followSubject(clip->transform, *item, canvas, paths[i], rate));
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

int AiController::speechStatus() const
{
    if (ai::whisper::executable().isEmpty()) {
        return 1;
    }
    return ai::whisper::bestInstalled().isEmpty() ? 2 : 0;
}

QString AiController::speechInstallCommand() const
{
    return ai::whisper::installCommand();
}

void AiController::refreshSpeech()
{
    emit speechStatusChanged();
}

bool AiController::startTranscription(const QString &language,
                                      std::function<void(const QHash<QString, ai::Transcript> &)> then)
{
    if (busy()) {
        return false;
    }
    switch (speechStatus()) {
    case 1:
        emit m_editor.message(tr("Automatic captions need whisper.cpp: install it with “%1”.").arg(ai::whisper::installCommand()), false);
        return false;
    case 2:
        emit m_editor.message(tr("Download a speech model first (Preferences → AI models)."), false);
        return false;
    default:
        break;
    }
    const Sequence *sequence = m_editor.data().mainSequence();
    std::vector<ai::Transcription::File> files;
    QSet<QString> seen;
    for (const Clip &clip : sequence ? sequence->visualTracks.front().clips : std::vector<Clip>{}) {
        const MediaClipData *media = clip.media();
        const Media *item = media ? m_editor.data().findMedia(media->mediaId) : nullptr;
        if (!item || !item->info.audio || media->streams == Streams::VideoOnly || media->audio.muted) {
            continue;
        }
        if (!seen.contains(item->fingerprint.value)) {
            seen.insert(item->fingerprint.value);
            files.push_back({item->path, item->fingerprint.value});
        }
    }
    if (files.empty()) {
        emit m_editor.message(tr("There is no sound on the main track to make captions from."), false);
        return false;
    }
    auto task = std::make_unique<ai::Transcription>(files, ai::whisper::bestInstalled(), language);
    ai::Transcription *transcription = task.get();
    connect(transcription, &ai::AiTask::finished, this, [this, transcription, then = std::move(then)] {
        for (auto it = transcription->transcripts().begin(); it != transcription->transcripts().end(); ++it) {
            m_transcripts.insert(it.key(), it.value());
        }
        emit transcriptsChanged();
        then(transcription->transcripts());
    });
    run(std::move(task));
    return true;
}

bool AiController::autoCaptions(const QString &language, bool bilingual)
{
    if (!bilingual) {
        return startTranscription(language, [this](const QHash<QString, ai::Transcript> &transcripts) { applyCaptions(transcripts); });
    }
    return startTranscription(language, [this, language](const QHash<QString, ai::Transcript> &transcripts) {
        // The files whose speech is not English already, translated by a second pass of whisper.cpp.
        std::vector<ai::Transcription::File> files;
        for (const Media &item : m_editor.data().media) {
            const auto it = transcripts.constFind(item.fingerprint.value);
            if (it != transcripts.constEnd() && it->language != u"en"_s &&
                std::none_of(files.begin(), files.end(), [&item](const auto &f) { return f.fingerprint == item.fingerprint.value; })) {
                files.push_back({item.path, item.fingerprint.value});
            }
        }
        if (files.empty()) {
            emit m_editor.message(tr("The speech is in English already: captions in one language."), false);
            applyCaptions(transcripts);
            return;
        }
        auto task = std::make_unique<ai::Transcription>(files, ai::whisper::bestInstalled(), language, true);
        ai::Transcription *translation = task.get();
        connect(translation, &ai::AiTask::finished, this, [this, translation, transcripts] {
            applyCaptions(transcripts, translation->transcripts());
        });
        run(std::move(task));
    });
}

bool AiController::captionsFromScript(const QString &script, const QString &language)
{
    if (captions::splitWords(script).isEmpty()) {
        emit m_editor.message(tr("Paste the script first."), false);
        return false;
    }
    const auto align = [this, script] {
        const std::vector<ai::SpokenWord> spoken = m_editor.transcript()->spokenWords();
        if (spoken.empty()) {
            emit m_editor.message(tr("No speech was recognised on the main track."), false);
            return;
        }
        ai::Transcript written;
        for (const ai::SpokenWord &word : ai::alignScript(script, spoken)) {
            written.words.push_back(ai::Transcript::Word{word.text, word.from, word.to});
        }
        const auto toTimeline = [](std::int64_t ms) { return RationalTime(ms, Rational(1000)); };
        placeCaptions(ai::captionLines(written, 0, std::numeric_limits<std::int64_t>::max() / 2, toTimeline));
    };
    if (m_editor.transcript()->available() && !m_editor.transcript()->incomplete()) {
        align();
        return true;
    }
    return startTranscription(language, [align](const QHash<QString, ai::Transcript> &) { align(); });
}

bool AiController::transcribe(const QString &language)
{
    return startTranscription(language, [this](const QHash<QString, ai::Transcript> &) {
        emit m_editor.message(tr("Transcript ready: delete words to cut them from the video"), false);
    });
}

const ai::Transcript *AiController::transcriptOf(const Media &media) const
{
    const QString fingerprint = media.fingerprint.value;
    if (const auto it = m_transcripts.constFind(fingerprint); it != m_transcripts.constEnd()) {
        return &it.value();
    }
    // Made in another session: the cache, best model first.
    std::vector<ai::whisper::Model> models = ai::whisper::catalog();
    std::sort(models.begin(), models.end(), [](const auto &a, const auto &b) { return a.quality > b.quality; });
    const QDir folder(QFileInfo(ai::whisper::transcriptCachePath(fingerprint, u"x"_s, u"x"_s)).absolutePath());
    for (const ai::whisper::Model &model : models) {
        for (const QString &name : folder.entryList({u"transcript-"_s + model.id + u"-*.json"_s}, QDir::Files)) {
            QFile file(folder.filePath(name));
            if (!file.open(QIODevice::ReadOnly)) {
                continue;
            }
            if (const auto transcript = ai::Transcript::fromJson(QJsonDocument::fromJson(file.readAll()).object())) {
                return &m_transcripts.insert(fingerprint, *transcript).value();
            }
        }
    }
    return nullptr;
}

void AiController::applyCaptions(const QHash<QString, ai::Transcript> &transcripts, const QHash<QString, ai::Transcript> &translations)
{
    const Sequence *sequence = m_editor.data().mainSequence();
    if (!sequence) {
        return;
    }
    const Rational rate = m_editor.data().settings.frameRate;
    std::vector<captions::CaptionLine> lines;
    for (const Clip &clip : sequence->visualTracks.front().clips) {
        const MediaClipData *media = clip.media();
        const Media *item = media ? m_editor.data().findMedia(media->mediaId) : nullptr;
        if (!item || media->reversed || media->curve || !transcripts.contains(item->fingerprint.value)) {
            continue;
        }
        const std::int64_t sourceInMs = media->sourceIn.rescaled(Rational(1000), Rounding::NearestEven).value();
        const std::int64_t lengthMs = std::llround(clip.duration.toSecondsDouble() * media->speed * 1000.0);
        const double speed = media->speed;
        const RationalTime start = clip.start;
        const auto toTimeline = [&](std::int64_t ms) {
            const double frames = static_cast<double>(ms - sourceInMs) / speed * rate.toDouble() / 1000.0;
            return start + RationalTime(std::llround(frames), rate);
        };
        std::vector<captions::CaptionLine> clipLines =
            ai::captionLines(transcripts.value(item->fingerprint.value), sourceInMs, sourceInMs + lengthMs, toTimeline);
        if (const auto translation = translations.constFind(item->fingerprint.value); translation != translations.constEnd()) {
            ai::attachTranslation(clipLines, *translation, sourceInMs, sourceInMs + lengthMs, toTimeline);
        }
        for (captions::CaptionLine &line : clipLines) {
            lines.push_back(std::move(line));
        }
    }
    if (lines.empty()) {
        emit m_editor.message(tr("No speech was recognised on the main track."), false);
        return;
    }
    placeCaptions(lines);
}

void AiController::placeCaptions(const std::vector<captions::CaptionLine> &lines)
{
    const Sequence *sequence = m_editor.data().mainSequence();
    if (!sequence) {
        return;
    }
    const bool replace = std::any_of(sequence->visualTracks.begin(), sequence->visualTracks.end(),
                                     [](const Track &track) { return track.captions; });
    // New captions: the style chosen in the library, else an animated social one (word by word, like CapCut's).
    std::optional<CaptionStyle> style = m_editor.captions()->nextStyle();
    if (!replace && !style) {
        if (const fx::CaptionStylePreset *preset = fx::Library::core().captionStyle(u"captions/pop-three"_s)) {
            style = projectjson::captionStyleFromJson(preset->style);
            style->preset = preset->id;
        }
    }
    EditResult result = TimelineEditor(m_editor.data(), m_editor.data().mainSequenceId)
                            .insertCaptions(lines, replace ? std::nullopt : style, replace);
    const int count = static_cast<int>(lines.size());
    if (m_editor.push(std::move(result))) {
        emit m_editor.message(tr("%n caption line(s) from the speech", nullptr, count), true);
        emit m_editor.libraryRequested(u"captions"_s);
        emit captionsMade();
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

} // namespace velacut::ui
