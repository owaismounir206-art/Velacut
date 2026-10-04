// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/commands/EditCommand.h"
#include "core/project/ChangeSet.h"
#include "core/project/Id.h"
#include "engine/playback/TimelinePlayer.h"
#include "engine/render/RenderJob.h"
#include "ui/models/AudioLibraryModel.h"
#include "ui/models/MediaPoolModel.h"
#include "ui/models/TimelineModel.h"
#include "RecordController.h"

#include <QObject>
#include <QSet>
#include <QSize>
#include <QStringList>
#include <QUrl>
#include <QJsonObject>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <memory>

namespace vedit {
struct EditResult;
struct Media;
struct ProjectData;
}
namespace vedit::document {
class Document;
}
namespace vedit::engine {
class MediaAnalysis;
class MediaImporter;
}

namespace vedit::ui {

class ActionRegistry;
class ClipInspector;

// The open project in the editor: every user action becomes one command on the undo stack (docs/ARCHITECTURE.md §9).
// Times are frames at the project frame rate; timeline rows are those of TimelineModel (-1 = a new track above the
// top one, rowCountTotal = a new audio track below the last one).
class EditorController : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(Editor)
    QML_UNCREATABLE("Provided by App.editor")

    Q_PROPERTY(QString name READ name WRITE setName NOTIFY nameChanged FINAL)
    Q_PROPERTY(int saveState READ saveState NOTIFY saveStateChanged FINAL)
    Q_PROPERTY(QString saveError READ saveError NOTIFY saveStateChanged FINAL)
    Q_PROPERTY(bool recovered READ recovered CONSTANT FINAL)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY undoChanged FINAL)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY undoChanged FINAL)
    Q_PROPERTY(QString undoText READ undoText NOTIFY undoChanged FINAL)
    Q_PROPERTY(QString redoText READ redoText NOTIFY undoChanged FINAL)
    Q_PROPERTY(vedit::engine::TimelinePlayer *player READ player CONSTANT FINAL)
    Q_PROPERTY(vedit::ui::MediaPoolModel *media READ media CONSTANT FINAL)
    Q_PROPERTY(vedit::ui::TimelineModel *timeline READ timeline CONSTANT FINAL)
    Q_PROPERTY(vedit::engine::RenderJob *exportJob READ exportJob CONSTANT FINAL)
    Q_PROPERTY(vedit::ui::ClipInspector *inspector READ inspector CONSTANT FINAL)
    Q_PROPERTY(vedit::ui::ActionRegistry *actions READ actions CONSTANT FINAL)
    Q_PROPERTY(vedit::ui::RecordController *recorder READ recorder CONSTANT FINAL)
    Q_PROPERTY(QStringList selection READ selection NOTIFY selectionChanged FINAL)
    // A transition selected on the timeline, or the cut (its first clip) chosen for a new one; "" = none.
    Q_PROPERTY(QString selectedTransition READ selectedTransition NOTIFY selectionChanged FINAL)
    Q_PROPERTY(QString selectedCut READ selectedCut NOTIFY selectionChanged FINAL)
    Q_PROPERTY(bool importing READ importing NOTIFY importingChanged FINAL)
    Q_PROPERTY(bool splitAvailable READ canSplit NOTIFY splitAvailableChanged FINAL)
    Q_PROPERTY(int canvasPreset READ canvasPreset NOTIFY formatChanged FINAL)
    Q_PROPERTY(QString formatText READ formatText NOTIFY formatChanged FINAL)
    Q_PROPERTY(QSize canvasSize READ canvasSize NOTIFY formatChanged FINAL)
    Q_PROPERTY(double frameRate READ frameRate NOTIFY formatChanged FINAL)
    Q_PROPERTY(bool snappingEnabled READ snappingEnabled WRITE setSnappingEnabled NOTIFY snappingChanged FINAL)
    Q_PROPERTY(bool magneticMain READ magneticMain NOTIFY magneticMainChanged FINAL)
    // Preview axis (SPEC 0bis rule 12): pointing at the timeline shows that frame without moving the playhead.
    // On by default, switched off with a button; remembered with the project.
    Q_PROPERTY(bool skimmingEnabled READ skimmingEnabled WRITE setSkimmingEnabled NOTIFY skimmingChanged FINAL)
    Q_PROPERTY(int inPoint READ inPoint NOTIFY inOutChanged FINAL)
    Q_PROPERTY(int outPoint READ outPoint NOTIFY inOutChanged FINAL)
    Q_PROPERTY(bool hasInOut READ hasInOut NOTIFY inOutChanged FINAL)
    // The cover of the video (SPEC §5.13ter): chosen by the user (a frame or a picture), shown at the head of the main
    // track and on the draft; empty URL = none chosen (the draft then shows the frame where the editing stopped).
    Q_PROPERTY(QUrl coverUrl READ coverUrl NOTIFY coverChanged FINAL)
    // Template slots still waiting for media (SPEC §5.13): the interface invites to fill them.
    Q_PROPERTY(int placeholderCount READ placeholderCount NOTIFY modelChanged FINAL)
    // Set when the project was just made from a template: the interface asks at once for the media of its slots.
    Q_PROPERTY(bool askForTemplateMedia MEMBER m_askForTemplateMedia NOTIFY askForTemplateMediaChanged FINAL)

