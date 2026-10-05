// SPDX-License-Identifier: GPL-3.0-or-later
#include "EditorController.h"

#include "ActionRegistry.h"
#include "AiController.h"
#include "CaptionsController.h"
#include "ClipInspector.h"
#include "RecordController.h"
#include "TranscriptController.h"
#include "common/Paths.h"
#include "core/edit/ProjectFormat.h"
#include "core/edit/TimelineEditor.h"
#include "core/project/ProjectMutator.h"
#include "ui/controllers/AiController.h"
#include "core/project/Captions.h"
#include "ai/Script.h"
#include "core/project/ClipTime.h"
#include "core/effects/Easing.h"
#include "core/serialization/ProjectJson.h"
#include "document/Document.h"
#include "engine/analysis/AudioSync.h"
#include "engine/analysis/BeatDetection.h"
#include "engine/analysis/Decoding.h"
#include "engine/analysis/Fingerprint.h"
#include "engine/analysis/MediaAnalysis.h"
#include "engine/analysis/MediaImporter.h"
#include "engine/mlt/MltRuntime.h"
#include "engine/playback/TimelinePlayer.h"
#include "engine/render/RenderJob.h"
#include "fx/Library.h"
#include "ui/models/AudioLibraryModel.h"
#include "ui/models/MediaPoolModel.h"
#include "ui/models/TimelineModel.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QPainter>
#include <QLocale>
#include <QDateTime>
#include <QFileInfo>
#include <QJsonArray>
#include <QLoggingCategory>
#include <QPointer>
#include <QRegularExpression>
#include <QThreadPool>
#include <QUuid>

#include <cmath>
#include <limits>

Q_LOGGING_CATEGORY(lcEditor, "vedit.ui.editor")

using namespace Qt::StringLiterals;

