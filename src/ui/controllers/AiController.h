// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/project/Id.h"

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QtQml/qqmlregistration.h>

#include <memory>
#include <vector>

namespace vedit::ai {
class AiTask;
struct SourceRange;
}

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
    Q_INVOKABLE void cancel();

signals:
    void busyChanged();
    void progressChanged();

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

    EditorController &m_editor;
    std::unique_ptr<ai::AiTask> m_task;
    QHash<QString, std::vector<ai::SourceRange>> m_pauses; // by media fingerprint
    QHash<QString, std::vector<double>> m_scenes;
};

} // namespace vedit::ui
