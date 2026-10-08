// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/gpu/GraphicsSetup.h"

#include <QElapsedTimer>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>
#include <QTest>

using namespace velacut::gpu;
using namespace Qt::StringLiterals;

namespace {

GpuCapabilities hardwareCaps()
{
    GpuCapabilities caps;
    caps.vulkan.status = ProbeStatus::Ok;
    caps.vulkan.devices.push_back({u"GPU"_s, 0x8086, 0x64a0, DeviceType::Integrated, u"1.4.0"_s, u"Mesa"_s, u"26"_s, 8192});
    caps.opengl.status = ProbeStatus::Ok;
    caps.opengl.major = 4;
    caps.opengl.minor = 6;
    caps.opengl.version = u"4.6 Mesa"_s;
    caps.opengl.renderer = u"Mesa Intel"_s;
    caps.video.status = ProbeStatus::Ok;
    caps.video.encoders = {u"h264_vaapi"_s};
    caps.video.decoders = {u"h264@vaapi"_s};
    return caps;
}

GraphicsDecision decide(const GpuCapabilities &caps, GraphicsPreferences preferences = {}, bool safeMode = false,
                        QList<UiBackend> failed = {}, std::optional<UiBackend> environment = std::nullopt)
{
    return decideGraphics(DecisionInput{caps, preferences, safeMode, failed, environment});
}

QString fakeProbe()
{
    return QCoreApplication::applicationDirPath() + u"/fake_gpuprobe"_s;
}

} // namespace