namespace vedit::ui {

namespace {

int evenSize(double value)
{
    return std::max(2, static_cast<int>(std::lround(value / 2.0)) * 2);
}

QString formatName(const Canvas &canvas)
{
    switch (canvas.preset) {
    case CanvasPreset::Landscape16x9:
        return u"16:9"_s;
    case CanvasPreset::Portrait9x16:
        return u"9:16"_s;
    case CanvasPreset::Square1x1:
        return u"1:1"_s;
    case CanvasPreset::Portrait4x5:
        return u"4:5"_s;
    case CanvasPreset::Cinema21x9:
        return u"21:9"_s;
    case CanvasPreset::Portrait3x4:
        return u"3:4"_s;
    case CanvasPreset::Custom:
        break;
    }
    return u"%1×%2"_s.arg(canvas.width).arg(canvas.height);
}

} // namespace

EditorController::EditorController(std::unique_ptr<document::Document> document, engine::MediaAnalysis &analysis,
                                   QString helperExecutable, int previewLimit, QObject *parent)
    : QObject(parent)
    , m_document(std::move(document))
    , m_analysis(analysis)
    , m_player(std::make_unique<engine::TimelinePlayer>())
    , m_importer(std::make_unique<engine::MediaImporter>())
    , m_exportJob(std::make_unique<engine::RenderJob>())
{
    Project &project = m_document->project();
    const SequenceId sequence = project.data().mainSequenceId;
    m_media = std::make_unique<MediaPoolModel>(project);
    m_timeline = std::make_unique<TimelineModel>(project, sequence);
    engine::MltRuntime::waitUntilReady(); // started at launch: normally ready long before a project opens
    m_player->setPreviewLimit(previewLimit);
    m_player->setHelperExecutable(helperExecutable);
    m_player->setSequence(&project, sequence);
    // Back where the user left the project.
    m_player->seek(m_document->uiState().value(u"playhead"_s).toInt());

    m_importer->setExecutable(helperExecutable);
    m_exportJob->setExecutable(helperExecutable);
    connect(m_importer.get(), &engine::MediaImporter::imported, this, &EditorController::onImported);
    connect(m_importer.get(), &engine::MediaImporter::failed, this, [this](const QString &path, const QString &text) {
        m_pendingInserts.removeIf([&path](const PendingInsert &pending) { return pending.path == path; });
        emit message(text, false);
        slideshowFileDone(path, std::nullopt);
        pendingImportDone(path, std::nullopt);
    });
    connect(m_importer.get(), &engine::MediaImporter::busyChanged, this, &EditorController::importingChanged);
    connect(m_exportJob.get(), &engine::RenderJob::finished, this, [this](const QString &path) { emit exportFinished(path); });
    connect(m_exportJob.get(), &engine::RenderJob::failed, this, [this](const QString &text) { emit message(text, false); });

    connect(&project, &Project::changed, this, &EditorController::onProjectChanged);
    connect(m_document.get(), &document::Document::saveStateChanged, this, &EditorController::saveStateChanged);
    QUndoStack &stack = m_document->undoStack();
    connect(&stack, &QUndoStack::canUndoChanged, this, &EditorController::undoChanged);
    connect(&stack, &QUndoStack::canRedoChanged, this, &EditorController::undoChanged);
    connect(&stack, &QUndoStack::undoTextChanged, this, &EditorController::undoChanged);
    connect(&stack, &QUndoStack::redoTextChanged, this, &EditorController::undoChanged);
    connect(m_player.get(), &engine::TimelinePlayer::positionChanged, this, &EditorController::splitAvailableChanged);
    connect(m_player.get(), &engine::TimelinePlayer::smoothFailed, this, [this](const QString &error) { emit message(error, false); });
    m_inspector = new ClipInspector(*this);
    m_captions = new CaptionsController(*this);
    m_ai = new AiController(*this);
    m_transcript = new TranscriptController(*this);
    m_actions = new ActionRegistry(*this);
    m_recorder = std::make_unique<RecordController>(*this);
}

EditorController::~EditorController()
{
    close();
}

bool EditorController::close(QString *error)
{
    if (m_closed) {
        return true;
    }
    m_closed = true;
    // The frame on screen becomes the draft's thumbnail on the home screen; if nothing was shown yet (closed at
    // once), the first frame of the first clip.
    // A cover chosen by the user stays the draft's picture.
    QImage frame = coverPath().isEmpty() ? m_player->sink()->latest() : QImage(coverPath());
    const auto blank = [](const QImage &image) {
        if (image.isNull()) {
            return true;
        }
        for (int i = 0; i < 64; ++i) {
            const QRgb pixel = image.pixel((i % 8) * image.width() / 8, (i / 8) * image.height() / 8);
            if (qRed(pixel) > 16 || qGreen(pixel) > 16 || qBlue(pixel) > 16) {
                return false;
            }
        }
        return true;
    };
    if (blank(frame)) {
        frame = {};
        const Sequence *sequence = data().mainSequence();
        const Track *main = sequence && !sequence->visualTracks.empty() ? &sequence->visualTracks.front() : nullptr;
        const MediaClipData *first = main && !main->clips.empty() ? main->clips.front().media() : nullptr;
        if (const Media *media = first ? data().findMedia(first->mediaId) : nullptr) {
            const QImage strip = m_analysis.thumbnails(*media);
            if (!strip.isNull()) {
                frame = strip.copy(0, 0, strip.width() / engine::MediaAnalysis::thumbnailCount(*media), strip.height());
            }
        }
    }
    if (!frame.isNull()) {
        m_document->setThumbnail(frame.scaled(320, 320, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
    QJsonObject state = m_document->uiState();
    state.insert(u"playhead"_s, m_player->position());
    m_document->setUiState(state);
    m_player->close();
    return m_document->close(error);
}

void EditorController::saveNow()
{
    m_document->saveNow();
}

const ProjectData &EditorController::data() const
{
    return m_document->data();
}

const Media *EditorController::findMedia(const QString &mediaId) const
{
    const std::optional<MediaId> id = MediaId::fromString(mediaId);
    return id ? data().findMedia(*id) : nullptr;
}

QString EditorController::name() const
{
    return data().name;
}

void EditorController::setName(const QString &name)
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty() || trimmed == data().name) {
        emit nameChanged(); // the field goes back to the current name
        return;
    }
    EditResult rename;
    rename.script.push_back(edits::setName(data().name, trimmed));
    rename.text = tr("Rename project");
    apply(std::move(rename), false);
}

int EditorController::saveState() const
{
    switch (m_document->saveState()) {
    case document::Document::SaveState::Saved:
        return Saved;
    case document::Document::SaveState::Saving:
        return Saving;
    case document::Document::SaveState::Failed:
        break;
    }
    return SaveFailed;
}

QString EditorController::saveError() const
{
    return m_document->saveError();
}

bool EditorController::recovered() const
{
    return m_document->recovered();
}

bool EditorController::canUndo() const
{
    return m_document->undoStack().canUndo();
}

bool EditorController::canRedo() const
{
    return m_document->undoStack().canRedo();
}

QString EditorController::undoText() const
{
    return m_document->undoStack().undoText();
}

QString EditorController::redoText() const
{
    return m_document->undoStack().redoText();
}

QStringList EditorController::selection() const
{
    QStringList ids;
    for (const ClipId &id : m_selection) {
        ids << id.toString();
    }
    return ids;
}

bool EditorController::importing() const
{
    return m_importer->busy();
}

int EditorController::canvasPreset() const
{
    const Sequence *sequence = data().mainSequence();
    return sequence ? static_cast<int>(sequence->canvas.preset) : 0;
}

QString EditorController::formatText() const
{
    const Sequence *sequence = data().mainSequence();
    return sequence ? formatName(sequence->canvas) : QString();
}

QSize EditorController::canvasSize() const
{
    const Sequence *sequence = data().mainSequence();
    return sequence ? QSize(sequence->canvas.width, sequence->canvas.height) : QSize();
}

double EditorController::frameRate() const
{
    return data().settings.frameRate.toDouble();
}

int EditorController::playhead() const
{
    return m_player->position();
}

std::vector<ClipId> EditorController::selectedClips() const
{
    std::vector<ClipId> ids(m_selection.begin(), m_selection.end());
    // The focused clip first; the rest in a stable order.
    std::sort(ids.begin(), ids.end(), [this](const ClipId &a, const ClipId &b) {
        if ((a == m_focus) != (b == m_focus)) {
            return a == m_focus;
        }
        return a.toString() < b.toString();
    });
    return ids;
}

std::optional<ClipId> EditorController::focusClip() const
{
    return m_selection.contains(m_focus) ? std::optional<ClipId>(m_focus) : std::nullopt;
}

QString EditorController::selectedTransition() const
{
    return m_transition.isNull() ? QString() : m_transition.toString();
}

QString EditorController::selectedCut() const
{
    return m_cut.isNull() ? QString() : m_cut.toString();
}

std::optional<TransitionId> EditorController::focusTransition() const
{
    return m_transition.isNull() ? std::nullopt : std::optional<TransitionId>(m_transition);
}

std::optional<ClipId> EditorController::transitionTarget() const
{
    const Sequence *sequence = data().mainSequence();
    if (!sequence) {
        return std::nullopt;
    }
    // The first clip of the cut that `clip` makes with its next neighbour, if they touch.
    const auto cutAfter = [](const Track &track, size_t index) -> std::optional<ClipId> {
        if (index + 1 < track.clips.size() && track.clips[index].end() == track.clips[index + 1].start) {
            return track.clips[index].id;
        }
        return std::nullopt;
    };
    for (const Track &track : sequence->visualTracks) {
        for (const Transition &transition : track.transitions) {
            if (transition.id == m_transition) {
                return transition.from;
            }
        }
    }
    if (!m_cut.isNull() && data().findClip(m_cut)) {
        return m_cut;
    }
    if (const std::optional<ClipId> focus = focusClip()) {
        for (const Track &track : sequence->visualTracks) {
            const int index = track.clipIndex(*focus);
            if (index < 0) {
                continue;
            }
            if (const auto after = cutAfter(track, static_cast<size_t>(index))) {
                return after;
            }
            if (index > 0) {
                return cutAfter(track, static_cast<size_t>(index - 1));
            }
            return std::nullopt;
        }
        return std::nullopt;
    }
    // Nothing selected: the cut of the main track nearest to the playhead.
    if (sequence->visualTracks.empty()) {
        return std::nullopt;
    }
    const Track &main = sequence->visualTracks.front();
    std::optional<ClipId> nearest;
    std::int64_t best = std::numeric_limits<std::int64_t>::max();
    for (size_t i = 0; i < main.clips.size(); ++i) {
        if (const auto cut = cutAfter(main, i)) {
            const std::int64_t distance = std::abs(main.clips[i].end().value() - playhead());
            if (distance < best) {
                best = distance;
                nearest = cut;
            }
        }
    }
    return nearest;
}

std::optional<ClipId> EditorController::clipForLibrary() const
{
    if (const std::optional<ClipId> focus = focusClip()) {
        return focus;
    }
    // Here a clip starting at the playhead counts (unlike for splitting).
    const Sequence *sequence = data().mainSequence();
    if (sequence && !sequence->visualTracks.empty()) {
        const RationalTime at(playhead(), data().settings.frameRate);
        for (const Clip &candidate : sequence->visualTracks.front().clips) {
            if (candidate.start <= at && at < candidate.end()) {
                return candidate.id;
            }
        }
    }
    return clipAtPlayhead();
}

bool EditorController::selectClipAtPlayhead()
{
    if (!m_selection.isEmpty()) {
        return true;
    }
    const std::optional<ClipId> clip = clipForLibrary();
    if (!clip) {
        return false;
    }
    setSelection({*clip});
    return true;
}

void EditorController::selectTransition(const QString &transitionId)
{
    const std::optional<TransitionId> id = TransitionId::fromString(transitionId);
    if (!id) {
        return;
    }
    m_selection.clear();
    m_timeline->setSelection(m_selection);
    m_transition = *id;
    m_cut = {};
    emit selectionChanged();
    emit splitAvailableChanged();
}

void EditorController::selectCut(const QString &fromClipId)
{
    const std::optional<ClipId> id = ClipId::fromString(fromClipId);
    if (!id) {
        return;
    }
    m_selection.clear();
    m_timeline->setSelection(m_selection);
    m_transition = {};
    m_cut = *id;
    emit selectionChanged();
    emit splitAvailableChanged();
    emit libraryRequested(u"transitions"_s);
}

bool EditorController::push(EditResult result, MergeKey mergeKey)
{
    if (!result.ok()) {
        emit message(result.error, false);
        return false;
    }
    return m_document->apply(std::move(result), std::move(mergeKey));
}

bool EditorController::apply(EditResult result, bool selectResult)
{
    if (!result.ok()) {
        emit message(result.error, false);
        return false;
    }
    const ClipId primary = result.primaryClip;
    if (!m_document->apply(std::move(result))) {
        return false;
    }
    if (selectResult && !primary.isNull()) {
        setSelection({primary});
    }
    return true;
}

void EditorController::onProjectChanged(const ChangeSet &changes)
{
    if (changes.projectChanged) {
        emit nameChanged();
    }
    if (changes.settingsChanged || changes.sequences.contains(data().mainSequenceId)) {
        emit formatChanged();
        emit magneticMainChanged();
    }
    if (!m_transition.isNull() || !m_cut.isNull()) {
        bool exists = false;
        if (const Sequence *sequence = data().mainSequence()) {
            for (const Track &track : sequence->visualTracks) {
                for (const Transition &transition : track.transitions) {
                    exists = exists || transition.id == m_transition;
                }
            }
        }
        if ((!m_transition.isNull() && !exists) || (!m_cut.isNull() && !data().findClip(m_cut))) {
            m_transition = {};
            m_cut = {};
            emit selectionChanged();
        }
    }
    QSet<ClipId> remaining;
    for (const ClipId &id : std::as_const(m_selection)) {
        if (data().findClip(id)) {
            remaining.insert(id);
        }
    }
    if (remaining != m_selection) {
        setSelection(remaining);
    }
    emit splitAvailableChanged();
    emit modelChanged();
}

void EditorController::setSelection(QSet<ClipId> selection)
{
    const bool otherSelection = !m_transition.isNull() || !m_cut.isNull();
    if (selection == m_selection && !(otherSelection && !selection.isEmpty())) {
        return;
    }
    if (!selection.isEmpty()) {
        m_transition = {};
        m_cut = {};
    }
    m_selection = std::move(selection);
    if (!m_selection.contains(m_focus)) {
        const std::vector<ClipId> ids = selectedClips(); // sorted: the same focus for the same selection
        m_focus = ids.empty() ? ClipId{} : ids.front();
    }
    m_timeline->setSelection(m_selection);
    emit selectionChanged();
    emit splitAvailableChanged();
}

// ---- Media ------------------------------------------------------------------------------------------------------

void EditorController::importFiles(const QList<QUrl> &urls)
{
    QStringList paths;
    for (const QUrl &url : urls) {
        if (url.isLocalFile()) {
            paths << url.toLocalFile();
        }
    }
    importPaths(paths);
}

void EditorController::importPaths(const QStringList &paths)
{
    if (paths.isEmpty()) {
        return;
    }
    ++m_importBatch;
    m_importer->import(paths);
}

void EditorController::importThen(const QStringList &paths, std::function<void(const QHash<QString, MediaId> &)> done)
{
    if (paths.isEmpty()) {
        done({});
        return;
    }
    m_pendingImports.push_back(PendingImport{QSet<QString>(paths.begin(), paths.end()), {}, std::move(done)});
    importPaths(paths);
}

void EditorController::pendingImportDone(const QString &path, const std::optional<MediaId> &media)
{
    for (size_t i = 0; i < m_pendingImports.size(); ++i) {
        PendingImport &pending = m_pendingImports[i];
        if (!pending.waiting.remove(path)) {
            continue;
        }
        if (media) {
            pending.media.insert(path, *media);
        }
        if (pending.waiting.isEmpty()) {
            PendingImport finished = std::move(pending);
            m_pendingImports.erase(m_pendingImports.begin() + static_cast<std::ptrdiff_t>(i));
            finished.done(finished.media);
        }
        return;
    }
}

void EditorController::importAndInsert(const QList<QUrl> &urls, int frame, int trackRow)
{
    QStringList paths;
    for (const QUrl &url : urls) {
        if (url.isLocalFile()) {
            paths << url.toLocalFile();
        }
    }
    importAndInsertPaths(paths, frame, trackRow);
}

void EditorController::importAndInsertPaths(const QStringList &paths, int frame, int trackRow)
{
    if (paths.isEmpty()) {
        return;
    }
    if (m_pendingInserts.isEmpty()) {
        m_insertStart = m_insertCursor = std::max(0, frame);
    }
    for (const QString &path : paths) {
        m_pendingInserts.append(PendingInsert{QFileInfo(path).absoluteFilePath(), trackRow});
    }
    importPaths(paths);
}

void EditorController::onImported(const Media &imported)
{
    // The same file imported again: the media already in the project is used.
    MediaId mediaId = imported.id;
    bool known = false;
    for (const Media &media : data().media) {
        if (media.path == imported.path && media.fingerprint == imported.fingerprint) {
            mediaId = media.id;
            known = true;
            break;
        }
    }
    if (!known) {
        EditResult add;
        add.script.push_back(edits::insertMedia(static_cast<int>(data().media.size()), imported));
        add.text = tr("Import media");
        // All the files of one import are one undo step.
        m_document->apply(std::move(add), MergeKey{u"import"_s, m_importBatch});
    }
    if (m_slideshow && (m_slideshow->photos.contains(imported.path) || m_slideshow->music == imported.path)) {
        slideshowFileDone(imported.path, mediaId);
        return;
    }
    pendingImportDone(imported.path, mediaId);
    const auto pending = std::find_if(m_pendingInserts.begin(), m_pendingInserts.end(),
                                      [&imported](const PendingInsert &p) { return p.path == imported.path; });
    if (pending == m_pendingInserts.end()) {
        return;
    }
    const int row = pending->trackRow;
    const bool asSticker = pending->sticker;
    const bool asWatermark = pending->watermark;
    const ClipId replace = pending->replace;
    m_pendingInserts.erase(pending);
    if (asWatermark) {
        if (imported.kind != MediaKind::Image) {
            emit message(tr("A watermark is a picture (PNG with transparency is best)."), false);
            return;
        }
        const Rational rate = data().settings.frameRate;
        const int length = m_timeline->duration() > 0 ? m_timeline->duration() : static_cast<int>(5 * rate.toDouble());
        StickerClipData sticker;
        sticker.mediaId = mediaId;
        EditResult insert = TimelineEditor(data(), data().mainSequenceId)
                                .insertSticker(RationalTime(0, rate), std::move(sticker), RationalTime(length, rate));
        const ClipId clipId = insert.primaryClip;
        if (apply(std::move(insert))) {
            // Bottom right, small and half-transparent: present, never in the way.
            apply(TimelineEditor(data(), data().mainSequenceId).updateClips({clipId}, [](Clip &clip) {
                clip.transform.position = Param(Vec2{0.40, 0.40});
                clip.transform.scale = Param(Vec2{0.16, 0.16});
                clip.opacity = Param(0.75);
                clip.name = tr("Watermark");
            }, tr("Watermark")));
            emit message(tr("Watermark added over the whole video"), true);
        }
        return;
    }
    if (!replace.isNull() && imported.kind != MediaKind::Audio && data().findClip(replace)) {
        apply(TimelineEditor(data(), data().mainSequenceId).replaceClipMedia(replace, mediaId));
        return;
    }
    if (asSticker) {
        if (imported.kind != MediaKind::Image) {
            emit message(tr("Only pictures can become stickers."), false);
            return;
        }
        StickerClipData sticker;
        sticker.mediaId = mediaId;
        apply(TimelineEditor(data(), data().mainSequenceId)
                  .insertSticker(RationalTime(playhead(), data().settings.frameRate), std::move(sticker)));
        return;
    }
    const bool audio = imported.kind == MediaKind::Audio;
    if (const std::optional<ClipId> clip = insertAtRow(mediaId, audio ? m_insertStart : m_insertCursor, row)) {
        if (!audio) {
            m_insertCursor = static_cast<int>(data().findClip(*clip)->end().value()); // the next file goes after it
        }
    }
}

std::optional<ClipId> EditorController::insertAtRow(const MediaId &mediaId, int frame, int trackRow)
{
    const Media *media = data().findMedia(mediaId);
    if (!media) {
        return std::nullopt;
    }
    Placement placement = Placement::Auto;
    if (media->kind != MediaKind::Audio) {
        const std::optional<TimelineModel::TrackRow> row = m_timeline->trackRow(trackRow);
        if (trackRow < 0 || (row && !row->audio && row->index > 0)) {
            placement = Placement::Overlay;
        }
    }
    const RationalTime position(std::max(0, frame), data().settings.frameRate);
    EditResult insert = insertMediaAdoptingFormat(data(), data().mainSequenceId, mediaId, position, placement);
    const ClipId clip = insert.primaryClip;
    if (!apply(std::move(insert))) {
        return std::nullopt;
    }
    return clip;
}

std::vector<ClipId> EditorController::placeholders() const
{
    std::vector<ClipId> list;
    const Sequence *sequence = data().mainSequence();
    if (!sequence) {
        return list;
    }
    // Main track first, then the overlays; each in time order.
    for (const Track &track : sequence->visualTracks) {
        for (const Clip &clip : track.clips) {
            if (clip.placeholder) {
                list.push_back(clip.id);
            }
        }
    }
    return list;
}

int EditorController::placeholderCount() const
{
    return static_cast<int>(placeholders().size());
}

QString EditorController::firstPlaceholder() const
{
    const std::vector<ClipId> list = placeholders();
    return list.empty() ? QString() : list.front().toString();
}

bool EditorController::replaceClip(const QString &clipId, const QString &mediaId)
{
    const std::optional<ClipId> clip = ClipId::fromString(clipId);
    const std::optional<MediaId> media = MediaId::fromString(mediaId);
    if (!clip || !media) {
        return false;
    }
    return apply(TimelineEditor(data(), data().mainSequenceId).replaceClipMedia(*clip, *media));
}

void EditorController::replaceClipWithFile(const QString &clipId, const QUrl &file)
{
    const std::optional<ClipId> clip = ClipId::fromString(clipId);
    if (!clip || !file.isLocalFile()) {
        return;
    }
    const QString path = QFileInfo(file.toLocalFile()).absoluteFilePath();
    PendingInsert pending{path, 0};
    pending.replace = *clip;
    m_pendingInserts.append(pending);
    importPaths({path});
}

void EditorController::fillPlaceholders(const QList<QUrl> &files)
{
    static const QStringList audioSuffixes{u"mp3"_s, u"wav"_s, u"flac"_s, u"aac"_s, u"ogg"_s, u"opus"_s, u"m4a"_s, u"wma"_s};
    const std::vector<ClipId> targets = placeholders();
    QStringList music;
    QStringList rest;
    QList<PendingInsert> replacements;
    size_t next = 0;
    for (const QUrl &url : files) {
        if (!url.isLocalFile()) {
            continue;
        }
        const QString path = QFileInfo(url.toLocalFile()).absoluteFilePath();
        if (audioSuffixes.contains(QFileInfo(path).suffix().toLower())) {
            music << path; // never a slot: music goes under the video, from its start
        } else if (next < targets.size()) {
            PendingInsert pending{path, 0};
            pending.replace = targets[next++];
            replacements.append(pending);
        } else {
            rest << path;
        }
    }
    // Leftover videos after the end, music from the start (insert positions are set by the first batch).
    if (!rest.isEmpty() || !music.isEmpty()) {
        importAndInsertPaths(rest + music, m_timeline->duration(), m_timeline->mainRow());
        if (!music.isEmpty()) {
            m_insertStart = 0;
        }
    }
    QStringList paths;
    for (const PendingInsert &pending : std::as_const(replacements)) {
        m_pendingInserts.append(pending);
        paths << pending.path;
    }
    if (!paths.isEmpty()) {
        importPaths(paths);
    }
}

void EditorController::buildSlideshow(const QList<QUrl> &photos, const QUrl &music, int style, bool onBeat)
{
    PendingSlideshow slideshow;
    for (const QUrl &url : photos) {
        if (url.isLocalFile()) {
            slideshow.photos << QFileInfo(url.toLocalFile()).absoluteFilePath();
        }
    }
    if (slideshow.photos.isEmpty()) {
        emit message(tr("Choose at least one photo for the slideshow."), false);
        return;
    }
    slideshow.music = music.isLocalFile() ? QFileInfo(music.toLocalFile()).absoluteFilePath() : QString();
    slideshow.style = std::clamp(style, 0, 3);
    slideshow.onBeat = onBeat && !slideshow.music.isEmpty();
    QStringList files = slideshow.photos;
    if (!slideshow.music.isEmpty()) {
        files << slideshow.music;
    }
    m_slideshow = std::move(slideshow);
    emit slideshowChanged();
    emit message(tr("Preparing the slideshow…"), false);
    importPaths(files);
}

void EditorController::slideshowFileDone(const QString &path, const std::optional<MediaId> &media)
{
    if (!m_slideshow || (!m_slideshow->photos.contains(path) && m_slideshow->music != path)) {
        return;
    }
    if (media) {
        m_slideshow->imported.insert(path, *media);
    } else {
        m_slideshow->failed.insert(path);
    }
    QStringList all = m_slideshow->photos;
    if (!m_slideshow->music.isEmpty()) {
        all << m_slideshow->music;
    }
    for (const QString &file : std::as_const(all)) {
        if (!m_slideshow->imported.contains(file) && !m_slideshow->failed.contains(file)) {
            return; // still importing
        }
    }
    const MediaId musicId = m_slideshow->imported.value(m_slideshow->music);
    const Media *song = musicId.isNull() ? nullptr : data().findMedia(musicId);
    if (!m_slideshow->onBeat || !song) {
        finishSlideshow({});
        return;
    }
    // The beats of the song in background (decoding a whole song never runs on the interface thread).
    QPointer<EditorController> self(this);
    QThreadPool::globalInstance()->start([self, media = *song] {
        const std::optional<engine::Spectrum> spectrum = engine::cachedSpectrum(media);
        const std::vector<double> beats = spectrum ? engine::detectBeats(*spectrum) : std::vector<double>{};
        QMetaObject::invokeMethod(qApp, [self, beats] {
            if (self) {
                self->finishSlideshow(beats);
            }
        });
    });
}

void EditorController::finishSlideshow(const std::vector<double> &beats)
{
    if (!m_slideshow) {
        return;
    }
    const PendingSlideshow slideshow = std::move(*m_slideshow);
    m_slideshow.reset();
    struct Style
    {
        double seconds;
        const char *transition;
        double transitionSeconds;
        const char *filter;
    };
    static constexpr Style styles[] = {{3.0, "transitions/dissolve", 0.8, ""},
                                       {2.0, "transitions/push-left", 0.4, "filters/vivid"},
                                       {3.5, "transitions/dissolve", 1.0, "filters/film"},
                                       {3.0, "transitions/dip-to-black", 0.6, "filters/teal-orange"}};
    const Style &look = styles[slideshow.style];
    const Rational rate = data().settings.frameRate;
    const auto seconds = [](double value, const Rational &r) {
        return RationalTime::fromSeconds(Rational(static_cast<qint64>(std::llround(value * 1000)), 1000), r, Rounding::NearestEven);
    };
    const auto build = [this](EditResult result) { return m_document->apply(std::move(result)); };

    std::vector<MediaId> photos;
    for (const QString &path : slideshow.photos) {
        const MediaId id = slideshow.imported.value(path);
        const Media *media = id.isNull() ? nullptr : data().findMedia(id);
        if (media && media->kind != MediaKind::Audio) {
            photos.push_back(id);
        }
    }
    if (photos.empty()) {
        emit slideshowChanged();
        emit message(tr("None of the chosen files is a photo or a video."), false);
        return;
    }
    // When each photo ends: every `seconds`, or on the beat nearest to that (never much shorter or longer).
    std::vector<double> ends;
    double t = 0;
    for (size_t i = 0; i < photos.size(); ++i) {
        double end = t + look.seconds;
        double best = -1;
        for (const double beat : beats) {
            if (beat >= t + look.seconds * 0.6 && beat <= t + look.seconds * 1.6
                && (best < 0 || std::abs(beat - end) < std::abs(best - end))) {
                best = beat;
            }
        }
        end = best > 0 ? best : end;
        ends.push_back(end);
        t = end;
    }

    // The photos in order (the first one sets the format of the video, SPEC 0bis rule 1).
    static const QString moves[] = {u"animations/loop/ken_burns"_s, u"animations/loop/ken_burns_out"_s,
                                    u"animations/loop/ken_burns_left"_s, u"animations/loop/ken_burns_right"_s};
    double start = 0;
    for (size_t i = 0; i < photos.size(); ++i) {
        const RationalTime at = seconds(start, rate);
        const RationalTime length = std::max(RationalTime(1, data().settings.frameRate), seconds(ends[i] - start, data().settings.frameRate));
        EditResult insert = i == 0 ? insertMediaAdoptingFormat(data(), data().mainSequenceId, photos[i], at)
                                   : TimelineEditor(data(), data().mainSequenceId)
                                         .insertMedia(photos[i], at, TimeRange{RationalTime(0, data().settings.frameRate), length});
        const ClipId clipId = insert.primaryClip;
        if (!build(std::move(insert))) {
            continue;
        }
        if (i == 0) { // inserted at the default length of a photo
            build(TimelineEditor(data(), data().mainSequenceId).trimClip(clipId, ClipEdge::End, at + length));
        }
        const QString move = moves[i % 4];
        build(TimelineEditor(data(), data().mainSequenceId).updateClips({clipId}, [&move](Clip &clip) {
            clip.transform.fit = FitMode::Cover;
            ClipAnimation animation = TimelineEditor::kenBurns();
            animation.type.id = move;
            clip.animations.loop = animation;
        }, tr("Slideshow")));
        start = ends[i];
    }
    const Sequence &sequence = *data().mainSequence();
    const Track &main = sequence.visualTracks.front();
    std::vector<ClipId> clips;
    for (const Clip &clip : main.clips) {
        clips.push_back(clip.id);
    }
    if (const fx::FilterPreset *filter = fx::Library::core().filter(QString::fromLatin1(look.filter))) {
        const AssetRef ref{QString::fromLatin1(fx::Library::kCorePack), filter->id, filter->version};
        build(TimelineEditor(data(), data().mainSequenceId).updateClips(clips, [&ref](Clip &clip) {
            Effect effect;
            effect.id = EffectId::create();
            effect.type = u"vedit.filter"_s;
            effect.preset = ref;
            clip.effects.insert(clip.effects.begin(), std::move(effect));
        }, tr("Apply filter")));
    }
    if (const fx::TransitionPreset *transition = fx::Library::core().transition(QString::fromLatin1(look.transition));
        transition && clips.size() > 1) {
        build(TimelineEditor(data(), data().mainSequenceId)
                  .applyTransitionToAll(main.id, AssetRef{QString::fromLatin1(fx::Library::kCorePack), transition->id, transition->version},
                                        seconds(look.transitionSeconds, rate)));
    }
    // The music under the photos, as long as them, ending with a fade.
    const MediaId song = slideshow.imported.value(slideshow.music);
    if (!song.isNull()) {
        EditResult insert = TimelineEditor(data(), data().mainSequenceId).insertMedia(song, RationalTime(0, rate));
        const ClipId musicClip = insert.primaryClip;
        if (build(std::move(insert))) {
            const RationalTime videoEnd = data().mainSequence()->visualTracks.front().clips.back().end();
            const Clip *clip = data().findClip(musicClip);
            if (clip && videoEnd < clip->end()) {
                build(TimelineEditor(data(), data().mainSequenceId).trimClip(musicClip, ClipEdge::End, videoEnd));
            }
            build(TimelineEditor(data(), data().mainSequenceId).updateClips({musicClip}, [&](Clip &c) {
                if (MediaClipData *media = std::get_if<MediaClipData>(&c.payload)) {
                    media->audio.fadeOut = std::min(seconds(2.0, rate), c.duration);
                }
            }, tr("Fades")));
        }
    }
    m_document->undoStack().clear(); // the slideshow is where the project starts
    m_player->seek(0);
    emit slideshowChanged();
    emit message(tr("Slideshow ready: %n photo(s). Change anything you like.", nullptr, static_cast<int>(photos.size())), false);
}

namespace {
TextClipData textInStyle(const QString &styleId, const QString &fallbackText); // below, with the texts
} // namespace

void EditorController::buildMontage(const QList<QUrl> &files, const QUrl &music, const QString &style, int seconds)
{
    QStringList paths;
    for (const QUrl &url : files) {
        if (url.isLocalFile()) {
            paths << QFileInfo(url.toLocalFile()).absoluteFilePath();
        }
    }
    if (paths.isEmpty()) {
        emit message(tr("Choose some videos or photos for the montage."), false);
        return;
    }
    const QString song = music.isLocalFile() ? QFileInfo(music.toLocalFile()).absoluteFilePath() : QString();
    MontageState state;
    state.style = ai::montageStyle(style) ? style : u"vlog"_s;
    state.seconds = std::max(0, seconds);
    m_montage = std::move(state);
    emit montageChanged();
    emit message(tr("Making the montage: looking for the best moments…"), false);
    QStringList all = paths;
    if (!song.isEmpty()) {
        all << song;
    }
    importThen(all, [this, paths, song](const QHash<QString, MediaId> &imported) {
        if (!m_montage) {
            return;
        }
        struct Job
        {
            MediaId id;
            QString path;
            bool photo;
            double seconds;
        };
        std::vector<Job> jobs;
        for (const QString &path : paths) {
            const MediaId id = imported.value(path);
            const Media *media = id.isNull() ? nullptr : data().findMedia(id);
            if (!media || media->kind == MediaKind::Audio) {
                continue;
            }
            jobs.push_back({id, media->path, media->kind == MediaKind::Image,
                            media->info.duration ? media->info.duration->toSecondsDouble() : 0.0});
        }
        if (jobs.empty()) {
            m_montage.reset();
            emit montageChanged();
            emit message(tr("None of the chosen files is a photo or a video."), false);
            return;
        }
        m_montage->music = imported.value(song);
        const Media *songMedia = m_montage->music.isNull() ? nullptr : data().findMedia(m_montage->music);
        // The moments of every video and the beats of the song, in background.
        QPointer<EditorController> self(this);
        QThreadPool::globalInstance()->start([self, jobs, songMedia = songMedia ? std::optional<Media>(*songMedia) : std::nullopt] {
            std::vector<ai::MontageSource> sources;
            for (const Job &job : jobs) {
                ai::MontageSource source;
                source.photo = job.photo;
                source.seconds = job.seconds;
                if (!job.photo) {
                    double measured = 0.0;
                    if (auto samples = engine::extractShotSamples(job.path, 4, &measured)) {
                        source.samples = std::move(*samples);
                    }
                    if (source.seconds <= 0.0) {
                        source.seconds = measured;
                    }
                }
                sources.push_back(std::move(source));
            }
            std::vector<double> beats;
            if (songMedia) {
                if (const std::optional<engine::Spectrum> spectrum = engine::cachedSpectrum(*songMedia)) {
                    beats = engine::detectBeats(*spectrum);
                }
            }
            QMetaObject::invokeMethod(qApp, [self, jobs, sources = std::move(sources), beats = std::move(beats)] {
                if (!self || !self->m_montage) {
                    return;
                }
                for (const Job &job : jobs) {
                    self->m_montage->media.push_back(job.id);
                }
                self->m_montage->sources = sources;
                self->m_montage->beats = beats;
                self->layMontage(true);
            });
        });
    });
}

void EditorController::layMontage(bool initial)
{
    if (!m_montage || m_montage->sources.empty()) {
        return;
    }
    const MontageState &state = *m_montage;
    const ai::MontageStyle &style = *ai::montageStyle(state.style);
    const std::vector<ai::MontagePiece> plan = ai::planMontage(state.sources, state.beats, style, state.seconds, state.seed);
    const MergeKey step{u"montage"_s, static_cast<quint64>(QDateTime::currentMSecsSinceEpoch())};
    // A first montage starts the project (no undo below it); a shuffle is one undo step.
    const auto build = [this, initial, &step](EditResult result) {
        if (!result.ok()) {
            return false;
        }
        return initial ? m_document->apply(std::move(result)) : m_document->apply(std::move(result), step);
    };
    if (!initial) {
        std::vector<ClipId> everything;
        const Sequence &sequence = *data().mainSequence();
        for (const auto *tracks : {&sequence.visualTracks, &sequence.audioTracks}) {
            for (const Track &track : *tracks) {
                for (const Clip &clip : track.clips) {
                    everything.push_back(clip.id);
                }
            }
        }
        build(TimelineEditor(data(), data().mainSequenceId).deleteClips(everything));
    }
    const auto at = [](double seconds, const Rational &rate) {
        return RationalTime::fromSeconds(Rational(static_cast<qint64>(std::llround(seconds * 1000)), 1000), rate, Rounding::NearestEven);
    };
    static const QString moves[] = {u"animations/loop/ken_burns"_s, u"animations/loop/ken_burns_out"_s,
                                    u"animations/loop/ken_burns_left"_s, u"animations/loop/ken_burns_right"_s};
    double time = 0.0;
    int photos = 0;
    for (const ai::MontagePiece &piece : plan) {
        const MediaId media = state.media[static_cast<size_t>(piece.source)];
        const Rational rate = data().settings.frameRate;
        const TimeRange range{at(piece.from, rate), at(piece.length, rate)};
        EditResult insert = insertMediaAdoptingFormat(data(), data().mainSequenceId, media, at(time, rate), Placement::Auto, range);
        const ClipId clipId = insert.primaryClip;
        if (!build(std::move(insert))) {
            continue;
        }
        const bool photo = state.sources[static_cast<size_t>(piece.source)].photo;
        const QString move = moves[photos % 4];
        photos += photo ? 1 : 0;
        build(TimelineEditor(data(), data().mainSequenceId).updateClips({clipId}, [photo, &move](Clip &clip) {
            clip.transform.fit = FitMode::Cover; // every shot fills the picture
            if (photo) {
                ClipAnimation animation = TimelineEditor::kenBurns();
                animation.type.id = move;
                clip.animations.loop = animation;
            }
        }, tr("Automatic montage")));
        time += piece.length;
    }
    const Rational rate = data().settings.frameRate;
    const Track &main = data().mainSequence()->visualTracks.front();
    std::vector<ClipId> clips;
    for (const Clip &clip : main.clips) {
        clips.push_back(clip.id);
    }
    if (const fx::FilterPreset *filter = style.filter.isEmpty() ? nullptr : fx::Library::core().filter(style.filter)) {
        const AssetRef ref{QString::fromLatin1(fx::Library::kCorePack), filter->id, filter->version};
        build(TimelineEditor(data(), data().mainSequenceId).updateClips(clips, [&ref](Clip &clip) {
            Effect effect;
            effect.id = EffectId::create();
            effect.type = u"vedit.filter"_s;
            effect.preset = ref;
            clip.effects.insert(clip.effects.begin(), std::move(effect));
        }, tr("Apply filter")));
    }
    if (const fx::TransitionPreset *transition = style.transition.isEmpty() ? nullptr : fx::Library::core().transition(style.transition);
        transition && clips.size() > 1) {
        build(TimelineEditor(data(), data().mainSequenceId)
                  .applyTransitionToAll(main.id, AssetRef{QString::fromLatin1(fx::Library::kCorePack), transition->id, transition->version},
                                        at(style.transitionSeconds, rate)));
    }
    // The title, in the style's look, at the start.
    TextClipData title = textInStyle(style.titleStyle, name());
    title.text = name();
    build(TimelineEditor(data(), data().mainSequenceId).insertText(RationalTime(0, rate), std::move(title), at(std::min(2.5, std::max(1.0, time)), rate)));
    // The music under it all, as long as the montage, ending with a fade.
    if (!state.music.isNull()) {
        EditResult insert = TimelineEditor(data(), data().mainSequenceId).insertMedia(state.music, RationalTime(0, rate));
        const ClipId musicClip = insert.primaryClip;
        if (build(std::move(insert))) {
            const RationalTime videoEnd = data().mainSequence()->visualTracks.front().clips.back().end();
            const Clip *clip = data().findClip(musicClip);
            if (clip && videoEnd < clip->end()) {
                build(TimelineEditor(data(), data().mainSequenceId).trimClip(musicClip, ClipEdge::End, videoEnd));
            }
            build(TimelineEditor(data(), data().mainSequenceId).updateClips({musicClip}, [&](Clip &c) {
                if (MediaClipData *media = std::get_if<MediaClipData>(&c.payload)) {
                    media->audio.fadeOut = std::min(at(1.5, rate), c.duration);
                }
            }, tr("Fades")));
        }
    }
    if (initial) {
        m_document->undoStack().clear();
    }
    m_montage->building = false;
    m_player->seek(0);
    emit montageChanged();
    emit message(initial ? tr("Montage ready: %n shot(s) on the beat. Change anything you like, or shuffle.", nullptr,
                              static_cast<int>(plan.size()))
                         : tr("Another montage: %n shot(s)", nullptr, static_cast<int>(plan.size())),
                 !initial);
}

void EditorController::buildFromScript(const QString &script, int preset, const QUrl &music)
{
    const QStringList scenes = ai::splitScript(script);
    if (scenes.isEmpty()) {
        emit message(tr("Write or paste the script first."), false);
        return;
    }
    m_buildingScript = true;
    emit scriptVideoChanged();
    setCanvasPreset(preset);
    const QString song = music.isLocalFile() ? QFileInfo(music.toLocalFile()).absoluteFilePath() : QString();
    // The voice of each scene (when a Piper voice is installed), then everything in the project.
    const auto place = [this, scenes, song](const QStringList &voices) {
        QStringList files;
        for (const QString &voice : voices) {
            if (!voice.isEmpty()) {
                files << voice;
            }
        }
        if (!song.isEmpty()) {
            files << song;
        }
        importThen(files, [this, scenes, voices, song](const QHash<QString, MediaId> &media) { layScript(scenes, voices, media, song); });
    };
    if (!m_ai->synthesizeAll(scenes, place)) {
        place({});
    }
}

void EditorController::layScript(const QStringList &scenes, const QStringList &voices, const QHash<QString, MediaId> &media,
                                 const QString &music)
{
    const Rational rate = data().settings.frameRate;
    const auto at = [&rate](double seconds) {
        return RationalTime::fromSeconds(Rational(static_cast<qint64>(std::llround(seconds * 1000)), 1000), rate, Rounding::NearestEven);
    };
    const auto build = [this](EditResult result) { return result.ok() && m_document->apply(std::move(result)); };
    std::vector<captions::CaptionLine> lines;
    double time = 0.0;
    for (qsizetype i = 0; i < scenes.size(); ++i) {
        const QString voice = i < voices.size() ? voices[i] : QString();
        const Media *speech = voice.isEmpty() ? nullptr : data().findMedia(media.value(voice));
        const double spoken = speech && speech->info.duration ? speech->info.duration->toSecondsDouble() : 0.0;
        const double length = spoken > 0.0 ? spoken + 0.4 : ai::readingSeconds(scenes[i]);
        // A slot for the user's shot, named after what the scene says.
        const QStringList words = captions::splitWords(scenes[i]);
        const QString label = tr("Scene %1: %2").arg(i + 1).arg(words.mid(0, 4).join(u' ') + (words.size() > 4 ? u"…"_s : QString()));
        build(TimelineEditor(data(), data().mainSequenceId).insertPlaceholder(Placeholder{label, PlaceholderKind::Any}, at(length)));
        if (speech) {
            build(TimelineEditor(data(), data().mainSequenceId).insertMedia(speech->id, at(time)));
        }
        lines.push_back(captions::CaptionLine{at(time), at(time + (spoken > 0.0 ? spoken : length - 0.2)), scenes[i], {}});
        time += length;
    }
    // The text as animated captions, word by word.
    std::optional<CaptionStyle> style;
    if (const fx::CaptionStylePreset *preset = fx::Library::core().captionStyle(u"captions/pop-three"_s)) {
        style = projectjson::captionStyleFromJson(preset->style);
        style->preset = preset->id;
    }
    build(TimelineEditor(data(), data().mainSequenceId).insertCaptions(lines, style));
    // The music under it, lowered while the voice speaks, ending with a fade.
    const MediaId song = media.value(music);
    if (!song.isNull()) {
        EditResult insert = TimelineEditor(data(), data().mainSequenceId).insertMedia(song, RationalTime(0, rate));
        const ClipId musicClip = insert.primaryClip;
        if (build(std::move(insert))) {
            const Clip *clip = data().findClip(musicClip);
            if (clip && at(time) < clip->end()) {
                build(TimelineEditor(data(), data().mainSequenceId).trimClip(musicClip, ClipEdge::End, at(time)));
            }
            build(TimelineEditor(data(), data().mainSequenceId).updateClips({musicClip}, [&](Clip &c) {
                if (MediaClipData *data = std::get_if<MediaClipData>(&c.payload)) {
                    data->audio.fadeOut = std::min(at(2.0), c.duration);
                    data->audio.gainDb = Param(-6.0);
                }
            }, tr("Music")));
            if (std::any_of(voices.begin(), voices.end(), [](const QString &v) { return !v.isEmpty(); })) {
                setSelection({musicClip});
                m_inspector->autoDuck(-12.0);
                clearSelection();
            }
        }
    }
    m_document->undoStack().clear(); // the script is where the project starts
    m_buildingScript = false;
    m_player->seek(0);
    emit scriptVideoChanged();
    emit message(tr("%n scene(s) ready: put your videos in the grey slots (Choose), change anything you like.", nullptr,
                    static_cast<int>(scenes.size())),
                 false);
}

int EditorController::makeShortClipDrafts(const ClipId &clipId, const std::vector<ai::Span> &spans, const ai::Transcript *transcript)
{
    const Clip *clip = data().findClip(clipId);
    const MediaClipData *media = clip ? clip->media() : nullptr;
    const Media *item = media ? data().findMedia(media->mediaId) : nullptr;
    const std::optional<Canvas> canvas = canvasFor(static_cast<int>(CanvasPreset::Portrait9x16));
    if (!item || !canvas || !m_draftMaker) {
        return 0;
    }
    const Rational rate = data().settings.frameRate;
    const auto at = [&rate](double seconds) {
        return RationalTime::fromSeconds(Rational(static_cast<qint64>(std::llround(seconds * 1000)), 1000), rate, Rounding::NearestEven);
    };
    std::optional<CaptionStyle> style;
    if (const fx::CaptionStylePreset *preset = fx::Library::core().captionStyle(u"captions/pop-three"_s)) {
        style = projectjson::captionStyleFromJson(preset->style);
        style->preset = preset->id;
    }
    std::vector<ProjectData> drafts;
    int index = 0;
    for (const ai::Span &span : spans) {
        ++index;
        ProjectData base = ProjectData::createEmpty(tr("%1 — clip %2").arg(name()).arg(index));
        base.settings = data().settings;
        base.media = {*item};
        base.sequences.front().canvas = *canvas;
        Project project(std::move(base));
        const auto apply = [&project](EditResult result) {
            if (!result.ok()) {
                return false;
            }
            ProjectMutator mutator(project);
            for (const auto &edit : result.script) {
                edit->apply(mutator);
            }
            return true;
        };
        const SequenceId sequenceId = project.data().mainSequenceId;
        const double fileFrom = media->sourceIn.toSecondsDouble() + span.from;
        EditResult insert = TimelineEditor(project.data(), sequenceId)
                                .insertMedia(item->id, RationalTime(0, rate), TimeRange{at(fileFrom), at(span.length())});
        const ClipId shot = insert.primaryClip;
        if (!apply(std::move(insert))) {
            continue;
        }
        apply(TimelineEditor(project.data(), sequenceId).updateClips({shot}, [](Clip &c) { c.transform.fit = FitMode::Cover; },
                                                                     tr("Fill the picture")));
        // A title: the first words said, or the clip's number.
        QString title = tr("Part %1").arg(index);
        if (transcript) {
            QStringList words;
            for (const ai::Transcript::Word &word : transcript->words) {
                if (word.from / 1000.0 >= fileFrom && words.size() < 5) {
                    words << word.text;
                }
            }
            if (!words.isEmpty()) {
                title = words.join(u' ');
            }
            const auto toTimeline = [fileFrom](std::int64_t ms) {
                return RationalTime(std::max<std::int64_t>(0, ms - std::llround(fileFrom * 1000.0)), Rational(1000));
            };
            const auto lines = ai::captionLines(*transcript, std::llround(fileFrom * 1000.0),
                                                std::llround((fileFrom + span.length()) * 1000.0), toTimeline);
            if (!lines.empty()) {
                apply(TimelineEditor(project.data(), sequenceId).insertCaptions(lines, style));
            }
        }
        TextClipData text = textInStyle(u"text/anim-pop-in-bold"_s, title);
        text.text = title;
        apply(TimelineEditor(project.data(), sequenceId).insertText(RationalTime(0, rate), std::move(text), at(2.0)));
        drafts.push_back(project.data());
    }
    return m_draftMaker(drafts);
}

bool EditorController::shuffleMontage()
{
    if (!canShuffleMontage()) {
        return false;
    }
    ++m_montage->seed;
    layMontage(false);
    return true;
}

void EditorController::dismissMontage()
{
    if (m_montage && !m_montage->building) {
        m_montage.reset();
        emit montageChanged();
    }
}

bool EditorController::addMedia(const QString &mediaId)
{
    const std::optional<MediaId> id = MediaId::fromString(mediaId);
    if (!id) {
        return false;
    }
    // A template slot is selected: "+" fills it, and the next empty slot becomes the selection.
    const std::optional<ClipId> focus = focusClip();
    const Clip *clip = focus ? data().findClip(*focus) : nullptr;
    const Media *media = data().findMedia(*id);
    if (clip && clip->placeholder && media && media->kind != MediaKind::Audio) {
        if (!apply(TimelineEditor(data(), data().mainSequenceId).replaceClipMedia(*focus, *id))) {
            return false;
        }
        const std::vector<ClipId> left = placeholders();
        if (!left.empty()) {
            setSelection({left.front()});
        }
        return true;
    }
    return insertAtRow(*id, playhead(), m_timeline->mainRow()).has_value();
}

bool EditorController::insertMedia(const QString &mediaId, int frame, int trackRow)
{
    const std::optional<MediaId> id = MediaId::fromString(mediaId);
    return id && insertAtRow(*id, frame, trackRow);
}

bool EditorController::addFromLibrary(AudioLibraryModel *library, int row)
{
    const std::optional<Media> media = library ? library->media(row) : std::nullopt;
    if (!media) {
        return false;
    }
    MediaId mediaId = media->id;
    const Media *existing = nullptr;
    for (const Media &item : data().media) {
        if (item.path == media->path && item.fingerprint == media->fingerprint) {
            existing = &item;
        }
    }
    if (existing) {
        mediaId = existing->id;
    } else {
        Media added = *media;
        added.id = MediaId::create(); // the library keeps its own copy
        mediaId = added.id;
        EditResult add;
        add.script.push_back(edits::insertMedia(static_cast<int>(data().media.size()), added));
        add.text = tr("Add music");
        if (!m_document->apply(std::move(add), MergeKey{u"library"_s, ++m_importBatch})) {
            return false;
        }
    }
    // Merged with the import: one undo removes both.
    EditResult insert = TimelineEditor(data(), data().mainSequenceId)
                            .insertMedia(mediaId, RationalTime(playhead(), data().settings.frameRate));
    if (!insert.ok()) {
        emit message(insert.error, false);
        return false;
    }
    const ClipId clip = insert.primaryClip;
    insert.text = tr("Add music");
    m_document->apply(std::move(insert), MergeKey{u"library"_s, m_importBatch});
    setSelection({clip});
    return true;
}

namespace {

// A text in a style of the library (its sample text and animation too), or the default style.
TextClipData textInStyle(const QString &styleId, const QString &fallbackText)
{
    TextClipData text;
    text.text = fallbackText;
    text.style = ClipInspector::defaultTextStyle();
    if (const fx::TextStylePreset *preset = styleId.isEmpty() ? nullptr : fx::Library::core().textStyle(styleId)) {
        text.style = projectjson::textStyleFromJson(preset->style);
        text.stylePreset = AssetRef{QString::fromLatin1(fx::Library::kCorePack), preset->id, preset->version};
        if (!preset->sampleText.text().isEmpty()) {
            text.text = preset->sampleText.text();
        }
        if (!preset->animation.isEmpty()) {
            text.animation = projectjson::textAnimationFromJson(preset->animation);
        }
    }
    return text;
}

// {"en": "…", "it": "…"} in the language of the interface, or a plain string.
QString localized(const QJsonValue &value)
{
    if (value.isString()) {
        return value.toString();
    }
    const QJsonObject object = value.toObject();
    const QString language = QLocale().language() == QLocale::Italian ? u"it"_s : u"en"_s;
    return object.value(language).toString(object.value(u"en"_s).toString());
}

} // namespace

bool EditorController::addText(const QString &styleId)
{
    TextClipData text = textInStyle(styleId, tr("Your text"));
    const Rational rate = data().settings.frameRate;
    return apply(TimelineEditor(data(), data().mainSequenceId)
                     .insertText(RationalTime(playhead(), rate), std::move(text), RationalTime(0, rate))); // default length
}

bool EditorController::applyTemplate(const QJsonObject &spec, const QString &name)
{
    const QJsonArray shots = spec.value(u"slots"_s).toArray();
    if (shots.isEmpty() || !data().mainSequence()) {
        return false;
    }
    const Rational rate = data().settings.frameRate;
    const auto seconds = [&rate](double value) {
        return RationalTime::fromSeconds(Rational(static_cast<qint64>(std::llround(value * 1000)), 1000), rate,
                                         Rounding::NearestEven);
    };
    const auto build = [this](EditResult result) { return m_document->apply(std::move(result)); };

    // The format first (the shots are placed in it), then the shots.
    static const QStringList formats{u"16:9"_s, u"9:16"_s, u"1:1"_s, u"4:5"_s, u"21:9"_s, u"3:4"_s};
    const qsizetype format = formats.indexOf(spec.value(u"canvas"_s).toString(u"16:9"_s));
    setCanvasPreset(static_cast<int>(std::max<qsizetype>(0, format)));
    for (const QJsonValue &value : shots) {
        const QJsonObject shot = value.toObject();
        Placeholder placeholder;
        placeholder.label = localized(shot.value(u"label"_s));
        const QString kind = shot.value(u"kind"_s).toString();
        placeholder.kind = kind == u"video"_s ? PlaceholderKind::Video
                         : kind == u"photo"_s ? PlaceholderKind::Photo : PlaceholderKind::Any;
        if (!build(TimelineEditor(data(), data().mainSequenceId)
                       .insertPlaceholder(placeholder, seconds(std::max(0.5, shot.value(u"seconds"_s).toDouble(3.0)))))) {
            return false;
        }
    }
    const Sequence &sequence = *data().mainSequence();
    const Track &main = sequence.visualTracks.front();
    std::vector<ClipId> slotClips;
    for (const Clip &clip : main.clips) {
        slotClips.push_back(clip.id);
    }
    const RationalTime end = main.clips.empty() ? RationalTime(0, rate) : main.clips.back().end();

    // The look of every shot, which stays when the media replace the placeholders: the shot fills the frame (a
    // template is designed for its format: no black bars) and gets the template's filter.
    const fx::FilterPreset *filter = fx::Library::core().filter(spec.value(u"filter"_s).toString());
    const std::optional<AssetRef> filterRef = filter ? std::optional(AssetRef{QString::fromLatin1(fx::Library::kCorePack),
                                                                             filter->id, filter->version})
                                                     : std::nullopt;
    build(TimelineEditor(data(), data().mainSequenceId).updateClips(slotClips, [&filterRef](Clip &clip) {
        clip.transform.fit = FitMode::Cover;
        if (filterRef) {
            Effect effect;
            effect.id = EffectId::create();
            effect.type = u"vedit.filter"_s;
            effect.preset = *filterRef;
            clip.effects.insert(clip.effects.begin(), std::move(effect));
        }
    }, tr("Template")));
    const QJsonObject transition = spec.value(u"transition"_s).toObject();
    if (const fx::TransitionPreset *preset = fx::Library::core().transition(transition.value(u"preset"_s).toString())) {
        build(TimelineEditor(data(), data().mainSequenceId)
                  .applyTransitionToAll(main.id, AssetRef{QString::fromLatin1(fx::Library::kCorePack), preset->id, preset->version},
                                        seconds(transition.value(u"seconds"_s).toDouble(0.5))));
    }
    for (const QJsonValue &value : spec.value(u"texts"_s).toArray()) {
        const QJsonObject entry = value.toObject();
        TextClipData text = textInStyle(entry.value(u"style"_s).toString(), tr("Your text"));
        const QString written = localized(entry.value(u"text"_s));
        if (!written.isEmpty()) {
            text.text = written;
        }
        const double length = entry.value(u"seconds"_s).toDouble(0);
        build(TimelineEditor(data(), data().mainSequenceId)
                  .insertText(seconds(entry.value(u"at"_s).toDouble(0)), std::move(text),
                              length > 0 ? seconds(length) : RationalTime(0, rate)));
    }
    for (const QJsonValue &value : spec.value(u"stickers"_s).toArray()) {
        const QJsonObject entry = value.toObject();
        const fx::StickerPreset *preset = fx::Library::core().sticker(entry.value(u"id"_s).toString());
        if (!preset) {
            continue;
        }
        StickerClipData sticker;
        sticker.source = AssetRef{QString::fromLatin1(fx::Library::kCorePack), preset->id, preset->version};
        if (!preset->graphic.isEmpty()) {
            sticker.graphic = projectjson::graphicFromJson(preset->graphic);
        }
        if (!preset->visualizer.isEmpty()) {
            sticker.visualizer = projectjson::visualizerFromJson(preset->visualizer);
        }
        const RationalTime at = seconds(entry.value(u"at"_s).toDouble(0));
        const double length = entry.value(u"seconds"_s).toDouble(0);
        // 0 s: until the end of the video (a progress bar, a frame around the whole video).
        const RationalTime duration = length > 0 ? seconds(length) : end - at;
        if (duration.value() > 0) {
            build(TimelineEditor(data(), data().mainSequenceId).insertSticker(at, std::move(sticker), duration));
        }
    }
    if (!name.isEmpty()) {
        EditResult rename;
        rename.script.push_back(edits::setName(data().name, name));
        rename.text = tr("Rename project");
        build(std::move(rename));
    }
    m_document->undoStack().clear(); // the template is where the project starts, not an edit to undo
    if (const std::optional<ClipId> first = placeholders().empty() ? std::nullopt : std::optional(placeholders().front())) {
        setSelection({*first});
    }
    return true;
}

bool EditorController::addSticker(const QString &assetId)
{
    const fx::StickerPreset *preset = fx::Library::core().sticker(assetId);
    if (!preset) {
        return false;
    }
    StickerClipData sticker;
    sticker.source = AssetRef{QString::fromLatin1(fx::Library::kCorePack), preset->id, preset->version};
    const Rational rate = data().settings.frameRate;
    RationalTime duration = RationalTime::fromSeconds(Rational(std::max(1, static_cast<int>(std::lround(preset->defaultDuration)))),
                                                      rate, Rounding::NearestEven);
    if (!preset->graphic.isEmpty()) {
        sticker.graphic = projectjson::graphicFromJson(preset->graphic);
    }
    if (!preset->visualizer.isEmpty() || (sticker.graphic && sticker.graphic->kind == GraphicKind::ProgressBar)) {
        if (!preset->visualizer.isEmpty()) {
            sticker.visualizer = projectjson::visualizerFromJson(preset->visualizer);
        }
        // A visualizer follows the music and a progress bar the video: they last until the end of the video.
        const int remaining = m_timeline->duration() - playhead();
        if (remaining > duration.value()) {
            duration = RationalTime(remaining, rate);
        }
    }
    return apply(TimelineEditor(data(), data().mainSequenceId).insertSticker(RationalTime(playhead(), rate), std::move(sticker), duration));
}

void EditorController::importStickers(const QList<QUrl> &urls)
{
    QStringList paths;
    for (const QUrl &url : urls) {
        if (url.isLocalFile()) {
            paths << url.toLocalFile();
            m_pendingInserts.append(PendingInsert{QFileInfo(url.toLocalFile()).absoluteFilePath(), 0, true});
        }
    }
    importPaths(paths);
}

void EditorController::addLogo(const QUrl &file)
{
    importStickers({file});
}

void EditorController::addWatermark(const QUrl &file)
{
    if (!file.isLocalFile()) {
        return;
    }
    PendingInsert pending{QFileInfo(file.toLocalFile()).absoluteFilePath(), 0};
    pending.watermark = true;
    m_pendingInserts.append(pending);
    importPaths({pending.path});
}

void EditorController::addIntro(const QUrl &file)
{
    if (file.isLocalFile()) {
        importAndInsertPaths({file.toLocalFile()}, 0, m_timeline->mainRow());
    }
}

void EditorController::addOutro(const QUrl &file)
{
    if (file.isLocalFile()) {
        importAndInsertPaths({file.toLocalFile()}, m_timeline->duration(), m_timeline->mainRow());
    }
}

void EditorController::addBrandMusic(const QUrl &file)
{
    if (file.isLocalFile()) {
        importAndInsertPaths({file.toLocalFile()}, 0, m_timeline->mainRow());
    }
}

bool EditorController::addBrandText(const QString &fontFamily, const QColor &color)
{
    TextClipData text = textInStyle({}, tr("Your text"));
    if (!fontFamily.isEmpty()) {
        text.style.fontFamily = fontFamily;
    }
    if (color.isValid()) {
        text.style.color = Param(Color{static_cast<std::uint8_t>(color.red()), static_cast<std::uint8_t>(color.green()),
                                       static_cast<std::uint8_t>(color.blue()), 255});
    }
    const Rational rate = data().settings.frameRate;
    return apply(TimelineEditor(data(), data().mainSequenceId)
                     .insertText(RationalTime(playhead(), rate), std::move(text), RationalTime(0, rate)));
}

void EditorController::detectBeats()
{
    const std::optional<ClipId> id = focusClip();
    const Clip *clip = id ? data().findClip(*id) : nullptr;
    const MediaClipData *media = clip ? clip->media() : nullptr;
    const Media *source = media ? data().findMedia(media->mediaId) : nullptr;
    if (!source || !source->info.audio || media->streams == Streams::VideoOnly) {
        emit message(tr("Select a music or video clip with sound to find its beats."), false);
        return;
    }
    detectBeatsOf(*id);
}

void EditorController::ensureBeats()
{
    const Sequence *sequence = data().mainSequence();
    if (!sequence) {
        return;
    }
    const auto isBeat = [](const Marker &m) { return m.kind == MarkerKind::Beat; };
    if (std::any_of(sequence->markers.begin(), sequence->markers.end(), isBeat)) {
        return;
    }
    const Clip *music = nullptr;
    for (const auto *tracks : {&sequence->audioTracks, &sequence->visualTracks}) {
        for (const Track &track : *tracks) {
            for (const Clip &clip : track.clips) {
                if (std::any_of(clip.markers.begin(), clip.markers.end(), isBeat)) {
                    return;
                }
                const MediaClipData *media = clip.media();
                const Media *source = media ? data().findMedia(media->mediaId) : nullptr;
                if (!music && tracks == &sequence->audioTracks && source && source->info.audio) {
                    music = &clip;
                }
            }
        }
    }
    if (!music) {
        emit message(tr("Add music: the effect follows its beats."), false);
        return;
    }
    detectBeatsOf(music->id);
}

bool EditorController::addEffectLayer(const QString &effectId)
{
    const fx::VideoEffectPreset *preset = fx::Library::core().videoEffect(effectId);
    if (!preset) {
        return false;
    }
    const Rational rate = data().settings.frameRate;
    EditResult layer = TimelineEditor(data(), data().mainSequenceId)
                           .insertAdjustment(RationalTime(playhead(), rate), RationalTime::fromSeconds(Rational(3), rate, Rounding::NearestEven));
    const ClipId layerId = layer.primaryClip;
    if (!apply(std::move(layer))) {
        return false;
    }
    // The effect on the new (selected) layer: a second undo step, undone first.
    select(layerId.toString(), false);
    return inspector()->toggleEffect(effectId);
}

void EditorController::detectBeatsOf(const ClipId &clipId)
{
    const Clip *clip = data().findClip(clipId);
    const MediaClipData *media = clip ? clip->media() : nullptr;
    const Media *source = media ? data().findMedia(media->mediaId) : nullptr;
    if (!source) {
        return;
    }
    emit message(tr("Finding the beats…"), false);
    QPointer<EditorController> self(this);
    QThreadPool::globalInstance()->start([self, clipId, media = *source] {
        const std::optional<engine::Spectrum> spectrum = engine::cachedSpectrum(media);
        const std::vector<double> beats = spectrum ? engine::detectBeats(*spectrum) : std::vector<double>{};
        QMetaObject::invokeMethod(qApp, [self, clipId, beats] {
            if (!self || !self->data().findClip(clipId)) {
                return;
            }
            if (beats.empty()) {
                emit self->message(tr("No beats found in this audio."), false);
                return;
            }
            EditResult result = TimelineEditor(self->data(), self->data().mainSequenceId).setBeatMarkers(clipId, beats);
            if (self->apply(std::move(result), false)) {
                const Clip *clip = self->data().findClip(clipId);
                const auto count = clip ? std::count_if(clip->markers.begin(), clip->markers.end(),
                                                        [](const Marker &m) { return m.kind == MarkerKind::Beat; })
                                        : 0;
                emit self->message(tr("%n beat(s) marked on the clip", nullptr, static_cast<int>(count)), true);
            }
        });
    });
}

bool EditorController::setTrackVolume(const QString &trackId, double gainDb)
{
    const std::optional<TrackId> id = TrackId::fromString(trackId);
    if (!id) {
        return false;
    }
    const double value = std::clamp(gainDb, -60.0, 12.0);
    EditResult result = TimelineEditor(data(), data().mainSequenceId)
                            .updateTrack(*id, [value](Track &track) { track.gainDb = Param(value); }, tr("Change track volume"));
    return push(std::move(result), MergeKey{u"track-volume:"_s + trackId, m_trackGesture});
}

void EditorController::endTrackGesture()
{
    ++m_trackGesture;
}

bool EditorController::setTrackMuted(const QString &trackId, bool muted)
{
    const std::optional<TrackId> id = TrackId::fromString(trackId);
    if (!id) {
        return false;
    }
    return push(TimelineEditor(data(), data().mainSequenceId)
                    .updateTrack(*id, [muted](Track &track) { track.muted = muted; },
                                 muted ? tr("Mute track") : tr("Unmute track")));
}

bool EditorController::setTrackLocked(const QString &trackId, bool locked)
{
    const std::optional<TrackId> id = TrackId::fromString(trackId);
    if (!id) {
        return false;
    }
    return push(TimelineEditor(data(), data().mainSequenceId)
                    .updateTrack(*id, [locked](Track &track) { track.locked = locked; },
                                 locked ? tr("Lock track") : tr("Unlock track")));
}

bool EditorController::setTrackHidden(const QString &trackId, bool hidden)
{
    const std::optional<TrackId> id = TrackId::fromString(trackId);
    if (!id) {
        return false;
    }
    return push(TimelineEditor(data(), data().mainSequenceId)
                    .updateTrack(*id, [hidden](Track &track) { track.hidden = hidden; },
                                 hidden ? tr("Hide track") : tr("Show track")));
}

bool EditorController::setTrackSolo(const QString &trackId, bool solo)
{
    const std::optional<TrackId> id = TrackId::fromString(trackId);
    if (!id) {
        return false;
    }
    return push(TimelineEditor(data(), data().mainSequenceId)
                    .updateTrack(*id, [solo](Track &track) { track.solo = solo; },
                                 solo ? tr("Solo track") : tr("Unsolo track")));
}

bool EditorController::freezeFrame()
{
    const std::optional<ClipId> id = clipForLibrary();
    const Clip *clip = id ? data().findClip(*id) : nullptr;
    const MediaClipData *media = clip ? clip->media() : nullptr;
    const Media *source = media ? data().findMedia(media->mediaId) : nullptr;
    const Rational rate = data().settings.frameRate;
    const RationalTime at(playhead(), rate);
    if (!source || source->kind != MediaKind::Video || media->streams == Streams::AudioOnly || at < clip->start ||
        !(at < clip->end())) {
        emit message(tr("Move the playhead over a video clip to freeze a frame."), false);
        return false;
    }
    // The frame of the source shown there (speed and direction included, D-44), at up to 4K.
    const double seconds = keyframeTime(*clip, at - clip->start).toSecondsDouble();
    const QImage frame = engine::extractFrame(source->path, seconds, 2160);
    if (frame.isNull()) {
        emit message(tr("This frame cannot be read from the file."), false);
        return false;
    }
    // Pictures made by the editor live in the draft (docs/FILE_FORMAT.md §6.1).
    const QDir folder(m_document->directory() + u"/media"_s);
    const QString path = folder.filePath(u"freeze-%1.png"_s.arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
    if (!QDir().mkpath(folder.path()) || !frame.save(path)) {
        emit message(tr("The frame cannot be saved in the draft folder."), false);
        return false;
    }
    Media still;
    still.id = MediaId::create();
    still.kind = MediaKind::Image;
    still.name = tr("Freeze frame");
    still.path = path;
    if (const std::optional<MediaFingerprint> fingerprint = engine::sampledFingerprint(path)) {
        still.fingerprint = *fingerprint;
    }
    VideoStreamInfo video;
    video.width = frame.width();
    video.height = frame.height();
    video.codec = u"png"_s;
    still.info.video = video;
    // The picture and its place on the timeline: one undo step.
    const MergeKey key{u"freeze"_s, ++m_importBatch};
    EditResult add;
    add.script.push_back(edits::insertMedia(static_cast<int>(data().media.size()), still));
    add.text = tr("Freeze frame");
    if (!push(std::move(add), key)) {
        return false;
    }
    EditResult freeze = TimelineEditor(data(), data().mainSequenceId)
                            .insertFreezeFrame(clip->id, at, still.id,
                                               RationalTime(std::llround(TimelineEditor::kDefaultTextSeconds * rate.toDouble()), rate));
    if (!freeze.ok()) {
        m_document->undoStack().undo(); // the picture alone is of no use
        emit message(freeze.error, false);
        return false;
    }
    const ClipId primary = freeze.primaryClip;
    freeze.text = tr("Freeze frame");
    if (!push(std::move(freeze), key)) {
        return false;
    }
    if (!primary.isNull()) {
        setSelection({primary});
    }
    return true;
}

// ---- Timeline -----------------------------------------------------------------------------------------------------

bool EditorController::moveClip(const QString &clipId, int frame, int trackRow)
{
    const std::optional<ClipId> id = ClipId::fromString(clipId);
    const Sequence *sequence = data().mainSequence();
    if (!id || !sequence || !data().findClip(*id)) {
        return false;
    }
    TimelineEditor editor(data(), data().mainSequenceId);
    const RationalTime start(std::max(0, frame), data().settings.frameRate);
    const std::optional<TimelineModel::TrackRow> row = m_timeline->trackRow(trackRow);
    bool audioClip = false;
    for (const Track &track : sequence->audioTracks) {
        for (const Clip &clip : track.clips) {
            audioClip = audioClip || clip.id == *id;
        }
    }
    if (!row) {
        // Into the free space above the tracks (visual clips) or below them (audio clips): a new track.
        if (trackRow < 0 && !audioClip) {
            return apply(editor.moveClipToNewTrack(*id, start, static_cast<int>(sequence->visualTracks.size())));
        }
        if (trackRow >= m_timeline->trackRowCount() && audioClip) {
            return apply(editor.moveClipToNewTrack(*id, start, static_cast<int>(sequence->audioTracks.size())));
        }
        return apply(editor.moveClip(*id, start));
    }
    if (row->audio != audioClip) {
        return apply(editor.moveClip(*id, start)); // stays on its kind of track, moves in time
    }
    return apply(editor.moveClip(*id, start, row->id));
}

bool EditorController::trimClip(const QString &clipId, bool startEdge, int frame)
{
    const std::optional<ClipId> id = ClipId::fromString(clipId);
    if (!id) {
        return false;
    }
    TimelineEditor editor(data(), data().mainSequenceId);
    return apply(editor.trimClip(*id, startEdge ? ClipEdge::Start : ClipEdge::End,
                                 RationalTime(std::max(0, frame), data().settings.frameRate)));
}

std::optional<ClipId> EditorController::clipAtPlayhead() const
{
    const Sequence *sequence = data().mainSequence();
    if (!sequence) {
        return std::nullopt;
    }
    const RationalTime at(playhead(), data().settings.frameRate);
    const auto inside = [&at](const Track &track) -> std::optional<ClipId> {
        for (const Clip &clip : track.clips) {
            if (clip.start < at && at < clip.end()) {
                return clip.id;
            }
        }
        return std::nullopt;
    };
    // The main track first (CapCut), then the overlays from the top, then the audio tracks.
    if (!sequence->visualTracks.empty()) {
        if (auto id = inside(sequence->visualTracks.front())) {
            return id;
        }
    }
    for (auto it = sequence->visualTracks.rbegin(); it != sequence->visualTracks.rend(); ++it) {
        if (auto id = inside(*it)) {
            return id;
        }
    }
    for (const Track &track : sequence->audioTracks) {
        if (auto id = inside(track)) {
            return id;
        }
    }
    return std::nullopt;
}

std::vector<ClipId> EditorController::splitTargets() const
{
    const RationalTime at(playhead(), data().settings.frameRate);
    std::vector<ClipId> targets;
    for (const ClipId &id : m_selection) {
        const Clip *clip = data().findClip(id);
        if (clip && clip->start < at && at < clip->end()) {
            targets.push_back(id);
        }
    }
    if (targets.empty() && m_selection.isEmpty()) {
        if (auto id = clipAtPlayhead()) {
            targets.push_back(*id);
        }
    }
    return targets;
}

bool EditorController::canSplit() const
{
    return !splitTargets().empty();
}

bool EditorController::split()
{
    const std::vector<ClipId> targets = splitTargets();
    if (targets.empty()) {
        emit message(tr("Move the playhead over a clip to split it."), false);
        return false;
    }
    const RationalTime at(playhead(), data().settings.frameRate);
    QUndoStack &stack = m_document->undoStack();
    if (targets.size() > 1) {
        stack.beginMacro(tr("Split clips"));
    }
    bool ok = true;
    for (const ClipId &id : targets) {
        ok = apply(TimelineEditor(data(), data().mainSequenceId).splitClip(id, at), targets.size() == 1) && ok;
    }
    if (targets.size() > 1) {
        stack.endMacro();
    }
    return ok;
}

std::optional<ClipId> EditorController::rippleTrimTarget() const
{
    const RationalTime at(playhead(), data().settings.frameRate);
    for (const ClipId &id : m_selection) {
        const Clip *clip = data().findClip(id);
        if (clip && clip->start < at && at < clip->end()) {
            return id;
        }
    }
    return clipAtPlayhead();
}

bool EditorController::canRippleTrimLeft() const
{
    const auto target = rippleTrimTarget();
    if (!target) {
        return false;
    }
    const Clip *clip = data().findClip(*target);
    if (!clip) {
        return false;
    }
    const RationalTime at(playhead(), data().settings.frameRate);
    return clip->start < at && at < clip->end();
}

bool EditorController::canRippleTrimRight() const
{
    const auto target = rippleTrimTarget();
    if (!target) {
        return false;
    }
    const Clip *clip = data().findClip(*target);
    if (!clip) {
        return false;
    }
    const RationalTime at(playhead(), data().settings.frameRate);
    return clip->start < at && at < clip->end();
}

bool EditorController::rippleTrimLeft()
{
    const auto target = rippleTrimTarget();
    if (!target) {
        emit message(tr("Move the playhead over a clip to ripple trim."), false);
        return false;
    }
    const Clip *clip = data().findClip(*target);
    if (!clip) {
        return false;
    }
    const RationalTime at(playhead(), data().settings.frameRate);
    if (at <= clip->start || at >= clip->end()) {
        emit message(tr("Move the playhead over a clip to ripple trim."), false);
        return false;
    }
    const RationalTime oldStart = clip->start;
    const bool ok = apply(TimelineEditor(data(), data().mainSequenceId).rippleTrimClip(*target, ClipEdge::Start, at));
    if (ok) {
        const int newPos = static_cast<int>(oldStart.rescaled(data().settings.frameRate, Rounding::Floor).value());
        m_player->seek(newPos);
        emit message(tr("Trimmed start to playhead"), true);
    }
    return ok;
}

bool EditorController::rippleTrimRight()
{
    const auto target = rippleTrimTarget();
    if (!target) {
        emit message(tr("Move the playhead over a clip to ripple trim."), false);
        return false;
    }
    const Clip *clip = data().findClip(*target);
    if (!clip) {
        return false;
    }
    const RationalTime at(playhead(), data().settings.frameRate);
    if (at <= clip->start || at >= clip->end()) {
        emit message(tr("Move the playhead over a clip to ripple trim."), false);
        return false;
    }
    const bool ok = apply(TimelineEditor(data(), data().mainSequenceId).rippleTrimClip(*target, ClipEdge::End, at));
    if (ok) {
        emit message(tr("Trimmed playhead to end"), true);
    }
    return ok;
}

bool EditorController::deleteSelection()
{
    if (m_selection.isEmpty()) {
        return false;
    }
    const std::vector<ClipId> ids(m_selection.cbegin(), m_selection.cend());
    if (!apply(TimelineEditor(data(), data().mainSequenceId).deleteClips(ids), false)) {
        return false;
    }
    setSelection({});
    emit message(ids.size() == 1 ? tr("Clip deleted") : tr("%n clips deleted", nullptr, static_cast<int>(ids.size())), true);
    return true;
}

bool EditorController::rippleDeleteSelection()
{
    std::vector<ClipId> ids(m_selection.cbegin(), m_selection.cend());
    if (ids.empty()) {
        const auto target = clipAtPlayhead();
        if (target) {
            ids.push_back(*target);
        }
    }
    if (ids.empty()) {
        return false;
    }
    if (!apply(TimelineEditor(data(), data().mainSequenceId).rippleDeleteClips(ids), false)) {
        return false;
    }
    setSelection({});
    emit message(ids.size() == 1 ? tr("Ripple deleted clip") : tr("%n clips ripple deleted", nullptr, static_cast<int>(ids.size())), true);
    return true;
}

bool EditorController::trimSelectedToPlayhead(bool startEdge)
{
    const auto target = focusClip().has_value() ? focusClip() : clipAtPlayhead();
    if (!target) {
        return false;
    }
    return trimClip(target->toString(), startEdge, playhead());
}

bool EditorController::duplicateSelection()
{
    if (m_selection.isEmpty()) {
        return false;
    }
    const std::vector<ClipId> ids(m_selection.cbegin(), m_selection.cend());
    return apply(TimelineEditor(data(), data().mainSequenceId).duplicateClips(ids));
}

void EditorController::select(const QString &clipId, bool additive)
{
    const std::optional<ClipId> id = ClipId::fromString(clipId);
    if (!id) {
        return;
    }
    QSet<ClipId> selection = additive ? m_selection : QSet<ClipId>{};
    if (additive && selection.contains(*id)) {
        selection.remove(*id);
    } else {
        selection.insert(*id);
        if (m_focus != *id) {
            m_focus = *id;
            if (selection == m_selection) {
                emit selectionChanged(); // same clips, another focus
            }
        }
    }
    setSelection(std::move(selection));
}

void EditorController::clearSelection()
{
    const bool other = !m_transition.isNull() || !m_cut.isNull();
    m_transition = {};
    m_cut = {};
    setSelection({});
    if (other) {
        emit selectionChanged();
    }
}

void EditorController::selectAll()
{
    const Sequence *sequence = data().mainSequence();
    if (!sequence) {
        return;
    }
    QSet<ClipId> selection;
    for (const auto *tracks : {&sequence->visualTracks, &sequence->audioTracks}) {
        for (const Track &track : *tracks) {
            if (track.locked) {
                continue;
            }
            for (const Clip &clip : track.clips) {
                selection.insert(clip.id);
            }
        }
    }
    m_transition = {};
    m_cut = {};
    setSelection(std::move(selection));
}

void EditorController::undo()
{
    m_document->undoStack().undo();
}

void EditorController::redo()
{
    m_document->undoStack().redo();
}

void EditorController::setSnappingEnabled(bool enabled)
{
    if (m_snappingEnabled != enabled) {
        m_snappingEnabled = enabled;
        emit snappingChanged();
        emit message(enabled ? tr("Snapping enabled") : tr("Snapping disabled"), false);
    }
}

void EditorController::toggleSnapping()
{
    setSnappingEnabled(!m_snappingEnabled);
}

bool EditorController::skimmingEnabled() const
{
    return m_document->uiState().value(u"skimming"_s).toBool(true);
}

void EditorController::setSkimmingEnabled(bool enabled)
{
    if (enabled == skimmingEnabled()) {
        return;
    }
    QJsonObject state = m_document->uiState();
    state.insert(u"skimming"_s, enabled);
    m_document->setUiState(state);
    if (!enabled) {
        m_player->endSkim();
    }
    emit skimmingChanged();
    emit message(enabled ? tr("Preview axis on: point at the timeline to see that frame")
                         : tr("Preview axis off"), false);
}

void EditorController::toggleSkimming()
{
    setSkimmingEnabled(!skimmingEnabled());
}

double EditorController::panelSize(const QString &key, double fallback) const
{
    const QJsonValue value = m_document->uiState().value(u"panels"_s).toObject().value(key);
    return value.isDouble() && value.toDouble() > 0 ? value.toDouble() : fallback;
}

void EditorController::setPanelSize(const QString &key, double size)
{
    if (size <= 0 || std::abs(panelSize(key, 0) - size) < 0.5) {
        return;
    }
    QJsonObject state = m_document->uiState();
    QJsonObject panels = state.value(u"panels"_s).toObject();
    panels.insert(key, std::round(size));
    state.insert(u"panels"_s, panels);
    m_document->setUiState(state);
}

bool EditorController::magneticMain() const
{
    const Sequence *sequence = data().mainSequence();
    return sequence ? sequence->magneticMain : true;
}

bool EditorController::setMagneticMain(bool enabled)
{
    if (magneticMain() == enabled) {
        return true;
    }
    const bool ok = apply(TimelineEditor(data(), data().mainSequenceId).setMagneticMain(enabled));
    if (ok) {
        emit magneticMainChanged();
        emit message(enabled ? tr("Magnetic main track enabled") : tr("Magnetic main track disabled"), false);
    }
    return ok;
}

bool EditorController::toggleMagneticMain()
{
    return setMagneticMain(!magneticMain());
}

int EditorController::inPoint() const
{
    return m_inPoint;
}

int EditorController::outPoint() const
{
    return m_outPoint;
}

bool EditorController::hasInOut() const
{
    return m_inPoint >= 0 || m_outPoint >= 0;
}

void EditorController::setInPoint(int frame)
{
    const int target = frame >= 0 ? frame : playhead();
    if (m_outPoint >= 0 && target > m_outPoint) {
        m_outPoint = target;
    }
    m_inPoint = target;
    emit inOutChanged();
    emit message(tr("In point set at %1").arg(m_player->timecode(m_inPoint)), false);
}

void EditorController::setOutPoint(int frame)
{
    const int target = frame >= 0 ? frame : playhead();
    if (m_inPoint >= 0 && target < m_inPoint) {
        m_inPoint = target;
    }
    m_outPoint = target;
    emit inOutChanged();
    emit message(tr("Out point set at %1").arg(m_player->timecode(m_outPoint)), false);
}

void EditorController::clearInOut()
{
    if (m_inPoint != -1 || m_outPoint != -1) {
        m_inPoint = -1;
        m_outPoint = -1;
        emit inOutChanged();
        emit message(tr("In/Out points cleared"), false);
    }
}

void EditorController::nextCut()
{
    const int currentPos = playhead();
    int nextPos = -1;
    const Sequence *sequence = data().mainSequence();
    if (!sequence) {
        return;
    }
    const Rational rate = data().settings.frameRate;
    std::set<int> cuts;
    cuts.insert(m_timeline->duration());
    for (const auto *tracks : {&sequence->visualTracks, &sequence->audioTracks}) {
        for (const Track &track : *tracks) {
            for (const Clip &clip : track.clips) {
                cuts.insert(static_cast<int>(clip.start.rescaled(rate, Rounding::NearestEven).value()));
                cuts.insert(static_cast<int>(clip.end().rescaled(rate, Rounding::NearestEven).value()));
            }
        }
    }
    for (const Marker &marker : sequence->markers) {
        cuts.insert(static_cast<int>(marker.time.rescaled(rate, Rounding::NearestEven).value()));
    }
    for (int cut : cuts) {
        if (cut > currentPos) {
            nextPos = cut;
            break;
        }
    }
    if (nextPos >= 0) {
        m_player->seek(nextPos);
    }
}

void EditorController::previousCut()
{
    const int currentPos = playhead();
    int prevPos = -1;
    const Sequence *sequence = data().mainSequence();
    if (!sequence) {
        return;
    }
    const Rational rate = data().settings.frameRate;
    std::set<int> cuts;
    cuts.insert(0);
    for (const auto *tracks : {&sequence->visualTracks, &sequence->audioTracks}) {
        for (const Track &track : *tracks) {
            for (const Clip &clip : track.clips) {
                cuts.insert(static_cast<int>(clip.start.rescaled(rate, Rounding::NearestEven).value()));
                cuts.insert(static_cast<int>(clip.end().rescaled(rate, Rounding::NearestEven).value()));
            }
        }
    }
    for (const Marker &marker : sequence->markers) {
        cuts.insert(static_cast<int>(marker.time.rescaled(rate, Rounding::NearestEven).value()));
    }
    for (auto it = cuts.rbegin(); it != cuts.rend(); ++it) {
        if (*it < currentPos) {
            prevPos = *it;
            break;
        }
    }
    if (prevPos >= 0) {
        m_player->seek(prevPos);
    }
}

int EditorController::snap(int frame, const QStringList &excludedClips, int threshold) const
{
    return snapRange(frame, 0, excludedClips, threshold);
}

int EditorController::snapRange(int start, int duration, const QStringList &excludedClips, int threshold) const
{
    if (!m_snappingEnabled) {
        return start;
    }
    QSet<ClipId> excluded;
    for (const QString &text : excludedClips) {
        if (const auto id = ClipId::fromString(text)) {
            excluded.insert(*id);
        }
    }
    std::vector<int> targets{0, playhead()};
    for (const auto &[begin, end] : m_timeline->clipEdges(excluded)) {
        targets.push_back(begin);
        targets.push_back(end);
    }
    const Sequence *sequence = data().mainSequence();
    if (sequence) {
        const Rational rate = data().settings.frameRate;
        for (const Marker &marker : sequence->markers) {
            targets.push_back(static_cast<int>(marker.time.rescaled(rate, Rounding::NearestEven).value()));
        }
        for (const Track &track : sequence->visualTracks) {
            for (const Clip &clip : track.clips) {
                if (!excluded.contains(clip.id)) {
                    for (const Marker &marker : clip.markers) {
                        targets.push_back(static_cast<int>((clip.start + marker.time).rescaled(rate, Rounding::NearestEven).value()));
                    }
                }
            }
        }
        for (const Track &track : sequence->audioTracks) {
            for (const Clip &clip : track.clips) {
                if (!excluded.contains(clip.id)) {
                    for (const Marker &marker : clip.markers) {
                        targets.push_back(static_cast<int>((clip.start + marker.time).rescaled(rate, Rounding::NearestEven).value()));
                    }
                }
            }
        }
    }
    int best = threshold + 1;
    int delta = 0;
    for (const int target : targets) {
        for (const int edge : {start, start + duration}) {
            const int distance = std::abs(target - edge);
            if (distance < best) {
                best = distance;
                delta = target - edge;
            }
        }
    }
    return best <= threshold ? start + delta : start;
}

std::optional<Canvas> EditorController::canvasFor(int preset) const
{
    const Sequence *sequence = data().mainSequence();
    if (!sequence || preset < 0 || preset > static_cast<int>(CanvasPreset::Portrait3x4)) {
        return std::nullopt;
    }
    // The same resolution class ("1080p") in the new shape.
    const int shortSide = std::min(sequence->canvas.width, sequence->canvas.height);
    const auto size = [shortSide](double width, double height) {
        return width >= height ? QSize(evenSize(shortSide * width / height), shortSide)
                               : QSize(shortSide, evenSize(shortSide * height / width));
    };
    QSize dimensions;
    switch (static_cast<CanvasPreset>(preset)) {
    case CanvasPreset::Landscape16x9:
        dimensions = size(16, 9);
        break;
    case CanvasPreset::Portrait9x16:
        dimensions = size(9, 16);
        break;
    case CanvasPreset::Square1x1:
        dimensions = size(1, 1);
        break;
    case CanvasPreset::Portrait4x5:
        dimensions = size(4, 5);
        break;
    case CanvasPreset::Cinema21x9:
        dimensions = size(21, 9);
        break;
    case CanvasPreset::Portrait3x4:
        dimensions = size(3, 4);
        break;
    case CanvasPreset::Custom:
        return std::nullopt;
    }
    return Canvas{dimensions.width(), dimensions.height(), static_cast<CanvasPreset>(preset)};
}

void EditorController::setCanvasPreset(int preset)
{
    const Sequence *sequence = data().mainSequence();
    const std::optional<Canvas> canvas = canvasFor(preset);
    if (!sequence || !canvas || *canvas == sequence->canvas) {
        return;
    }
    EditResult change;
    change.script.push_back(edits::setCanvas(data().mainSequenceId, sequence->canvas, *canvas));
    change.text = tr("Change format");
    apply(std::move(change), false);
}

// ---- Export -------------------------------------------------------------------------------------------------------

void EditorController::setHardwareEncoding(QStringList encoders, QString gpuName)
{
    m_hardwareEncoders = std::move(encoders);
    m_gpuName = std::move(gpuName);
    m_exportJob->setHardwareEncoders(m_hardwareEncoders);
}

QString EditorController::exportCurrentFrame(const QString &fileName, const QString &folder)
{
    const QImage frame = m_player->sink()->latest();
    if (frame.isNull() || !QFileInfo(folder).isDir()) {
        return {};
    }
    // The same characters the export stays away from; a recognizable base name.
    QString base = fileName.trimmed();
    base.replace(QRegularExpression(u"[/\\\\:*?\"<>|]"_s), u"-"_s);
    if (base.isEmpty()) {
        base = data().name;
        base.replace(QRegularExpression(u"[/\\\\:*?\"<>|]"_s), u"-"_s);
        base = base.trimmed().isEmpty() ? u"frame"_s : base.trimmed();
    }
    QString path = QDir(folder).filePath(base + u".png"_s);
    for (int n = 2; QFileInfo::exists(path); ++n) {
        path = QDir(folder).filePath(u"%1 (%2).png"_s.arg(base).arg(n));
    }
    if (!frame.save(path, "PNG")) {
        return {};
    }
    return path;
}

namespace {

// "base.ext" in `folder`, or "base (2).ext"… when it exists: an export never overwrites a file.
QString uniqueFilePath(const QString &folder, QString base, const QString &extension)
{
    base.replace(QRegularExpression(u"[/\\\\:*?\"<>|]"_s), u"-"_s);
    base = base.trimmed();
    if (base.isEmpty()) {
        base = u"vedit"_s;
    }
    QString path = QDir(folder).filePath(base + u"."_s + extension);
    for (int n = 2; QFileInfo::exists(path); ++n) {
        path = QDir(folder).filePath(u"%1 (%2).%3"_s.arg(base).arg(n).arg(extension));
    }
    return path;
}

} // namespace

QString EditorController::coverPath() const
{
    const QString path = QDir(m_document->directory()).filePath(u"cover.png"_s);
    return QFileInfo::exists(path) ? path : QString();
}

QUrl EditorController::coverUrl() const
{
    const QString path = coverPath();
    if (path.isEmpty()) {
        return {};
    }
    QUrl url = QUrl::fromLocalFile(path);
    url.setQuery(u"v=%1"_s.arg(m_coverSerial));
    return url;
}

bool EditorController::setCover(const QImage &image)
{
    if (image.isNull()) {
        return false;
    }
    // At most 1920 px on the long side: a cover is a still, and the draft folder stays small.
    const QImage cover = std::max(image.width(), image.height()) > 1920
                             ? image.scaled(1920, 1920, Qt::KeepAspectRatio, Qt::SmoothTransformation)
                             : image;
    const QString path = QDir(m_document->directory()).filePath(u"cover.png"_s);
    if (!cover.convertToFormat(QImage::Format_RGB32).save(path, "PNG")) {
        emit message(tr("The cover could not be saved."), false);
        return false;
    }
    m_document->setThumbnail(cover.scaled(320, 320, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    ++m_coverSerial;
    emit coverChanged();
    return true;
}

bool EditorController::setCoverFromCurrentFrame()
{
    const QImage frame = m_player->sink()->latest();
    if (frame.isNull()) {
        emit message(tr("No video frame available to set as cover."), false);
        return false;
    }
    if (!setCover(frame)) {
        return false;
    }
    emit message(tr("Cover set from current frame."), false);
    return true;
}

bool EditorController::setCoverFromImage(const QUrl &file)
{
    const QImage image(file.isLocalFile() ? file.toLocalFile() : file.toString());
    if (image.isNull()) {
        emit message(tr("This picture cannot be opened."), false);
        return false;
    }
    if (!setCover(image)) {
        return false;
    }
    emit message(tr("Cover set from the picture."), false);
    return true;
}

void EditorController::clearCover()
{
    const QString path = coverPath();
    if (path.isEmpty()) {
        return;
    }
    QFile::remove(path);
    ++m_coverSerial;
    emit coverChanged();
    emit message(tr("Cover removed: the draft shows where you stopped editing."), false);
}

QString EditorController::exportCover(const QString &folder, bool youtube)
{
    const QImage cover(coverPath());
    if (cover.isNull() || !QFileInfo(folder).isDir()) {
        return {};
    }
    QString base = data().name;
    if (!youtube) {
        const QString path = uniqueFilePath(folder, base + u" - "_s + tr("cover"), u"png"_s);
        return cover.save(path, "PNG") ? path : QString();
    }
    // 1280×720: the whole picture fitted in the middle, on a blurred, darkened copy filling the frame (a vertical
    // cover stays whole instead of being cropped).
    const QSize size(1280, 720);
    QImage canvas(size, QImage::Format_RGB32);
    {
        QPainter painter(&canvas);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        const QImage fill = cover.scaled(size, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
        // Blur by a round trip through a small size (cheap and on every backend: CPU only).
        const QImage blurred = fill.scaled(size / 24, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                                   .scaled(fill.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        painter.drawImage(QPoint((size.width() - blurred.width()) / 2, (size.height() - blurred.height()) / 2), blurred);
        painter.fillRect(canvas.rect(), QColor(0, 0, 0, 90));
        const QImage fitted = cover.scaled(size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        painter.drawImage(QPoint((size.width() - fitted.width()) / 2, (size.height() - fitted.height()) / 2), fitted);
    }
    const QString path = uniqueFilePath(folder, base + u" - "_s + tr("cover") + u" 1280x720"_s, u"jpg"_s);
    return canvas.save(path, "JPG", 92) ? path : QString();
}

QVariantMap EditorController::exportDefaults() const
{
    const QJsonObject last = m_document->uiState().value(u"export"_s).toObject();
    const QSize canvas = canvasSize();
    const int shortSide = std::min(canvas.width(), canvas.height());
    QVariantList resolutions;
    for (const auto &[value, label] : {std::pair{480, u"480p"_s}, std::pair{720, u"720p"_s}, std::pair{1080, u"1080p"_s},
                                       std::pair{1440, u"2K"_s}, std::pair{2160, u"4K"_s}}) {
        resolutions.append(QVariantMap{{u"label"_s, label}, {u"value"_s, value}});
    }
    // Recommended = the project's own size (SPEC 0bis rule 6); listed among the standard ones if it is not one.
    const int resolution = shortSide;
    bool listed = false;
    for (const QVariant &entry : std::as_const(resolutions)) {
        listed = listed || entry.toMap().value(u"value"_s).toInt() == shortSide;
    }
    if (!listed) {
        qsizetype index = 0;
        while (index < resolutions.size() && resolutions[index].toMap().value(u"value"_s).toInt() < shortSide) {
            ++index;
        }
        resolutions.insert(index, QVariantMap{{u"label"_s, u"%1p"_s.arg(shortSide)}, {u"value"_s, shortSide}});
    }
    QVariantList rates;
    const Rational project = data().settings.frameRate;
    bool projectListed = false;
    for (const Rational &rate : {Rational(24), Rational(25), Rational(30), Rational(50), Rational(60)}) {
        rates.append(QVariantMap{{u"label"_s, QString::number(rate.num())}, {u"value"_s, rate.toString()}});
        projectListed = projectListed || rate == project;
    }
    if (!projectListed) {
        rates.append(QVariantMap{{u"label"_s, QLocale().toString(project.toDouble(), 'f', 2)}, {u"value"_s, project.toString()}});
    }
    QString folder = last.value(u"folder"_s).toString();
    if (folder.isEmpty() || !QFileInfo(folder).isDir()) {
        folder = paths::videosDir();
    }
    // A file name the user recognizes: the project's, without characters file managers dislike.
    QString fileName = data().name;
    fileName.replace(QRegularExpression(u"[/\\\\:*?\"<>|]"_s), u"-"_s);
    const QJsonObject saved = last.value(u"advanced"_s).toObject();
    return {{u"fileName"_s, fileName.trimmed().isEmpty() ? u"video"_s : fileName.trimmed()},
            {u"folder"_s, folder},
            {u"resolutions"_s, resolutions},
            {u"resolution"_s, resolution},
            {u"frameRates"_s, rates},
            {u"frameRate"_s, project.toString()},
            {u"quality"_s, last.contains(u"quality"_s) ? last.value(u"quality"_s).toInt() : 1},
            {u"codec"_s, saved.contains(u"codec"_s) ? saved.value(u"codec"_s).toString() : u"h264"_s},
            {u"hardware"_s, saved.contains(u"hardware"_s) ? saved.value(u"hardware"_s).toBool() : true},
            {u"maxFileSizeMB"_s, saved.contains(u"maxFileSizeMB"_s) ? saved.value(u"maxFileSizeMB"_s).toInt() : 0},
            // What the machine can really do (SPEC §5.15): verified by the probe at startup.
            {u"hardwareAvailable"_s, !m_hardwareEncoders.isEmpty()},
            {u"gpuName"_s, m_gpuName}};
}

QString EditorController::exportEstimate(int shortSide, const QString &frameRate, int quality, const QString &codec,
                                         int maxFileSizeMB) const
{
    const Sequence *sequence = data().mainSequence();
    const std::optional<Rational> rate = Rational::fromString(frameRate);
    if (!sequence || !rate) {
        return {};
    }
    engine::ExportSettings settings;
    settings.size = engine::scaledToShortSide(canvasSize(), shortSide);
    settings.frameRate = *rate;
    settings.quality = static_cast<engine::ExportQuality>(std::clamp(quality, 0, 2));
    settings.videoCodec = engine::videoCodecFromName(codec).value_or(engine::VideoCodec::H264);
    settings.maxFileSizeMB = maxFileSizeMB;
    const RationalTime duration = sequence->duration(data().settings.frameRate);
    const double megabytes = static_cast<double>(engine::estimatedFileSize(settings, duration)) / 1e6;
    const QString size = megabytes >= 1000 ? tr("%1 GB").arg(QLocale().toString(megabytes / 1000, 'f', 1))
                                           : tr("%1 MB").arg(QLocale().toString(std::max(1.0, std::round(megabytes)), 'f', 0));
    return tr("%1 × %2 · %3 · about %4")
        .arg(settings.size.width())
        .arg(settings.size.height())
        .arg(MediaPoolModel::durationText(duration.toSecondsDouble()), size);
}

bool EditorController::startExport(const QString &fileName, const QString &folder, int shortSide,
                                   const QString &frameRate, int quality, bool normalizeLoudness,
                                   double targetLufs, const QString &codec, bool hardware, int maxFileSizeMB)
{
    const std::optional<Rational> rate = Rational::fromString(frameRate);
    if (!rate || m_exportJob->running()) {
        return false;
    }
    QString base = fileName.trimmed();
    if (base.endsWith(u".mp4"_s, Qt::CaseInsensitive)) {
        base.chop(4);
    }
    if (base.isEmpty()) {
        base = u"video"_s;
    }
    // Never overwrite an existing video: "name (2).mp4".
    QString path = QDir(folder).filePath(base + u".mp4"_s);
    for (int n = 2; QFileInfo::exists(path); ++n) {
        path = QDir(folder).filePath(u"%1 (%2).mp4"_s.arg(base).arg(n));
    }
    engine::ExportSettings settings;
    settings.outputPath = path;
    settings.size = engine::scaledToShortSide(canvasSize(), shortSide);
    settings.frameRate = *rate;
    settings.quality = static_cast<engine::ExportQuality>(std::clamp(quality, 0, 2));
    settings.videoCodec = engine::videoCodecFromName(codec).value_or(engine::VideoCodec::H264);
    // The user can only turn hardware off (the "Auto" of SPEC §5.15): on a machine without verified GPU encoders
    // the renderer silently stays on software, so an export can never fail for that.
    settings.hardwareEncoder = (hardware && !m_hardwareEncoders.isEmpty()) ? engine::HardwareEncoder::Auto
                                                                          : engine::HardwareEncoder::Off;
    settings.maxFileSizeMB = std::max(0, maxFileSizeMB);
    settings.normalizeLoudness = normalizeLoudness;
    settings.coverImage = coverPath(); // the cover chosen by the user goes into the file
    settings.targetLufs = targetLufs;
    QJsonObject state = m_document->uiState();
    state.insert(u"export"_s, QJsonObject{{u"folder"_s, folder},
                                         {u"quality"_s, quality},
                                         {u"advanced"_s, QJsonObject{{u"codec"_s, engine::videoCodecName(settings.videoCodec)},
                                                                     {u"hardware"_s, hardware},
                                                                     {u"maxFileSizeMB"_s, settings.maxFileSizeMB}}}});
    m_document->setUiState(state);
    return m_exportJob->start(data(), data().mainSequenceId, settings);
}

QString EditorController::folderPath(const QUrl &url) const
{
    return url.isLocalFile() ? url.toLocalFile() : url.toString();
}

bool EditorController::addMarker(const QString &name, const QString &color, const QString &note)
{
    const int currentFrame = playhead();
    const Rational rate = data().settings.frameRate;
    const RationalTime at(currentFrame, rate);

    const auto focus = focusClip();
    if (focus) {
        if (const Clip *c = data().findClip(*focus)) {
            if (at >= c->start && at <= c->end()) {
                // Clip markers are in keyframe time: they stay on the content (D-05).
                const RationalTime time = keyframeTime(*c, at - c->start);
                return push(TimelineEditor(data(), data().mainSequenceId).addClipMarker(*focus, time, name, color, note));
            }
        }
    }
    return push(TimelineEditor(data(), data().mainSequenceId).addSequenceMarker(at, name, color, note));
}

bool EditorController::addSequenceMarker(int frame, const QString &name, const QString &color, const QString &note)
{
    const Rational rate = data().settings.frameRate;
    return push(TimelineEditor(data(), data().mainSequenceId).addSequenceMarker(RationalTime(frame, rate), name, color, note));
}

bool EditorController::removeSequenceMarker(const QString &markerId)
{
    const auto id = MarkerId::fromString(markerId);
    if (!id) {
        return false;
    }
    return push(TimelineEditor(data(), data().mainSequenceId).removeSequenceMarker(*id));
}

bool EditorController::addClipMarker(const QString &clipId, int frameOffset, const QString &name, const QString &color, const QString &note)
{
    const auto id = ClipId::fromString(clipId);
    if (!id) {
        return false;
    }
    const Clip *clip = data().findClip(*id);
    if (!clip) {
        return false;
    }
    const Rational rate = data().settings.frameRate;
    const RationalTime time = keyframeTime(*clip, RationalTime(frameOffset, rate));
    return push(TimelineEditor(data(), data().mainSequenceId).addClipMarker(*id, time, name, color, note));
}

bool EditorController::removeClipMarker(const QString &clipId, const QString &markerId)
{
    const auto cId = ClipId::fromString(clipId);
    const auto mId = MarkerId::fromString(markerId);
    if (!cId || !mId) {
        return false;
    }
    return push(TimelineEditor(data(), data().mainSequenceId).removeClipMarker(*cId, *mId));
}

void EditorController::nextMarker()
{
    const Sequence *sequence = data().mainSequence();
    if (!sequence) {
        return;
    }
    const Rational rate = data().settings.frameRate;
    const int currentFrame = playhead();
    int nextFrame = std::numeric_limits<int>::max();

    for (const Marker &m : sequence->markers) {
        const int f = static_cast<int>(m.time.rescaled(rate, Rounding::NearestEven).value());
        if (f > currentFrame && f < nextFrame) {
            nextFrame = f;
        }
    }
    for (const Track &t : sequence->visualTracks) {
        for (const Clip &c : t.clips) {
            for (const Marker &m : c.markers) {
                const int f = static_cast<int>((c.start + offsetOfKeyframeTime(c, m.time)).rescaled(rate, Rounding::NearestEven).value());
                if (f > currentFrame && f < nextFrame) {
                    nextFrame = f;
                }
            }
        }
    }
    for (const Track &t : sequence->audioTracks) {
        for (const Clip &c : t.clips) {
            for (const Marker &m : c.markers) {
                const int f = static_cast<int>((c.start + offsetOfKeyframeTime(c, m.time)).rescaled(rate, Rounding::NearestEven).value());
                if (f > currentFrame && f < nextFrame) {
                    nextFrame = f;
                }
            }
        }
    }
    if (nextFrame != std::numeric_limits<int>::max() && m_player) {
        m_player->seek(nextFrame);
    }
}

void EditorController::previousMarker()
{
    const Sequence *sequence = data().mainSequence();
    if (!sequence) {
        return;
    }
    const Rational rate = data().settings.frameRate;
    const int currentFrame = playhead();
    int prevFrame = -1;

    for (const Marker &m : sequence->markers) {
        const int f = static_cast<int>(m.time.rescaled(rate, Rounding::NearestEven).value());
        if (f < currentFrame && f > prevFrame) {
            prevFrame = f;
        }
    }
    for (const Track &t : sequence->visualTracks) {
        for (const Clip &c : t.clips) {
            for (const Marker &m : c.markers) {
                const int f = static_cast<int>((c.start + offsetOfKeyframeTime(c, m.time)).rescaled(rate, Rounding::NearestEven).value());
                if (f < currentFrame && f > prevFrame) {
                    prevFrame = f;
                }
            }
        }
    }
    for (const Track &t : sequence->audioTracks) {
        for (const Clip &c : t.clips) {
            for (const Marker &m : c.markers) {
                const int f = static_cast<int>((c.start + offsetOfKeyframeTime(c, m.time)).rescaled(rate, Rounding::NearestEven).value());
                if (f < currentFrame && f > prevFrame) {
                    prevFrame = f;
                }
            }
        }
    }
    if (prevFrame >= 0 && m_player) {
        m_player->seek(prevFrame);
    }
}

bool EditorController::applyAnimation(const QString &animationId, double durationSeconds)
{
    const auto target = clipForLibrary();
    if (!target) {
        return false;
    }
    const Clip *clip = data().findClip(*target);
    if (!clip) {
        return false;
    }
    const Rational rate = data().settings.frameRate;
    ClipAnimations anims = clip->animations;

    const fx::AnimationPreset *preset = fx::Library::core().animation(animationId);
    ClipAnimation anim;
    anim.type = AssetRef{QString::fromLatin1(fx::Library::kCorePack), animationId, preset ? preset->version : 1};
    const double durSec = durationSeconds > 0.05 ? durationSeconds : (preset ? preset->defaultSeconds : 0.5);
    anim.duration = RationalTime::fromSeconds(Rational(static_cast<int64_t>(std::round(durSec * 1000.0)), 1000), rate, Rounding::NearestEven);
    if (preset) {
        const auto easingOpt = Easing::fromName(preset->easing);
        if (easingOpt) {
            anim.easing = *easingOpt;
        }
        anim.params = preset->params;
    }

    QString cat = preset ? preset->category : QString();
    if (cat.isEmpty()) {
        if (animationId.contains(u"/in/") || animationId.startsWith(u"in")) {
            cat = QStringLiteral("in");
        } else if (animationId.contains(u"/out/") || animationId.startsWith(u"out")) {
            cat = QStringLiteral("out");
        } else {
            cat = QStringLiteral("loop");
        }
    }

    if (cat == u"in"_s) {
        anims.in = anim;
    } else if (cat == u"out"_s) {
        anims.out = anim;
    } else {
        anims.loop = anim;
    }

    return push(TimelineEditor(data(), data().mainSequenceId).setClipAnimations(*target, anims));
}

bool EditorController::removeAnimation(const QString &category)
{
    const auto target = clipForLibrary();
    if (!target) {
        return false;
    }
    const Clip *clip = data().findClip(*target);
    if (!clip) {
        return false;
    }
    ClipAnimations anims = clip->animations;
    if (category == u"in"_s) {
        anims.in.reset();
    } else if (category == u"out"_s) {
        anims.out.reset();
    } else if (category == u"loop"_s) {
        anims.loop.reset();
    } else {
        anims.in.reset();
        anims.out.reset();
        anims.loop.reset();
    }
    return push(TimelineEditor(data(), data().mainSequenceId).setClipAnimations(*target, anims));
}

bool EditorController::createCompoundClip(const QString &name)
{
    const auto selected = selectedClips();
    if (selected.empty()) {
        return false;
    }
    return apply(TimelineEditor(data(), data().mainSequenceId).createCompoundClip(selected, name)); // selected
}

bool EditorController::expandCompoundClip(const QString &clipId)
{
    std::optional<ClipId> target;
    if (!clipId.isEmpty()) {
        target = ClipId::fromString(clipId);
    } else {
        target = focusClip();
    }
    if (!target) {
        return false;
    }
    return push(TimelineEditor(data(), data().mainSequenceId).expandCompoundClip(*target));
}

bool EditorController::insertAdjustment(int durationFrames)
{
    const Rational rate = data().settings.frameRate;
    const RationalTime position(playhead(), rate);
    const RationalTime duration(durationFrames > 0 ? durationFrames : 90, rate);
    return apply(TimelineEditor(data(), data().mainSequenceId).insertAdjustment(position, duration)); // selected
}

bool EditorController::syncSelectedClipsByAudio()
{
    const auto selected = selectedClips();
    if (selected.size() < 2) {
        emit message(tr("Select at least 2 clips to synchronize by audio."), false);
        return false;
    }
    const ClipId refId = selected.front();
    std::vector<ClipId> targets(selected.begin() + 1, selected.end());

    auto calcOffset = [this](const Clip &ref, const Clip &target) -> std::optional<double> {
        const MediaClipData *refMedia = ref.media();
        const MediaClipData *tgtMedia = target.media();
        if (!refMedia || !tgtMedia) {
            return std::nullopt;
        }
        const Media *m1 = data().findMedia(refMedia->mediaId);
        const Media *m2 = data().findMedia(tgtMedia->mediaId);
        if (!m1 || !m2) {
            return std::nullopt;
        }
        const auto res = engine::alignAudioFiles(m1->path, m2->path);
        if (res.matched) {
            return res.offsetSeconds;
        }
        return std::nullopt;
    };

    return apply(TimelineEditor(data(), data().mainSequenceId).alignClipsByAudio(refId, targets, calcOffset));
}

bool EditorController::createMulticamFromSelection(const QString &name)
{
    const auto selected = selectedClips();
    if (selected.size() < 2) {
        emit message(tr("Select at least 2 clips to create a multicam clip."), false);
        return false;
    }

    auto calcOffset = [this](const Clip &ref, const Clip &target) -> std::optional<double> {
        const MediaClipData *refMedia = ref.media();
        const MediaClipData *tgtMedia = target.media();
        if (!refMedia || !tgtMedia) {
            return std::nullopt;
        }
        const Media *m1 = data().findMedia(refMedia->mediaId);
        const Media *m2 = data().findMedia(tgtMedia->mediaId);
        if (!m1 || !m2) {
            return std::nullopt;
        }
        const auto res = engine::alignAudioFiles(m1->path, m2->path);
        if (res.matched) {
            return res.offsetSeconds;
        }
        return std::nullopt;
    };

    return apply(TimelineEditor(data(), data().mainSequenceId).createMulticamClip(selected, name, calcOffset));
}

bool EditorController::switchMulticamAngle(int angle)
{
    if (angle < 0) {
        return false;
    }
    std::optional<ClipId> targetClip = focusClip();
    if (targetClip) {
        const Clip *c = data().findClip(*targetClip);
        if (!c || !c->compound()) {
            targetClip.reset();
        }
    }
    const Rational rate = data().settings.frameRate;
    const RationalTime pos(playhead(), rate);
    if (!targetClip) {
        const Sequence *seq = data().findSequence(data().mainSequenceId);
        if (seq) {
            for (const Track &t : seq->visualTracks) {
                for (const Clip &c : t.clips) {
                    if (c.compound() && pos >= c.start && pos < c.end()) {
                        targetClip = c.id;
                        break;
                    }
                }
                if (targetClip) {
                    break;
                }
            }
        }
    }
    if (!targetClip) {
        return false;
    }

    TimelineEditor editor(data(), data().mainSequenceId);
    if (m_player && m_player->playing()) {
        return apply(editor.cutAndSwitchAngle(*targetClip, angle, pos));
    } else {
        const Clip *c = data().findClip(*targetClip);
        if (c && pos > c->start && pos < c->end()) {
            return apply(editor.cutAndSwitchAngle(*targetClip, angle, pos));
        }
        return apply(editor.setMulticamAngle(*targetClip, angle));
    }
}

void EditorController::multicamAngleKey(int number1To9)
{
    if (number1To9 >= 1 && number1To9 <= 9) {
        switchMulticamAngle(number1To9 - 1);
    }
}

RecordController *EditorController::recorder() const
{
    return m_recorder.get();
}

void EditorController::startRecord(int mode)
{
    if (m_recorder) {
        m_recorder->open(mode);
    }
}

} // namespace vedit::ui