public:
    enum SaveState
    {
        Saved,
        Saving,
        SaveFailed,
    };
    Q_ENUM(SaveState)

    // `previewLimit`: short side the preview frames are capped to (0 = canvas size); see TimelinePlayer.
    EditorController(std::unique_ptr<document::Document> document, engine::MediaAnalysis &analysis,
                     QString helperExecutable, int previewLimit = 0, QObject *parent = nullptr);
    ~EditorController() override;

    document::Document &document() { return *m_document; }
    const ProjectData &data() const;
    const Media *findMedia(const QString &mediaId) const;
    engine::MediaAnalysis &analysis() { return m_analysis; }

    QString name() const;
    void setName(const QString &name);
    int saveState() const;
    QString saveError() const;
    bool recovered() const;
    bool canUndo() const;
    bool canRedo() const;
    QString undoText() const;
    QString redoText() const;
    engine::TimelinePlayer *player() const { return m_player.get(); }
    MediaPoolModel *media() const { return m_media.get(); }
    TimelineModel *timeline() const { return m_timeline.get(); }
    engine::RenderJob *exportJob() const { return m_exportJob.get(); }
    ClipInspector *inspector() const { return m_inspector; }
    ActionRegistry *actions() const { return m_actions; }
    RecordController *recorder() const;
    Q_INVOKABLE void startRecord(int mode = 0);
    QStringList selection() const;
    std::vector<ClipId> selectedClips() const;
    // The clip whose properties are shown: the last one clicked among the selected ones.
    std::optional<ClipId> focusClip() const;
    QString selectedTransition() const;
    QString selectedCut() const;
    std::optional<TransitionId> focusTransition() const;
    // Where a transition of the library goes: the selected transition or cut, else the cut after (or before) the
    // selected clip, else the cut of the main track nearest to the playhead.
    std::optional<ClipId> transitionTarget() const;
    // No clip selected: the clip under the playhead (main track first) becomes the selection, so that a filter or an
    // adjustment applies "to what is on screen". False if there is none.
    bool selectClipAtPlayhead();
    // The focused clip, else the one under the playhead (main track first), without selecting it.
    std::optional<ClipId> clipForLibrary() const;
    // Pushes a command (a failed operation becomes a message for the user). Same non-empty key = one undo step.
    bool push(EditResult result, MergeKey mergeKey = {});
    bool importing() const;
    int canvasPreset() const;
    QString formatText() const;
    QSize canvasSize() const;
    double frameRate() const;

    // Media
    Q_INVOKABLE void importFiles(const QList<QUrl> &urls);
    void importPaths(const QStringList &paths);
    // Files dropped on the timeline: imported, then placed one after the other from `frame` on `trackRow`.
    Q_INVOKABLE void importAndInsert(const QList<QUrl> &urls, int frame, int trackRow);
    void importAndInsertPaths(const QStringList &paths, int frame, int trackRow);
    // "+" of a media item: at the playhead, on the right track.
    Q_INVOKABLE bool addMedia(const QString &mediaId);
    // "+" of the music library: under the video, at the playhead.
    Q_INVOKABLE bool addFromLibrary(vedit::ui::AudioLibraryModel *library, int row);
    // A media item dropped on the timeline.
    Q_INVOKABLE bool insertMedia(const QString &mediaId, int frame, int trackRow);
    // "Replace" (SPEC §5.2, §5.13): the clip shows another video or photo, keeping its place, length and look; a
    // template slot becomes a normal clip. From the media pool, or from a file (imported first).
    Q_INVOKABLE bool replaceClip(const QString &clipId, const QString &mediaId);
    Q_INVOKABLE void replaceClipWithFile(const QString &clipId, const QUrl &file);
    // The files go into the template slots in timeline order (one file each); files left over go after the video,
    // music under it.
    Q_INVOKABLE void fillPlaceholders(const QList<QUrl> &files);
    int placeholderCount() const;
    // The first slot waiting for media, in timeline order ("" = none).
    Q_INVOKABLE QString firstPlaceholder() const;
    // Builds the project of a template (SPEC §5.13; the spec of fx::TemplatePreset, docs/EFFECT_FORMAT.md): its format,
    // a placeholder for each shot, the transition on every cut, the filter on every shot, its titles and stickers. Not
    // undoable (it is how the project starts). False if the template has no shots.
    bool applyTemplate(const QJsonObject &spec, const QString &name);
    void setAskForTemplateMedia(bool ask)
    {
        m_askForTemplateMedia = ask;
        emit askForTemplateMediaChanged();
    }

    // Timeline
    Q_INVOKABLE bool moveClip(const QString &clipId, int frame, int trackRow);
    Q_INVOKABLE bool trimClip(const QString &clipId, bool startEdge, int frame);
    bool canSplit() const;
    // The selected clips at the playhead, or else the clip under the playhead (main track first).
    Q_INVOKABLE bool split();
    bool canRippleTrimLeft() const;
    bool canRippleTrimRight() const;
    // Ripple trim from clip start to playhead (Q) and playhead to clip end (W)
    Q_INVOKABLE bool rippleTrimLeft();
    Q_INVOKABLE bool rippleTrimRight();
    Q_INVOKABLE bool deleteSelection();
    Q_INVOKABLE bool rippleDeleteSelection();
    Q_INVOKABLE bool trimSelectedToPlayhead(bool startEdge);
    Q_INVOKABLE bool duplicateSelection();
    Q_INVOKABLE void select(const QString &clipId, bool additive);
    Q_INVOKABLE void clearSelection();
    // Every clip of the unlocked tracks (Ctrl+A).
    Q_INVOKABLE void selectAll();
    Q_INVOKABLE void selectTransition(const QString &transitionId);
    // A cut without a transition clicked on the timeline: the Transitions library opens for it.
    Q_INVOKABLE void selectCut(const QString &fromClipId);
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    // Snapping: the nearest clip edge, playhead or start within `threshold` frames of `frame` (else `frame`).
    bool snappingEnabled() const { return m_snappingEnabled; }
    void setSnappingEnabled(bool enabled);
    Q_INVOKABLE void toggleSnapping();
    bool skimmingEnabled() const;
    void setSkimmingEnabled(bool enabled);
    Q_INVOKABLE void toggleSkimming();
    // Sizes of the resizable panels (library and properties width, timeline height), remembered with the project
    // (SPEC §4 "layout ricordato per progetto"). `fallback` when the project has none.
    Q_INVOKABLE double panelSize(const QString &key, double fallback) const;
    Q_INVOKABLE void setPanelSize(const QString &key, double size);
    bool magneticMain() const;
    Q_INVOKABLE bool setMagneticMain(bool enabled);
    Q_INVOKABLE bool toggleMagneticMain();
    int inPoint() const;
    int outPoint() const;
    bool hasInOut() const;
    Q_INVOKABLE void setInPoint(int frame = -1);
    Q_INVOKABLE void setOutPoint(int frame = -1);
    Q_INVOKABLE void clearInOut();
    Q_INVOKABLE void nextCut();
    Q_INVOKABLE void previousCut();
    Q_INVOKABLE int snap(int frame, const QStringList &excludedClips, int threshold) const;
    // Start of a range of `duration` frames whose start or end snaps (for dragging clips).
    Q_INVOKABLE int snapRange(int start, int duration, const QStringList &excludedClips, int threshold) const;

    // A text at the playhead with the default style, or a style of the library, selected for editing.
    Q_INVOKABLE bool addText(const QString &styleId = {});
    // A sticker of the library at the playhead, on a sticker track (a visualizer lasts until the end of the video).
    Q_INVOKABLE bool addSticker(const QString &assetId);
    // The user's pictures (PNG, SVG, WebP, GIF…) imported and added as stickers at the playhead.
    Q_INVOKABLE void importStickers(const QList<QUrl> &urls);
    // "Beat" (SPEC 0bis rule 3): the beats of the focused clip's audio become its beat markers (in background).
    Q_INVOKABLE void detectBeats();
    // Effects on the beat need beats: when the video has none, those of the first music are found (in background).
    void ensureBeats();
    // An effect of the library on its own layer at the playhead (3 s): it acts on everything under it.
    Q_INVOKABLE bool addEffectLayer(const QString &effectId);

    // Mixer (SPEC §5.9): the volume of a whole track in dB (a slider drag is one undo step until endTrackGesture()),
    // mute.
    Q_INVOKABLE bool setTrackVolume(const QString &trackId, double gainDb);
    Q_INVOKABLE void endTrackGesture();
    Q_INVOKABLE bool setTrackMuted(const QString &trackId, bool muted);
    Q_INVOKABLE bool setTrackLocked(const QString &trackId, bool locked);
    Q_INVOKABLE bool setTrackHidden(const QString &trackId, bool hidden);
    Q_INVOKABLE bool setTrackSolo(const QString &trackId, bool solo);

    // Freeze frame (SPEC §5.5): the frame at the playhead of the clip on screen, held for 3 s; the rest moves along.
    Q_INVOKABLE bool freezeFrame();

    // Markers & Animations (Phase 3)
    Q_INVOKABLE bool addMarker(const QString &name = {}, const QString &color = {}, const QString &note = {});
    Q_INVOKABLE bool addSequenceMarker(int frame, const QString &name = {}, const QString &color = {}, const QString &note = {});
    Q_INVOKABLE bool removeSequenceMarker(const QString &markerId);
    Q_INVOKABLE bool addClipMarker(const QString &clipId, int frameOffset, const QString &name = {}, const QString &color = {}, const QString &note = {});
    Q_INVOKABLE bool removeClipMarker(const QString &clipId, const QString &markerId);
    Q_INVOKABLE void nextMarker();
    Q_INVOKABLE void previousMarker();
    Q_INVOKABLE bool applyAnimation(const QString &animationId, double durationSeconds = 0.5);
    Q_INVOKABLE bool removeAnimation(const QString &category = {});

    // Compound clip and adjustment layer (Phase 3)
    Q_INVOKABLE bool createCompoundClip(const QString &name = {});
    Q_INVOKABLE bool expandCompoundClip(const QString &clipId = {});
    Q_INVOKABLE bool insertAdjustment(int durationFrames = 90);

    // Multicam and audio sync (Phase 4)
    Q_INVOKABLE bool syncSelectedClipsByAudio();
    Q_INVOKABLE bool createMulticamFromSelection(const QString &name = {});
    Q_INVOKABLE bool switchMulticamAngle(int angle);
    Q_INVOKABLE void multicamAngleKey(int number1To9);

    // Format (SPEC 0bis rule 1: changeable with one click)
    Q_INVOKABLE void setCanvasPreset(int preset);

    // Export (one screen, SPEC §5.15)
    Q_INVOKABLE QVariantMap exportDefaults() const;
    Q_INVOKABLE QString exportEstimate(int shortSide, const QString &frameRate, int quality, const QString &codec = QStringLiteral("h264"),
                                       int maxFileSizeMB = 0) const;
    Q_INVOKABLE bool startExport(const QString &fileName, const QString &folder, int shortSide, const QString &frameRate,
                                 int quality, bool normalizeLoudness = false, double targetLufs = -14.0,
                                 const QString &codec = QStringLiteral("h264"), bool hardware = true,
                                 int maxFileSizeMB = 0);
    Q_INVOKABLE QString folderPath(const QUrl &url) const;
    // Hardware encoders verified by the probe and the name of the GPU that has them (shown in the export window).
    void setHardwareEncoding(QStringList encoders, QString gpuName);
    // Saves the frame on screen as an image (SPEC §5.15 "esporta fotogramma corrente"; also the basis of the
    // video cover). Never overwrites: "name (2).png". Returns the path written, or an empty string.
    Q_INVOKABLE QString exportCurrentFrame(const QString &fileName, const QString &folder);
    Q_INVOKABLE bool setCoverFromCurrentFrame();
    Q_INVOKABLE bool setCoverFromImage(const QUrl &file);
    Q_INVOKABLE void clearCover();
    QUrl coverUrl() const;
    // The cover as a picture file in `folder`: PNG at its own size, or JPG 1280×720 (YouTube thumbnail, the picture
    // fitted on a blurred copy of itself). Never overwrites. Returns the path written, or an empty string.
    Q_INVOKABLE QString exportCover(const QString &folder, bool youtube);
    // The path of the cover picture in the draft ("" = none): attached to the exported MP4.
    QString coverPath() const;

    // Saves at once (the window lost focus); saving is otherwise automatic.
    Q_INVOKABLE void saveNow();
    // Writes everything (and the draft's thumbnail) before the editor closes.
    bool close(QString *error = nullptr);

