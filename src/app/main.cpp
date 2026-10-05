// SPDX-License-Identifier: GPL-3.0-or-later
// vedit entry point: startup sequence of docs/ARCHITECTURE.md §7 (capabilities, fallback chains, safe mode).
#include "app/Logging.h"
#include "common/DevSandbox.h"
#include "engine/gpu/GraphicsSetup.h"
#include "engine/mlt/MltRuntime.h"
#include "engine/playback/FrameSink.h"
#include "engine/playback/TimelinePlayer.h"
#include "ui/controllers/EditorController.h"
#include "ui/models/TimelineModel.h"
#include "document/DraftStore.h"
#include "theme/ThemeManager.h"
#include "engine/gpu/GpuTransitions.h"
#include "ui/controllers/AppController.h"
#include "ui/models/DraftsModel.h"

#include <QCommandLineParser>
#include <QDir>
#include <QGuiApplication>
#include <QLibraryInfo>
#include <QLocale>
#include <QLoggingCategory>
#include <QTranslator>
#include <QProcess>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QThreadPool>
#include <QTimer>
#include <QtQml/QQmlExtensionPlugin>

Q_IMPORT_QML_PLUGIN(Vedit_ThemePlugin)
Q_IMPORT_QML_PLUGIN(Vedit_StylePlugin)
Q_IMPORT_QML_PLUGIN(Vedit_ComponentsPlugin)
Q_IMPORT_QML_PLUGIN(Vedit_UIPlugin)

Q_LOGGING_CATEGORY(lcApp, "vedit.app")

using namespace Qt::StringLiterals;
using namespace vedit;

namespace {

QSGRendererInterface::GraphicsApi toGraphicsApi(gpu::UiBackend backend)
{
    switch (backend) {
    case gpu::UiBackend::Vulkan:
        return QSGRendererInterface::Vulkan;
    case gpu::UiBackend::OpenGL:
        return QSGRendererInterface::OpenGL;
    case gpu::UiBackend::Software:
        return QSGRendererInterface::Software;
    }
    return QSGRendererInterface::Software;
}

QString probeExecutable()
{
    return QCoreApplication::applicationDirPath() + u"/vedit-gpuprobe"_s;
}

// Relaunches vedit with the same arguments (runtime fallback to the next backend).
void relaunch()
{
    QStringList arguments = QCoreApplication::arguments();
    const QString program = arguments.takeFirst();
    QProcess::startDetached(program, arguments);
}

} // namespace