class TestGpu : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QCoreApplication::setOrganizationName(u"velacut"_s);
        QCoreApplication::setApplicationName(u"velacut-tests"_s);
    }

    void preferVulkanOnHardware()
    {
        const GraphicsDecision d = decide(hardwareCaps());
        QCOMPARE(d.ui, UiBackend::Vulkan);
        QVERIFY(d.gpuEffects);
        QVERIFY(d.hardwareDecoding);
        QVERIFY(d.hardwareEncoding);
        QVERIFY(!d.lowVideoMemory);
        QVERIFY(!d.reasons.isEmpty());
    }

    void fallbackChain()
    {
        GpuCapabilities caps = hardwareCaps();
        // Vulkan only through a CPU device (lavapipe): OpenGL wins.
        caps.vulkan.devices.front().type = DeviceType::Cpu;
        QCOMPARE(decide(caps).ui, UiBackend::OpenGL);
        // Vulkan probe crashed: OpenGL, and the reason is recorded.
        caps = hardwareCaps();
        caps.vulkan = VulkanInfo{ProbeStatus::Crashed, u"crash"_s, {}};
        const GraphicsDecision d = decide(caps);
        QCOMPARE(d.ui, UiBackend::OpenGL);
        QVERIFY(d.reasons.join(u' ').contains(u"crashed"_s));
        // OpenGL 2.0 is not enough for Qt Quick: software.
        caps.opengl.major = 2;
        caps.opengl.minor = 0;
        QCOMPARE(decide(caps).ui, UiBackend::Software);
        // OpenGL ES 2.0 is enough.
        caps.opengl.gles = true;
        QCOMPARE(decide(caps).ui, UiBackend::OpenGL);
        // Nothing at all: software, never a failure.
        QCOMPARE(decide(GpuCapabilities{}).ui, UiBackend::Software);
    }

    void softwareRasterizerDisablesGpuEffects()
    {
        GpuCapabilities caps = hardwareCaps();
        caps.vulkan = {};
        caps.opengl.softwareRasterizer = true;
        caps.opengl.renderer = u"llvmpipe"_s;
        const GraphicsDecision d = decide(caps);
        QCOMPARE(d.ui, UiBackend::OpenGL);
        QVERIFY(!d.gpuEffects);
    }

    void runtimeFailuresAreAvoided()
    {
        QCOMPARE(decide(hardwareCaps(), {}, false, {UiBackend::Vulkan}).ui, UiBackend::OpenGL);
        const GraphicsDecision d = decide(hardwareCaps(), {}, false, {UiBackend::Vulkan, UiBackend::OpenGL});
        QCOMPARE(d.ui, UiBackend::Software);
        QVERIFY(!d.gpuEffects);
    }

    void safeModeDisablesEverything()
    {
        const GraphicsDecision d = decide(hardwareCaps(), {}, true);
        QCOMPARE(d.ui, UiBackend::Software);
        QVERIFY(d.safeMode);
        QVERIFY(!d.gpuEffects && !d.hardwareDecoding && !d.hardwareEncoding);
    }

    void preferencesAndEnvironment()
    {
        GraphicsPreferences preferences;
        preferences.ui = UiBackendChoice::OpenGL;
        preferences.hardwareEncoding = false;
        GraphicsDecision d = decide(hardwareCaps(), preferences);
        QCOMPARE(d.ui, UiBackend::OpenGL);
        QVERIFY(!d.hardwareEncoding);
        QVERIFY(d.hardwareDecoding);
        // A preferred backend that is not usable falls back to the automatic chain.
        GpuCapabilities noVulkan = hardwareCaps();
        noVulkan.vulkan = {};
        preferences.ui = UiBackendChoice::Vulkan;
        QCOMPARE(decide(noVulkan, preferences).ui, UiBackend::OpenGL);
        // QT_QUICK_BACKEND=software always wins (SPEC 1bis verification).
        QCOMPARE(decide(hardwareCaps(), preferences, false, {}, UiBackend::Software).ui, UiBackend::Software);
    }

    void lowVideoMemory()
    {
        GpuCapabilities caps = hardwareCaps();
        caps.vulkan.devices.front().type = DeviceType::Discrete;
        caps.vulkan.devices.front().deviceLocalMemoryMB = 1024;
        QVERIFY(decide(caps).lowVideoMemory);
    }

    // An iGPU alone (Radeon 740M, Intel Arc 130V) shares the system memory: the app knows and renders the
    // preview at a reduced size. A discrete GPU next to it (hybrid laptop) means no limit.
    void integratedGpuIsDetected()
    {
        GpuCapabilities caps = hardwareCaps();
        caps.vulkan.devices.front().type = DeviceType::Integrated;
        caps.vulkan.devices.front().name = u"AMD Radeon 740M Graphics (RADV PHOENIX2)"_s;
        const GraphicsDecision igpu = decide(caps);
        QVERIFY(igpu.integratedGpu);
        QVERIFY(igpu.reasons.join(u' ').contains(u"integrated"_s));
        caps.vulkan.devices.push_back(caps.vulkan.devices.front());
        caps.vulkan.devices.front().type = DeviceType::Discrete;
        QVERIFY(!decide(caps).integratedGpu);
    }

    void environmentOverride()
    {
        qputenv("QT_QUICK_BACKEND", "software");
        QCOMPARE(uiBackendFromEnvironment(), std::optional(UiBackend::Software));
        qunsetenv("QT_QUICK_BACKEND");
        qputenv("QSG_RHI_BACKEND", "opengl");
        QCOMPARE(uiBackendFromEnvironment(), std::optional(UiBackend::OpenGL));
        qunsetenv("QSG_RHI_BACKEND");
        QCOMPARE(uiBackendFromEnvironment(), std::nullopt);
    }

    void capabilitiesJsonRoundTrip()
    {
        GpuCapabilities caps = hardwareCaps();
        caps.environment.sessionType = u"wayland"_s;
        caps.environment.drmDevices = {u"card0 0x8086:0x64a0 xe"_s};
        QCOMPARE(GpuCapabilities::fromJson(caps.toJson()), caps);
        QVERIFY(caps.summary().contains(u"Vulkan: ok"_s));
    }

    void cacheIsKeyedByFingerprint()
    {
        QTemporaryDir dir;
        CapabilityCache cache(dir.filePath(u"gpu-caps.json"_s));
        QVERIFY(!cache.load(u"abc"_s).has_value());
        QVERIFY(cache.save(hardwareCaps(), u"abc"_s));
        QCOMPARE(cache.load(u"abc"_s), std::optional(hardwareCaps()));
        QVERIFY(!cache.load(u"other driver"_s).has_value());
    }

    void fingerprintIsStableAndSensitive()
    {
        const QString a = driverFingerprint();
        QCOMPARE(driverFingerprint(), a);
        qputenv("LIBGL_ALWAYS_SOFTWARE", "1");
        QVERIFY(driverFingerprint() != a); // software GL must not reuse hardware results
        qunsetenv("LIBGL_ALWAYS_SOFTWARE");
    }

    void startupGuardForcesSafeModeAfterTwoCrashes()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(u"startup.json"_s);
        {
            StartupGuard guard(path);
            guard.begin(u"fp"_s);
            QCOMPARE(guard.consecutiveCrashes(), 0);
        } // "crash": markStable never called
        {
            StartupGuard guard(path);
            guard.begin(u"fp"_s);
            QCOMPARE(guard.consecutiveCrashes(), 1);
            QVERIFY(!guard.requiresSafeMode());
        }
        StartupGuard guard(path);
        guard.begin(u"fp"_s);
        QVERIFY(guard.requiresSafeMode());
        guard.markStable();
        StartupGuard next(path);
        next.begin(u"fp"_s);
        QCOMPARE(next.consecutiveCrashes(), 0);
    }

    void backendFailuresArePerDriver()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(u"startup.json"_s);
        {
            StartupGuard guard(path);
            guard.begin(u"driver-1"_s);
            guard.recordBackendFailure(UiBackend::Vulkan);
            guard.markStable();
        }
        {
            StartupGuard guard(path);
            guard.begin(u"driver-1"_s);
            QCOMPARE(guard.failedBackends(), QList<UiBackend>{UiBackend::Vulkan});
            guard.markStable();
        }
        StartupGuard updated(path);
        updated.begin(u"driver-2"_s); // new driver: a new chance
        QVERIFY(updated.failedBackends().isEmpty());
    }

    void proberHandlesCrashesAndHangs()
    {
        CapabilityProber prober(fakeProbe());
        GpuCapabilities caps = prober.probeGraphics(2000);
        QCOMPARE(caps.vulkan.status, ProbeStatus::Ok);
        QCOMPARE(caps.vulkan.devices.front().name, u"Fake GPU"_s);
        QCOMPARE(caps.opengl.status, ProbeStatus::Ok);

        qputenv("FAKE_VULKAN", "crash");
        qputenv("FAKE_OPENGL", "hang");
        QElapsedTimer timer;
        timer.start();
        caps = prober.probeGraphics(1500);
        QVERIFY(timer.elapsed() < 5000); // bounded, even with a hanging driver
        QCOMPARE(caps.vulkan.status, ProbeStatus::Crashed);
        QCOMPARE(caps.opengl.status, ProbeStatus::TimedOut);
        QCOMPARE(decideGraphics(DecisionInput{caps, {}, false, {}, std::nullopt}).ui, UiBackend::Software);

        qputenv("FAKE_VULKAN", "fail");
        qunsetenv("FAKE_OPENGL");
        caps = prober.probeGraphics(2000);
        QCOMPARE(caps.vulkan.status, ProbeStatus::Unavailable);
        QVERIFY(caps.vulkan.error.contains(u"no driver"_s));
        QCOMPARE(decideGraphics(DecisionInput{caps, {}, false, {}, std::nullopt}).ui, UiBackend::OpenGL);
        qunsetenv("FAKE_VULKAN");

        QCOMPARE(prober.probeVideo(2000).encoders, QStringList{u"h264_vaapi"_s});
        QCOMPARE(CapabilityProber(u"/does/not/exist"_s).probeVideo(1000).status, ProbeStatus::Unavailable);
    }

    // The real probe binary answers with valid JSON for every mode, on any machine (headless included).
    void realProbeAnswers()
    {
        const QString probe = QStringLiteral(VELACUT_GPUPROBE_PATH);
        for (const QString &mode : {u"--vulkan"_s, u"--opengl"_s, u"--video"_s}) {
            QProcess process;
            process.start(probe, {mode});
            QVERIFY2(process.waitForFinished(30000), qPrintable(mode));
            QCOMPARE(process.exitStatus(), QProcess::NormalExit);
            const QByteArray out = process.readAllStandardOutput().trimmed();
            QVERIFY2(QJsonDocument::fromJson(out).isObject(), qPrintable(mode + u": "_s + QString::fromUtf8(out)));
        }
    }
};

QTEST_GUILESS_MAIN(TestGpu)
#include "tst_gpu.moc"