signals:
    void nameChanged();
    void saveStateChanged();
    void undoChanged();
    void selectionChanged();
    void importingChanged();
    void splitAvailableChanged();
    void formatChanged();
    void snappingChanged();
    void magneticMainChanged();
    void skimmingChanged();
    void inOutChanged();
    void coverChanged();
    void askForTemplateMediaChanged();
    // The project changed (any command, undo or redo).
    void modelChanged();
    // For the snackbar: `undoable` shows the "Undo" action.
    void message(const QString &text, bool undoable);
    void exportFinished(const QString &path);
    // Asks the interface to show a library ("transitions", "filters", "text").
    void libraryRequested(const QString &name);
    // Asks the interface to show a page of the properties panel ("speed", "audio", "text"…).
    void propertiesRequested(const QString &page);
    void exportRequested();
    void importRequested();
    // Asks the interface for a video or photo to put in this clip instead ("Replace").
    void replaceRequested(const QString &clipId);

private:
    bool apply(EditResult result, bool selectResult = true);
    void detectBeatsOf(const ClipId &clipId);
    void onProjectChanged(const ChangeSet &changes);
    void onImported(const Media &media);
    void setSelection(QSet<ClipId> selection);
    int playhead() const;
    std::optional<ClipId> clipAtPlayhead() const;
    std::vector<ClipId> splitTargets() const;
    std::optional<ClipId> rippleTrimTarget() const;
    std::optional<ClipId> insertAtRow(const MediaId &mediaId, int frame, int trackRow);

    std::unique_ptr<document::Document> m_document;
    engine::MediaAnalysis &m_analysis;
    std::unique_ptr<engine::TimelinePlayer> m_player;
    std::unique_ptr<MediaPoolModel> m_media;
    std::unique_ptr<TimelineModel> m_timeline;
    std::unique_ptr<engine::MediaImporter> m_importer;
    std::unique_ptr<engine::RenderJob> m_exportJob;
    QStringList m_hardwareEncoders;
    QString m_gpuName;
    std::unique_ptr<RecordController> m_recorder;
    QSet<ClipId> m_selection;
    ClipId m_focus;
    TransitionId m_transition;
    ClipId m_cut;
    ClipInspector *m_inspector = nullptr; // child
    quint64 m_trackGesture = 1;
    ActionRegistry *m_actions = nullptr;  // child
    quint64 m_importBatch = 0;
    // Files dropped on the timeline, inserted as they are imported.
    struct PendingInsert
    {
        QString path;
        int trackRow = 0;
        bool sticker = false; // added as a sticker (importStickers)
        ClipId replace{};     // replaces this clip's media instead of being inserted (replaceClipWithFile)
    };
    std::vector<ClipId> placeholders() const;
    QList<PendingInsert> m_pendingInserts;
    int m_insertStart = 0;  // where the files were dropped: music starts here, under the video
    int m_insertCursor = 0; // after the last video or photo placed
    bool m_snappingEnabled = true;
    int m_inPoint = -1;
    int m_outPoint = -1;
    int m_coverSerial = 0; // cache-busting part of coverUrl
    bool m_askForTemplateMedia = false;
    bool m_closed = false;
    bool setCover(const QImage &image);
};

} // namespace vedit::ui
