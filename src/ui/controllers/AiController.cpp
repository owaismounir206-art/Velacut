// SPDX-License-Identifier: GPL-3.0-or-later
#include "AiController.h"

#include "ai/Tasks.h"
#include "core/commands/Edit.h"
#include "core/edit/TimelineEditor.h"
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

bool AiController::autoCaptions(const QString &language)
{
    return startTranscription(language, [this](const QHash<QString, ai::Transcript> &transcripts) { applyCaptions(transcripts); });
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

void AiController::applyCaptions(const QHash<QString, ai::Transcript> &transcripts)
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
        for (captions::CaptionLine &line : ai::captionLines(transcripts.value(item->fingerprint.value), sourceInMs,
                                                            sourceInMs + lengthMs, toTimeline)) {
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

} // namespace vedit::ui
