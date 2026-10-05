// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ai/Tasks.h"
#include "ai/Whisper.h"
#include "core/project/Id.h"
#include "core/project/Sequence.h"

#include <QHash>
#include <QObject>
#include <QtQml/qqmlregistration.h>

#include <memory>
#include <vector>

namespace vedit::ui {

class EditorController;

// The one-click AI functions of the editor (SPEC 0bis rule 9, §5.12) on the selected clip (or the one on screen):
// each runs as an ai::AiTask with its progress shown where it was asked for and can be cancelled; its result becomes
// ordinary edits (cuts, splits) in one undo step. Results are remembered per media file for the session.
class AiController : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(AiTools)
    QML_UNCREATABLE("Provided by Editor.ai")

    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged FINAL)
    Q_PROPERTY(QString title READ title NOTIFY busyChanged FINAL)
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged FINAL)
    // Speech recognition (whisper.cpp): 0 = ready, 1 = whisper.cpp not installed, 2 = no speech model downloaded.
    Q_PROPERTY(int speechStatus READ speechStatus NOTIFY speechStatusChanged FINAL)
    Q_PROPERTY(QString speechInstallCommand READ speechInstallCommand CONSTANT FINAL)

public:
    explicit AiController(EditorController &editor);
    ~AiController() override;

    bool busy() const { return m_task != nullptr; }
    QString title() const;
    double progress() const;

    // "Remove pauses": the quiet stretches of the clip's sound are cut out, the rest stays together.
    Q_INVOKABLE bool removePauses();
    // "Split scenes": the clip is cut where its video changes shot.
    Q_INVOKABLE bool splitScenes();
    // Whether the clip under the selection (or the playhead) can be used: a video, or a sound for removePauses.
    Q_INVOKABLE bool canRemovePauses() const;
    Q_INVOKABLE bool canSplitScenes() const;
    // "Stabilize": the camera's shake in the part of the video the clip plays is measured, and the clip gets the
    // "vedit.stabilize" effect (strength in the Video page of the properties, removable).
    Q_INVOKABLE bool stabilize();
    Q_INVOKABLE bool canStabilize() const;
    // "Adapt to 9:16" (any CanvasPreset): the format changes and every video of the main track fills it, following its
    // subject with position keyframes (editable) — one undo step.
    Q_INVOKABLE bool autoReframe(int preset);
    // "Auto captions": the speech of the main track's clips, recognised by whisper.cpp (`language`: ISO 639-1 or
    // "auto"), becomes caption lines word by word on the caption track (replacing the lines there) — one undo step.
    Q_INVOKABLE bool autoCaptions(const QString &language = QStringLiteral("auto"));
    int speechStatus() const;
    QString speechInstallCommand() const;
    // Looks again for whisper.cpp and the models (installed while vedit runs).
    Q_INVOKABLE void refreshSpeech();
    Q_INVOKABLE void cancel();

signals:
    void busyChanged();
    void progressChanged();
    void speechStatusChanged();

private:
    struct Target
    {
        ClipId clip;
        QString path;
        QString fingerprint;
    };
    std::optional<Target> target(bool needsVideo, bool needsAudio) const;
    void run(std::unique_ptr<ai::AiTask> task);
    void applyPauses(const ClipId &clipId, const std::vector<ai::SourceRange> &pauses);
    void applyScenes(const ClipId &clipId, const std::vector<double> &cuts);
    void applyCaptions(const QHash<QString, ai::Transcript> &transcripts);
    void applyReframe(const Canvas &canvas, const std::vector<ClipId> &clips, const std::vector<ai::SubjectTracking::Path> &paths);

    EditorController &m_editor;
    std::unique_ptr<ai::AiTask> m_task;
    QHash<QString, std::vector<ai::SourceRange>> m_pauses; // by media fingerprint
    QHash<QString, std::vector<double>> m_scenes;
};

} // namespace vedit::ui
