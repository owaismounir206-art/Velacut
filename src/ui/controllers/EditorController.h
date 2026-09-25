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

#include <QObject>
#include <QSet>
#include <QSize>
#include <QStringList>
#include <QUrl>
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

public:
    enum SaveState
    {
        Saved,
        Saving,
        SaveFailed,
    };
    Q_ENUM(SaveState)

    EditorController(std::unique_ptr<document::Document> document, engine::MediaAnalysis &analysis,
                     QString helperExecutable, QObject *parent = nullptr);
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

    // Timeline
    Q_INVOKABLE bool moveClip(const QString &clipId, int frame, int trackRow);
    Q_INVOKABLE bool trimClip(const QString &clipId, bool startEdge, int frame);
    bool canSplit() const;
    // The selected clips at the playhead, or else the clip under the playhead (main track first).
    Q_INVOKABLE bool split();
    Q_INVOKABLE bool deleteSelection();
    Q_INVOKABLE bool duplicateSelection();
    Q_INVOKABLE void select(const QString &clipId, bool additive);
    Q_INVOKABLE void clearSelection();
    Q_INVOKABLE void selectTransition(const QString &transitionId);
    // A cut without a transition clicked on the timeline: the Transitions library opens for it.
    Q_INVOKABLE void selectCut(const QString &fromClipId);
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    // Snapping: the nearest clip edge, playhead or start within `threshold` frames of `frame` (else `frame`).
    Q_INVOKABLE int snap(int frame, const QStringList &excludedClips, int threshold) const;
    // Start of a range of `duration` frames whose start or end snaps (for dragging clips).
    Q_INVOKABLE int snapRange(int start, int duration, const QStringList &excludedClips, int threshold) const;

    // A text at the playhead with the default style, or a style of the library, selected for editing.
    Q_INVOKABLE bool addText(const QString &styleId = {});

    // Format (SPEC 0bis rule 1: changeable with one click)
    Q_INVOKABLE void setCanvasPreset(int preset);

    // Export (one screen, SPEC §5.15)
    Q_INVOKABLE QVariantMap exportDefaults() const;
    Q_INVOKABLE QString exportEstimate(int shortSide, const QString &frameRate, int quality) const;
    Q_INVOKABLE bool startExport(const QString &fileName, const QString &folder, int shortSide, const QString &frameRate,
                                 int quality);
    Q_INVOKABLE QString folderPath(const QUrl &url) const;

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
    // The project changed (any command, undo or redo).
    void modelChanged();
    // For the snackbar: `undoable` shows the "Undo" action.
    void message(const QString &text, bool undoable);
    void exportFinished(const QString &path);
    // Asks the interface to show a library ("transitions", "filters", "text").
    void libraryRequested(const QString &name);

private:
    bool apply(EditResult result, bool selectResult = true);
    void onProjectChanged(const ChangeSet &changes);
    void onImported(const Media &media);
    void setSelection(QSet<ClipId> selection);
    int playhead() const;
    std::optional<ClipId> clipAtPlayhead() const;
    std::vector<ClipId> splitTargets() const;
    std::optional<ClipId> insertAtRow(const MediaId &mediaId, int frame, int trackRow);

    std::unique_ptr<document::Document> m_document;
    engine::MediaAnalysis &m_analysis;
    std::unique_ptr<engine::TimelinePlayer> m_player;
    std::unique_ptr<MediaPoolModel> m_media;
    std::unique_ptr<TimelineModel> m_timeline;
    std::unique_ptr<engine::MediaImporter> m_importer;
    std::unique_ptr<engine::RenderJob> m_exportJob;
    QSet<ClipId> m_selection;
    ClipId m_focus;
    TransitionId m_transition;
    ClipId m_cut;
    ClipInspector *m_inspector = nullptr; // child
    quint64 m_importBatch = 0;
    // Files dropped on the timeline, inserted as they are imported.
    struct PendingInsert
    {
        QString path;
        int trackRow = 0;
    };
    QList<PendingInsert> m_pendingInserts;
    int m_insertStart = 0;  // where the files were dropped: music starts here, under the video
    int m_insertCursor = 0; // after the last video or photo placed
    bool m_closed = false;
};

} // namespace vedit::ui
