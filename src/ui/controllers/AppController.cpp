// SPDX-License-Identifier: GPL-3.0-or-later
#include "AppController.h"

#include "ai/Speech.h"

#include "ActionRegistry.h"
#include "RecordController.h"
#include "common/Paths.h"
#include "document/Document.h"
#include "document/DraftStore.h"
#include "engine/analysis/MediaAnalysis.h"
#include "engine/render/RenderJob.h"
#include "fx/Library.h"
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
#include <QProcess>
#include <QSettings>
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
    , m_brandKits(std::make_unique<BrandKitModel>())
{
    // Exports killed together with vedit leave their job files in the cache.
    engine::RenderJob::removeStaleJobFiles();
    BrandKitModel::setInstance(m_brandKits.get());
}

AppController::~AppController()
{
    // The editor (players, producers) goes before the analysis service it uses.
    m_editor.reset();
    if (BrandKitModel::instance() == m_brandKits.get()) {
        BrandKitModel::setInstance(nullptr);
    }
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
    if (m_editor) {
        m_editor->setHardwareEncoding(hardwareEncoders(), gpuDisplayName());
    }
    emit systemInformationChanged();
}

// The encoders the renderer may use: only those the probe verified, and only when the decision allows hardware.
QStringList AppController::hardwareEncoders() const
{
    return m_decision.hardwareEncoding ? m_capabilities.video.encoders : QStringList{};
}

QString AppController::gpuDisplayName() const
{
    // The name shown next to "Use hardware acceleration" in the export window.
    if (!m_capabilities.vulkan.devices.empty()) {
        return m_capabilities.vulkan.devices.front().name;
    }
    if (!m_capabilities.opengl.renderer.isEmpty()) {
        return m_capabilities.opengl.renderer;
    }
    return tr("GPU");
}

QString AppController::videosFolder() const
{
    return paths::videosDir();
}

void AppController::makeEditor(std::unique_ptr<document::Document> document)
{
    m_editor = std::make_unique<EditorController>(std::move(document), *m_analysis, m_helper, previewLimit());
    m_editor->actions()->setMusicLibrary(m_audioLibrary.get());
    m_editor->setHardwareEncoding(hardwareEncoders(), gpuDisplayName());
}

// How big the preview frames may be (SPEC 1bis rules 8 and 9). The export always renders at full size; only the
// frames on screen get smaller, and only when the machine pays for every pixel: a shared-memory iGPU
// (Radeon 740M, Intel Arc 130V), very little video memory, or software rendering.
int AppController::previewLimit() const
{
    if (m_decision.lowVideoMemory) {
        return 540;
    }
    if (m_decision.integratedGpu || m_decision.ui == gpu::UiBackend::Software || m_decision.safeMode) {
        return 1080;
    }
    return 0; // a discrete GPU renders the canvas as it is
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
    makeEditor(std::move(document));
    emit editorChanged();
    return true;
}

