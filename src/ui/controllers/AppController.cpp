// SPDX-License-Identifier: GPL-3.0-or-later
#include "AppController.h"

#include "ActionRegistry.h"
#include "RecordController.h"
#include "document/Document.h"
#include "document/DraftStore.h"
#include "engine/analysis/MediaAnalysis.h"
#include "engine/render/RenderJob.h"
#include "ui/controllers/EditorController.h"
#include "ui/models/AudioLibraryModel.h"
#include "ui/models/DraftsModel.h"

#include <QClipboard>
#include <QCoreApplication>
#include <QDBusInterface>
#include <QDesktopServices>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJSEngine>
#include <QLoggingCategory>
#include <QSysInfo>

Q_LOGGING_CATEGORY(lcAppController, "vedit.ui.app")

using namespace Qt::StringLiterals;

namespace vedit::ui {

namespace {
AppController *s_instance = nullptr;

} // namespace

AppController::AppController(gpu::GraphicsDecision decision, gpu::GpuCapabilities capabilities, QString helperExecutable,
                             QObject *parent)
    : QObject(parent)
    , m_decision(std::move(decision))
    , m_capabilities(std::move(capabilities))
    , m_helper(helperExecutable.isEmpty() ? QCoreApplication::applicationDirPath() + u"/vedit-render"_s
                                          : std::move(helperExecutable))
    , m_store(std::make_unique<document::DraftStore>(document::DraftStore::defaultRoot()))
    , m_analysis(std::make_unique<engine::MediaAnalysis>(engine::MediaAnalysis::defaultCacheRoot()))
    , m_drafts(std::make_unique<DraftsModel>(*m_store))
    , m_audioLibrary(std::make_unique<AudioLibraryModel>(AudioLibraryModel::defaultFolder(), m_helper))
{
    // Exports killed together with vedit leave their job files in the cache.
    engine::RenderJob::removeStaleJobFiles();
}

AppController::~AppController()
{
    // The editor (players, producers) goes before the analysis service it uses.
    m_editor.reset();
}

void AppController::setInstance(AppController *instance)
{
    s_instance = instance;
}

AppController *AppController::create(QQmlEngine *, QJSEngine *)
{
    Q_ASSERT_X(s_instance, "AppController::create", "AppController::setInstance() must be called before QML loads");
    QJSEngine::setObjectOwnership(s_instance, QJSEngine::CppOwnership);
    return s_instance;
}

QString AppController::version() const
{
    return QCoreApplication::applicationVersion();
}

QString AppController::systemInformation() const
{
    QStringList lines;
    lines << u"vedit %1, Qt %2, %3 (%4)"_s.arg(version(), QString::fromLatin1(qVersion()), QSysInfo::prettyProductName(),
                                             QSysInfo::kernelVersion());
    lines << u"UI backend: %1%2"_s.arg(uiBackend(), m_decision.safeMode ? u" (safe mode)"_s : QString());
    lines << u"GPU effects: %1, hardware decoding: %2, hardware encoding: %3"_s.arg(
        m_decision.gpuEffects ? u"on"_s : u"off"_s, m_decision.hardwareDecoding ? u"on"_s : u"off"_s,
        m_decision.hardwareEncoding ? u"on"_s : u"off"_s);
    for (const QString &reason : m_decision.reasons) {
        lines << u"  - "_s + reason;
    }
    lines << m_capabilities.summary();
    return lines.join(u'\n');
}

void AppController::updateGraphics(const gpu::GraphicsDecision &decision, const gpu::GpuCapabilities &capabilities)
{
    const gpu::UiBackend ui = m_decision.ui;
    m_decision = decision;
    m_decision.ui = ui;
    m_capabilities = capabilities;
    emit systemInformationChanged();
}

bool AppController::newProject()
{
    closeEditor();
    QString error;
    std::unique_ptr<document::Document> document = m_store->createDraft(&error);
    if (!document) {
        emit message(error);
        return false;
    }
    m_editor = std::make_unique<EditorController>(std::move(document), *m_analysis, m_helper);
    m_editor->actions()->setMusicLibrary(m_audioLibrary.get());
    emit editorChanged();
    return true;
}

bool AppController::recordScreen()
{
    if (!newProject()) {
        return false;
    }
    if (m_editor && m_editor->recorder()) {
        m_editor->recorder()->open(1); // Screen mode
    }
    return true;
}

bool AppController::openDraft(const QString &draftId)
{
    const std::optional<ProjectId> id = ProjectId::fromString(draftId);
    if (!id) {
        return false;
    }
    closeEditor();
    QString error;
    std::unique_ptr<document::Document> document = m_store->openDraft(*id, &error);
    if (!document) {
        emit message(error);
        return false;
    }
    const bool recovered = document->recovered();
    m_editor = std::make_unique<EditorController>(std::move(document), *m_analysis, m_helper);
    m_editor->actions()->setMusicLibrary(m_audioLibrary.get());
    emit editorChanged();
    if (recovered) {
        emit message(tr("Project recovered: vedit did not close properly last time. Your latest changes are here."));
    }
    return true;
}

void AppController::closeEditor()
{
    if (!m_editor) {
        return;
    }
    QString error;
    if (!m_editor->close(&error)) {
        emit message(tr("The last changes could not be saved: %1").arg(error));
    }
    // QML still references the editor while this signal is delivered: destroyed afterwards.
    EditorController *closing = m_editor.release();
    emit editorChanged();
    closing->deleteLater();
    m_drafts->refresh();
}

void AppController::copySystemInformation() const
{
    copyText(systemInformation());
}

void AppController::copyText(const QString &text) const
{
    if (QClipboard *clipboard = QGuiApplication::clipboard()) {
        clipboard->setText(text);
    }
}

void AppController::openFolderOf(const QString &path) const
{
    QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
}

void AppController::openFile(const QString &path) const
{
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

void AppController::notify(const QString &title, const QString &body) const
{
    QDBusInterface notifications(u"org.freedesktop.Notifications"_s, u"/org/freedesktop/Notifications"_s,
                                 u"org.freedesktop.Notifications"_s);
    if (!notifications.isValid()) {
        qCInfo(lcAppController) << "no notification service";
        return;
    }
    notifications.call(QDBus::NoBlock, u"Notify"_s, u"vedit"_s, 0u, u"vedit"_s, title, body, QStringList{},
                       QVariantMap{}, 8000);
}

} // namespace vedit::ui
