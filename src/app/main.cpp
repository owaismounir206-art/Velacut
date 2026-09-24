// SPDX-License-Identifier: GPL-3.0-or-later
// vedit entry point: startup sequence of docs/ARCHITECTURE.md §7 (capabilities, fallback chains, safe mode).
#include "app/Logging.h"
#include "common/DevSandbox.h"
#include "engine/gpu/GraphicsSetup.h"
#include "engine/mlt/MltRuntime.h"
#include "engine/playback/Player.h"
#include "theme/ThemeManager.h"
#include "ui/controllers/AppController.h"

#include <QCommandLineParser>
#include <QDir>
#include <QGuiApplication>
#include <QLoggingCategory>
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
                                             QCoreApplication::translate("main", "Play the given file and exit (automatic tests)."));
    const QCommandLineOption screenshotOption(u"screenshot"_s,
                                              QCoreApplication::translate("main", "With --smoke-test: save an image of the window."),
                                              u"file"_s);
    parser.addOptions({safeModeOption, galleryOption, reprobeOption, smokeTestOption, screenshotOption});
    parser.addPositionalArgument(u"file"_s, QCoreApplication::translate("main", "Video to open."));
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
        themeManager.setSoftwareRendering(decision.ui == gpu::UiBackend::Software);
        QQuickStyle::setStyle(u"Vedit.Style"_s);

        engine::Player player;
        ui::AppController controller(&player, decision, capabilities);
        ui::AppController::setInstance(&controller);

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

        const QStringList files = parser.positionalArguments();
        if (!files.isEmpty()) {
            player.open(QDir::current().absoluteFilePath(files.constFirst()));
        }
        if (smokeTest) {
            // Automatic check (SPEC 1bis verification): play the file (muted), require real frames, exit.
            player.setVolume(0.0);
            QObject::connect(&player, &engine::Player::sourceChanged, &player, [&player] { player.play(); });
            // Passes only when frames were decoded *and* drawn by the preview surface.
            const QString screenshot = parser.value(screenshotOption);
            QObject::connect(player.sink(), &engine::FrameSink::frameReady, &app, [&player, window, screenshot] {
                engine::FrameSink *sink = player.sink();
                if (sink->framesReceived() >= 45 && sink->framesDisplayed() >= 20) {
                    qCInfo(lcApp) << "smoke test passed:" << sink->framesReceived() << "frames decoded,"
                                  << sink->framesDisplayed() << "displayed," << sink->framesDropped() << "replaced before display";
                    if (!screenshot.isEmpty() && window) {
                        window->grabWindow().save(screenshot);
                    }
                    QCoreApplication::exit(0);
                }
            });
            QObject::connect(&player, &engine::Player::stateChanged, &app, [&player] {
                if (!player.error().isEmpty()) {
                    qCCritical(lcApp) << "smoke test failed:" << player.error();
                    QCoreApplication::exit(2);
                }
            });
            QTimer::singleShot(30000, &app, [&player] {
                qCCritical(lcApp) << "smoke test failed: timeout;" << player.sink()->framesReceived() << "decoded,"
                                  << player.sink()->framesDisplayed() << "displayed";
                QCoreApplication::exit(2);
            });
        }

        result = app.exec();
        themeManager.saveSettings();
        // Destruction order: QML first, then the player (every MLT object), then MLT itself.
    }
    engine::MltRuntime::shutdown();
    if (!restarting && result == 0) {
        guard.markStable();
    }
    return result;
}