bool AppController::newProjectFromTemplate(const QString &templateId)
{
    closeEditor();
    QString error;
    std::unique_ptr<document::Document> document = m_store->createDraft(&error);
    if (!document) {
        emit message(error);
        return false;
    }

    const fx::TemplatePreset *preset = fx::Library::core().templatePreset(templateId);
    if (!preset) {
        emit message(tr("Template not found."));
        return false;
    }
    makeEditor(std::move(document));
    if (!m_editor->applyTemplate(preset->spec, preset->name.text())) {
        emit message(tr("This template cannot be used."));
    }
    // The next step is the user's media: the editor asks for them at once (SPEC 0bis: fewest actions).
    m_editor->setAskForTemplateMedia(m_editor->placeholderCount() > 0);
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

bool AppController::newProjectWithFiles(const QList<QUrl> &files)
{
    QList<QUrl> local;
    for (const QUrl &url : files) {
        if (url.isLocalFile()) {
            local.append(url);
        }
    }
    if (local.isEmpty() || !newProject()) {
        return false;
    }
    m_editor->importAndInsert(local, 0, m_editor->timeline()->mainRow());
    return true;
}

bool AppController::newSlideshow(const QList<QUrl> &photos, const QUrl &music, int style, bool onBeat)
{
    if (photos.isEmpty() || !newProject()) {
        return false;
    }
    m_editor->buildSlideshow(photos, music, style, onBeat);
    return true;
}

bool AppController::newMontage(const QList<QUrl> &files, const QUrl &music, const QString &style, int seconds)
{
    if (files.isEmpty() || !newProject()) {
        return false;
    }
    m_editor->buildMontage(files, music, style, seconds);
    return true;
}

bool AppController::newFromScript(const QString &script, int preset, const QUrl &music)
{
    if (script.trimmed().isEmpty() || !newProject()) {
        return false;
    }
    m_editor->buildFromScript(script, preset, music);
    return true;
}

QString AppController::voiceName() const
{
    const QStringList voices = ai::piper::voices();
    return voices.isEmpty() || ai::piper::executable().isEmpty() ? QString() : ai::piper::voiceName(voices.front());
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
    makeEditor(std::move(document));
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

namespace vedit::ui {

QString AppController::savedLanguage()
{
    return QSettings().value(u"ui/language"_s, u"auto"_s).toString();
}

QString AppController::language() const
{
    return savedLanguage();
}

void AppController::setLanguage(const QString &language)
{
    if (language == savedLanguage() || (language != u"auto"_s && language != u"it"_s && language != u"en"_s)) {
        return;
    }
    QSettings().setValue(u"ui/language"_s, language);
    m_restartNeeded = true;
    emit preferencesChanged();
}

void AppController::changeGraphics(const std::function<void(gpu::GraphicsPreferences &)> &change)
{
    gpu::GraphicsPreferences preferences = gpu::GraphicsPreferences::load();
    const gpu::GraphicsPreferences before = preferences;
    change(preferences);
    if (preferences == before) {
        return;
    }
    preferences.save();
    m_restartNeeded = true;
    emit preferencesChanged();
}

int AppController::uiBackendChoice() const
{
    return static_cast<int>(gpu::GraphicsPreferences::load().ui);
}

void AppController::setUiBackendChoice(int choice)
{
    if (choice < 0 || choice > static_cast<int>(gpu::UiBackendChoice::Software)) {
        return;
    }
    changeGraphics([choice](gpu::GraphicsPreferences &p) { p.ui = static_cast<gpu::UiBackendChoice>(choice); });
}

bool AppController::gpuEffects() const
{
    return gpu::GraphicsPreferences::load().gpuEffects;
}

void AppController::setGpuEffects(bool enabled)
{
    changeGraphics([enabled](gpu::GraphicsPreferences &p) { p.gpuEffects = enabled; });
}

bool AppController::hardwareDecoding() const
{
    return gpu::GraphicsPreferences::load().hardwareDecoding;
}

void AppController::setHardwareDecoding(bool enabled)
{
    changeGraphics([enabled](gpu::GraphicsPreferences &p) { p.hardwareDecoding = enabled; });
}

bool AppController::hardwareEncoding() const
{
    return gpu::GraphicsPreferences::load().hardwareEncoding;
}

void AppController::setHardwareEncoding(bool enabled)
{
    changeGraphics([enabled](gpu::GraphicsPreferences &p) { p.hardwareEncoding = enabled; });
}

void AppController::restart()
{
    closeEditor(); // everything written
    QStringList arguments = QCoreApplication::arguments();
    const QString program = arguments.takeFirst();
    // The files given at the first start are in a draft now: not again.
    arguments.erase(std::remove_if(arguments.begin(), arguments.end(), [](const QString &a) { return !a.startsWith(u'-'); }),
                    arguments.end());
    QProcess::startDetached(program, arguments);
    QCoreApplication::exit(0);
}

} // namespace vedit::ui
