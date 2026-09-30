// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "engine/gpu/GraphicsSetup.h"
#include "ui/controllers/EditorController.h"
#include "ui/models/AudioLibraryModel.h"
#include "ui/models/DraftsModel.h"

#include <QObject>
#include <QStringList>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

#include <memory>

class QQmlEngine;
class QJSEngine;

namespace vedit::document {
class DraftStore;
}
namespace vedit::engine {
class MediaAnalysis;
}

namespace vedit::ui {

// The QML singleton `App` (import Vedit.UI): home screen (drafts), the open editor, application services.
class AppController : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(App)
    QML_SINGLETON

    Q_PROPERTY(vedit::ui::DraftsModel *drafts READ drafts CONSTANT FINAL)
    Q_PROPERTY(vedit::ui::AudioLibraryModel *audioLibrary READ audioLibrary CONSTANT FINAL)
    Q_PROPERTY(vedit::ui::EditorController *editor READ editor NOTIFY editorChanged FINAL)
    Q_PROPERTY(QString version READ version CONSTANT FINAL)
    Q_PROPERTY(QString uiBackend READ uiBackend CONSTANT FINAL)
    Q_PROPERTY(bool softwareRendering READ softwareRendering CONSTANT FINAL)
    Q_PROPERTY(bool safeMode READ safeMode CONSTANT FINAL)
    Q_PROPERTY(QStringList graphicsReasons READ graphicsReasons CONSTANT FINAL)
    Q_PROPERTY(QString systemInformation READ systemInformation NOTIFY systemInformationChanged FINAL)

public:
    // `helperExecutable`: vedit-render (default: next to the running executable).
    AppController(gpu::GraphicsDecision decision, gpu::GpuCapabilities capabilities, QString helperExecutable = {},
                  QObject *parent = nullptr);
    ~AppController() override;

    static void setInstance(AppController *instance);
    static AppController *create(QQmlEngine *qmlEngine, QJSEngine *jsEngine);

    DraftsModel *drafts() const { return m_drafts.get(); }
    AudioLibraryModel *audioLibrary() const { return m_audioLibrary.get(); }
    EditorController *editor() const { return m_editor.get(); }
    document::DraftStore &draftStore() { return *m_store; }
    QString version() const;
    QString uiBackend() const { return gpu::uiBackendName(m_decision.ui); }
    bool softwareRendering() const { return m_decision.ui == gpu::UiBackend::Software; }
    bool safeMode() const { return m_decision.safeMode; }
    QStringList graphicsReasons() const { return m_decision.reasons; }
    QString systemInformation() const;
    // Updated when the (slower) video probe finishes in background; the UI backend never changes at runtime.
    void updateGraphics(const gpu::GraphicsDecision &decision, const gpu::GpuCapabilities &capabilities);

    // "New project": opens the editor at once, no questions (SPEC 0bis rule 1).
    Q_INVOKABLE bool newProject();
    // Creates a project from a template (SPEC §5.13): the sequence has placeholder clips ready to be filled.
    Q_INVOKABLE bool newProjectFromTemplate(const QString &templateId);
    Q_INVOKABLE bool recordScreen();
    Q_INVOKABLE bool openDraft(const QString &draftId);
    // Back to the home screen (everything is already saved; this writes the last changes).
    Q_INVOKABLE void closeEditor();

    // "Copia informazioni di sistema" (SPEC 1bis rule 6).
    Q_INVOKABLE void copySystemInformation() const;
    Q_INVOKABLE void copyText(const QString &text) const;
    Q_INVOKABLE QString localPath(const QUrl &url) const { return url.toLocalFile(); }
    Q_INVOKABLE QUrl fileUrl(const QString &path) const { return QUrl::fromLocalFile(path); }
    Q_INVOKABLE void openFolderOf(const QString &path) const;
    Q_INVOKABLE void openFile(const QString &path) const;
    // A desktop notification (org.freedesktop.Notifications), e.g. when an export ends while vedit is in background.
    Q_INVOKABLE void notify(const QString &title, const QString &body) const;

signals:
    void editorChanged();
    void systemInformationChanged();
    // For the snackbar.
    void message(const QString &text);

private:
    gpu::GraphicsDecision m_decision;
    gpu::GpuCapabilities m_capabilities;
    QString m_helper;
    std::unique_ptr<document::DraftStore> m_store;
    std::unique_ptr<engine::MediaAnalysis> m_analysis;
    std::unique_ptr<DraftsModel> m_drafts;
    std::unique_ptr<AudioLibraryModel> m_audioLibrary;
    std::unique_ptr<EditorController> m_editor;
};

} // namespace vedit::ui