int main(int argc, char *argv[])
{
    applyDevSandbox();
    QCoreApplication::setOrganizationName(u"vedit"_s);
    QCoreApplication::setApplicationName(u"vedit"_s);
    QCoreApplication::setApplicationVersion(QStringLiteral(VEDIT_VERSION));
    QGuiApplication::setDesktopFileName(u"vedit"_s);
    QGuiApplication app(argc, argv);
    logging::install();

    // Interface language: the system one, or the one chosen in Preferences (Italian and English, SPEC §4). The default
    // locale is what the translations and the libraries' names (fx::LocalizedText) follow.
    const QString language = ui::AppController::savedLanguage();
    if (language == u"it"_s) {
        QLocale::setDefault(QLocale(QLocale::Italian, QLocale::Italy));
    } else if (language == u"en"_s) {
        QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedKingdom));
    }
    QTranslator qtTranslator;
    if (qtTranslator.load(QLocale(), u"qt"_s, u"_"_s, QLibraryInfo::path(QLibraryInfo::TranslationsPath))) {
        QCoreApplication::installTranslator(&qtTranslator);
    }
    QTranslator appTranslator;
    if (appTranslator.load(QLocale(), u"vedit"_s, u"_"_s, u":/i18n"_s)) {
        QCoreApplication::installTranslator(&appTranslator);
    }

    QCommandLineParser parser;
    parser.setApplicationDescription(QCoreApplication::translate("main", "Offline video editor"));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption safeModeOption(u"safe-mode"_s,
                                            QCoreApplication::translate("main", "Disable every GPU acceleration."));
    const QCommandLineOption galleryOption(u"component-gallery"_s,
                                           QCoreApplication::translate("main", "Show the Material 3 component gallery."));
    const QCommandLineOption reprobeOption(u"reprobe"_s,
                                           QCoreApplication::translate("main", "Probe the graphics capabilities again."));
    const QCommandLineOption smokeTestOption(u"smoke-test"_s,
                                             QCoreApplication::translate("main", "Put the given file in a new project, play it and exit (automatic tests)."));
    const QCommandLineOption screenshotOption(u"screenshot"_s,
                                              QCoreApplication::translate("main", "With --smoke-test: save an image of the window."),
                                              u"file"_s);
    const QCommandLineOption themeOption(u"theme"_s, QCoreApplication::translate("main", "Force the theme for this session: light, dark or auto."),
                                         u"mode"_s);
    const QCommandLineOption contrastOption(u"contrast"_s,
                                            QCoreApplication::translate("main", "Force the contrast for this session: standard, medium or high."),
                                            u"level"_s);
    const QCommandLineOption windowSizeOption(u"window-size"_s,
                                              QCoreApplication::translate("main", "Initial window size, e.g. 1280x2400 (screenshots)."),
                                              u"WxH"_s);
    parser.addOptions({safeModeOption, galleryOption, reprobeOption, smokeTestOption, screenshotOption, themeOption,
                       contrastOption, windowSizeOption});
    parser.addPositionalArgument(u"files"_s, QCoreApplication::translate("main", "Videos, photos or music to start a new project with."));
    parser.process(app);
    const bool smokeTest = parser.isSet(smokeTestOption);

    // 1. Startup guard: two unstable starts in a row force safe mode (SPEC 1bis rule 5).
    const QString fingerprint = gpu::driverFingerprint();
    gpu::StartupGuard guard(gpu::StartupGuard::defaultPath());
    guard.begin(fingerprint);
    const bool safeMode = parser.isSet(safeModeOption) || guard.requiresSafeMode();
    if (guard.requiresSafeMode()) {
        qCWarning(lcApp) << "safe mode forced after" << guard.consecutiveCrashes() << "unstable starts";
    }

    // 2. Capabilities: from the cache, or probed in isolated processes (only the fast graphics probe here).
    gpu::CapabilityCache cache(gpu::CapabilityCache::defaultPath());
    const gpu::CapabilityProber prober(probeExecutable());
    std::optional<gpu::GpuCapabilities> cached = parser.isSet(reprobeOption) ? std::nullopt : cache.load(fingerprint);
    gpu::GpuCapabilities capabilities;
    if (cached) {
        capabilities = *cached;
    } else if (!safeMode) {
        capabilities = prober.probeGraphics();
        cache.save(capabilities, fingerprint);
    } else {
        capabilities.environment = gpu::probeEnvironment();
    }

    // 3. Fallback chains -> decision, applied before any window exists.
    const gpu::GraphicsPreferences preferences = gpu::GraphicsPreferences::load();
    const auto decide = [&] {
        return gpu::decideGraphics({capabilities, preferences, safeMode, guard.failedBackends(), gpu::uiBackendFromEnvironment()});
    };
    const gpu::GraphicsDecision decision = decide();
    QQuickWindow::setGraphicsApi(toGraphicsApi(decision.ui));
    qCInfo(lcApp).noquote() << "UI backend:" << gpu::uiBackendName(decision.ui) << "—" << decision.reasons.join(u"; "_s);

    // 4. MLT loads its modules in background while the UI starts.
    engine::MltRuntime::initializeAsync();

    int result = 0;
    bool restarting = false;
    {
        // 5. Theme (Material 3, dynamic colors from the desktop).
        theme::SystemAppearance appearance;
        appearance.refresh();
        theme::ThemeManager::loadFonts();
        theme::ThemeManager themeManager(&appearance);
        theme::ThemeManager::setInstance(&themeManager);
        themeManager.loadSettings();
        {
            std::optional<theme::ThemeManager::Mode> mode;
            std::optional<theme::ThemeManager::Contrast> contrast;
            const QString themeValue = parser.value(themeOption);
            if (themeValue == u"light"_s) {
                mode = theme::ThemeManager::Mode::Light;
            } else if (themeValue == u"dark"_s) {
                mode = theme::ThemeManager::Mode::Dark;
            } else if (themeValue == u"auto"_s) {
                mode = theme::ThemeManager::Mode::Auto;
            }
            const QString contrastValue = parser.value(contrastOption);
            if (contrastValue == u"standard"_s) {
                contrast = theme::ThemeManager::Contrast::Standard;
            } else if (contrastValue == u"medium"_s) {
                contrast = theme::ThemeManager::Contrast::Medium;
            } else if (contrastValue == u"high"_s) {
                contrast = theme::ThemeManager::Contrast::High;
            }
            if (mode || contrast) {
                themeManager.setSessionOverrides(mode, contrast);
            }
        }
        themeManager.setSoftwareRendering(decision.ui == gpu::UiBackend::Software);
        QQuickStyle::setStyle(u"Vedit.Style"_s);

        ui::AppController controller(decision, capabilities);
        ui::AppController::setInstance(&controller);
        // GPU path of the transitions (SPEC §5.11bis): only when the graphics decision allows GPU effects; it checks
        // itself against the CPU on first use and falls back to it with a notice if anything goes wrong.
        if (decision.gpuEffects) {
            engine::GpuTransitions::initialize();
            engine::GpuTransitions::instance()->setFailureHandler([&controller](const QString &why) {
                QMetaObject::invokeMethod(&controller, [&controller, why] {
                    emit controller.message(QCoreApplication::translate("main", "The graphics card had a problem with "
                                                                                "the transitions: the processor draws "
                                                                                "them now (%1).").arg(why));
                });
            });
        }

        // Video capabilities are slower to probe: done in background, then cached.
        if (!safeMode && capabilities.video.status == gpu::ProbeStatus::NotProbed) {
            QThreadPool::globalInstance()->start([&prober, &controller, &capabilities, &cache, fingerprint, decide] {
                const gpu::VideoInfo video = prober.probeVideo();
                QMetaObject::invokeMethod(&controller, [&controller, &capabilities, &cache, fingerprint, decide, video] {
                    capabilities.video = video;
                    cache.save(capabilities, fingerprint);
                    controller.updateGraphics(decide(), capabilities);
                    qCInfo(lcApp).noquote() << "video capabilities:" << video.encoders.join(u", "_s) << "|"
                                            << video.decoders.join(u", "_s);
                });
            });
        }

        QQmlApplicationEngine qmlEngine;
        qmlEngine.loadFromModule("Vedit.UI", parser.isSet(galleryOption) ? "Gallery" : "Main");
        if (qmlEngine.rootObjects().isEmpty()) {
            qCCritical(lcApp) << "the user interface could not be loaded";
            return 1;
        }
        auto *window = qobject_cast<QQuickWindow *>(qmlEngine.rootObjects().constFirst());
        if (window) {
            // Stable = first frame shown and a few seconds without problems.
            QObject::connect(window, &QQuickWindow::frameSwapped, &app, [&guard] {
                static bool armed = false;
                if (!armed) {
                    armed = true;
                    QTimer::singleShot(3000, [&guard] { guard.markStable(); });
                }
            }, Qt::QueuedConnection);
            // Runtime fallback (SPEC 1bis rule 4): remember the failing backend and restart on the next one.
            QObject::connect(window, &QQuickWindow::sceneGraphError, &app,
                             [&guard, &decision, &restarting](QQuickWindow::SceneGraphError, const QString &message) {
                                 qCCritical(lcApp) << "scene graph error:" << message;
                                 guard.recordBackendFailure(decision.ui);
                                 restarting = true;
                                 relaunch();
                                 QCoreApplication::exit(3);
                             });
        }

        if (window && parser.isSet(windowSizeOption)) {
            const QStringList size = parser.value(windowSizeOption).split(u'x');
            if (size.size() == 2 && size[0].toInt() > 0 && size[1].toInt() > 0) {
                window->resize(size[0].toInt(), size[1].toInt());
            }
        }
        if (!smokeTest && parser.isSet(screenshotOption) && window) {
            const QString screenshot = parser.value(screenshotOption);
            QTimer::singleShot(1500, &app, [window, screenshot] {
                window->grabWindow().save(screenshot);
                QCoreApplication::exit(0);
            });
        }

        // "Open with vedit": a new project with those files on the timeline.
        QStringList files;
        for (const QString &file : parser.positionalArguments()) {
            files << QDir::current().absoluteFilePath(file);
        }
        if (!files.isEmpty() && controller.newProject()) {
            controller.editor()->importAndInsertPaths(files, 0, controller.editor()->timeline()->mainRow());
        }
        if (smokeTest) {
            // Automatic check (SPEC 1bis verification): the file goes through import, timeline, projection and
            // preview; frames must be decoded *and* drawn by the preview surface, then the draft is removed.
            ui::EditorController *editor = controller.editor();
            if (!editor) {
                qCCritical(lcApp) << "smoke test failed: no project";
                return 2;
            }
            editor->player()->setVolume(0.0);
            QObject::connect(editor->timeline(), &ui::TimelineModel::durationChanged, editor, [editor] {
                if (editor->timeline()->duration() > 0 && !editor->player()->playing()) {
                    editor->player()->play();
                }
            });
            QObject::connect(editor, &ui::EditorController::message, &app, [](const QString &text) {
                qCCritical(lcApp) << "smoke test failed:" << text;
                QCoreApplication::exit(2);
            });
            const QString screenshot = parser.value(screenshotOption);
            engine::FrameSink *sink = editor->player()->sink();
            QObject::connect(sink, &engine::FrameSink::frameReady, &app, [sink, window, screenshot] {
                if (sink->framesReceived() >= 45 && sink->framesDisplayed() >= 20) {
                    qCInfo(lcApp) << "smoke test passed:" << sink->framesReceived() << "frames decoded,"
                                  << sink->framesDisplayed() << "displayed," << sink->framesDropped() << "replaced before display";
                    if (!screenshot.isEmpty() && window) {
                        window->grabWindow().save(screenshot);
                    }
                    QCoreApplication::exit(0);
                }
            });
            QTimer::singleShot(30000, &app, [sink] {
                qCCritical(lcApp) << "smoke test failed: timeout;" << sink->framesReceived() << "decoded,"
                                  << sink->framesDisplayed() << "displayed";
                QCoreApplication::exit(2);
            });
        }

        result = app.exec();
        themeManager.saveSettings();
        if (smokeTest && controller.editor()) {
            const QString draft = controller.editor()->data().id.toString();
            controller.closeEditor();
            controller.drafts()->remove(draft); // the smoke test leaves nothing behind
        } else {
            controller.closeEditor(); // writes the last changes
        }
        engine::GpuTransitions::shutdown(); // after the editor: no MLT thread uses it any more
        // Destruction order: QML first, then the editor (every MLT object), then MLT itself.
    }
    engine::MltRuntime::shutdown();
    if (!restarting && result == 0) {
        guard.markStable();
    }
    return result;
}
