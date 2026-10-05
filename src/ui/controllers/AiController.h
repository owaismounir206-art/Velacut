// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ai/Separation.h"
#include "ai/Speech.h"
#include "ai/Tasks.h"
#include "ai/Whisper.h"
#include "core/project/Id.h"
#include "core/project/Media.h"
#include "core/project/Sequence.h"

#include <QHash>
#include <QObject>
#include <QtQml/qqmlregistration.h>

#include <functional>
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
    // Text to speech can be used: Piper installed and a voice added (the name of the first voice, "" otherwise).
    Q_PROPERTY(QString voiceName READ voiceName NOTIFY speechStatusChanged FINAL)

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
    // Captions from a script (SPEC §5.8): the text as written, each word at the time it is said (the speech of the main
    // track recognised first when needed).
    Q_INVOKABLE bool captionsFromScript(const QString &script, const QString &language = QStringLiteral("auto"));
    // "Separate voice and music" (Demucs): the clip's sound is muted and its voice and its music come under it as
    // two sounds of their own (each with its own volume) — one undo step.
    Q_INVOKABLE bool separateVoice();
    Q_INVOKABLE bool canSeparateVoice() const;
    // "Remove background" (rembg): the subject of the video clip on a transparent background (what is under it shows),
    // undoable, switched off in Cutout. The first time, rembg downloads its model: a first click explains it.
    Q_INVOKABLE bool removeBackground();
    Q_INVOKABLE bool canRemoveBackground() const;
    // "Track" (motion tracking): the selected text or sticker follows what is under it in the video (position
    // keyframes, editable).
    Q_INVOKABLE bool trackMotion();
    Q_INVOKABLE bool canTrackMotion() const;
    // "Read aloud" (Piper): the selected text, spoken with the first voice added, as a sound under it.
    Q_INVOKABLE bool readAloud();
    Q_INVOKABLE bool canReadAloud() const;
    QString voiceName() const;
    // Speech for each text, one after the other (the busy pill shows it); `done` gets the files ("" where it failed).
    bool synthesizeAll(const QStringList &texts, std::function<void(const QStringList &)> done);
    // "Transcribe": the speech of the main track's clips, for editing by the transcript (Editor.transcript).
    Q_INVOKABLE bool transcribe(const QString &language = QStringLiteral("auto"));
    // The transcript of a media file made in this session or found in the cache (best model first), if any.
    const ai::Transcript *transcriptOf(const Media &media) const;
    int speechStatus() const;
    QString speechInstallCommand() const;
    // Looks again for whisper.cpp and the models (installed while vedit runs).
    Q_INVOKABLE void refreshSpeech();
    Q_INVOKABLE void cancel();

signals:
    void busyChanged();
    void progressChanged();
    void speechStatusChanged();
    void transcriptsChanged();
    // Captions were made from the speech (the Captions tab shows its styles next).
    void captionsMade();

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
    // New caption lines on the caption track (replacing its lines), in the chosen style or an animated one.
    void placeCaptions(const std::vector<captions::CaptionLine> &lines);
    // Recognises the speech of the main track (what is not known yet), then calls `then` with every transcript.
    bool startTranscription(const QString &language, std::function<void(const QHash<QString, ai::Transcript> &)> then);
    void applyReframe(const Canvas &canvas, const std::vector<ClipId> &clips, const std::vector<ai::SubjectTracking::Path> &paths);

    EditorController &m_editor;
    std::unique_ptr<ai::AiTask> m_task;
    QHash<QString, std::vector<ai::SourceRange>> m_pauses; // by media fingerprint
    QHash<QString, std::vector<double>> m_scenes;
    mutable QHash<QString, ai::Transcript> m_transcripts; // by fingerprint (also those read from the cache)
    bool m_rembgDownloadExplained = false;
    void synthesizeNext(QStringList texts, QStringList done, std::function<void(const QStringList &)> finished);
};

} // namespace vedit::ui
