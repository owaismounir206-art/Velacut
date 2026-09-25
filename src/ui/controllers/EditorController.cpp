// SPDX-License-Identifier: GPL-3.0-or-later
#include "EditorController.h"

#include "common/Paths.h"
#include "core/edit/ProjectFormat.h"
#include "core/edit/TimelineEditor.h"
#include "document/Document.h"
#include "engine/analysis/MediaAnalysis.h"
#include "engine/analysis/MediaImporter.h"
#include "engine/mlt/MltRuntime.h"
#include "engine/playback/TimelinePlayer.h"
#include "engine/render/RenderJob.h"
#include "ui/models/AudioLibraryModel.h"
#include "ui/models/MediaPoolModel.h"
#include "ui/models/TimelineModel.h"

#include <QDir>
#include <QLocale>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QRegularExpression>

#include <cmath>

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
                                   QString helperExecutable, QObject *parent)
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
    m_player->setSequence(&project, sequence);
    // Back where the user left the project.
    m_player->seek(m_document->uiState().value(u"playhead"_s).toInt());

    m_importer->setExecutable(helperExecutable);
    m_exportJob->setExecutable(helperExecutable);
    connect(m_importer.get(), &engine::MediaImporter::imported, this, &EditorController::onImported);
    connect(m_importer.get(), &engine::MediaImporter::failed, this, [this](const QString &path, const QString &text) {
        m_pendingInserts.removeIf([&path](const PendingInsert &pending) { return pending.path == path; });
        emit message(text, false);
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
    QImage frame = m_player->sink()->latest();
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
}

void EditorController::setSelection(QSet<ClipId> selection)
{
    if (selection == m_selection) {
        return;
    }
    m_selection = std::move(selection);
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
    const auto pending = std::find_if(m_pendingInserts.begin(), m_pendingInserts.end(),
                                      [&imported](const PendingInsert &p) { return p.path == imported.path; });
    if (pending == m_pendingInserts.end()) {
        return;
    }
    const int row = pending->trackRow;
    m_pendingInserts.erase(pending);
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

bool EditorController::addMedia(const QString &mediaId)
{
    const std::optional<MediaId> id = MediaId::fromString(mediaId);
    return id && insertAtRow(*id, playhead(), m_timeline->mainRow());
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
    }
    setSelection(std::move(selection));
}

void EditorController::clearSelection()
{
    setSelection({});
}

void EditorController::undo()
{
    m_document->undoStack().undo();
}

void EditorController::redo()
{
    m_document->undoStack().redo();
}

int EditorController::snap(int frame, const QStringList &excludedClips, int threshold) const
{
    return snapRange(frame, 0, excludedClips, threshold);
}

int EditorController::snapRange(int start, int duration, const QStringList &excludedClips, int threshold) const
{
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

void EditorController::setCanvasPreset(int preset)
{
    const Sequence *sequence = data().mainSequence();
    if (!sequence || preset < 0 || preset > static_cast<int>(CanvasPreset::Portrait3x4)) {
        return;
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
        return;
    }
    const Canvas canvas{dimensions.width(), dimensions.height(), static_cast<CanvasPreset>(preset)};
    if (canvas == sequence->canvas) {
        return;
    }
    EditResult change;
    change.script.push_back(edits::setCanvas(data().mainSequenceId, sequence->canvas, canvas));
    change.text = tr("Change format");
    apply(std::move(change), false);
}

// ---- Export -------------------------------------------------------------------------------------------------------

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
    return {{u"fileName"_s, fileName.trimmed().isEmpty() ? u"video"_s : fileName.trimmed()},
            {u"folder"_s, folder},
            {u"resolutions"_s, resolutions},
            {u"resolution"_s, resolution},
            {u"frameRates"_s, rates},
            {u"frameRate"_s, project.toString()},
            {u"quality"_s, last.contains(u"quality"_s) ? last.value(u"quality"_s).toInt() : 1}};
}

QString EditorController::exportEstimate(int shortSide, const QString &frameRate, int quality) const
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
                                   const QString &frameRate, int quality)
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
    QJsonObject state = m_document->uiState();
    state.insert(u"export"_s, QJsonObject{{u"folder"_s, folder}, {u"quality"_s, quality}});
    m_document->setUiState(state);
    return m_exportJob->start(data(), data().mainSequenceId, settings);
}

QString EditorController::folderPath(const QUrl &url) const
{
    return url.isLocalFile() ? url.toLocalFile() : url.toString();
}

} // namespace vedit::ui
