// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "engine/gpu/GpuCapabilities.h"

#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

namespace vedit::gpu {

enum class UiBackend
{
    Vulkan,
    OpenGL,
    Software,
};

enum class UiBackendChoice
{
    Auto,
    Vulkan,
    OpenGL,
    Software,
};

QString uiBackendName(UiBackend backend);
std::optional<UiBackend> uiBackendFromName(QStringView name);

// User preferences (Preferences → Performance), stored in QSettings group "graphics".
struct GraphicsPreferences
{
    UiBackendChoice ui = UiBackendChoice::Auto;
    bool gpuEffects = true;
    bool hardwareDecoding = true;
    bool hardwareEncoding = true;

    static GraphicsPreferences load();
    void save() const;
    friend bool operator==(const GraphicsPreferences &, const GraphicsPreferences &) = default;
};

// What the application will use, and why (logged, and shown in Preferences → Performance).
struct GraphicsDecision
{
    UiBackend ui = UiBackend::Software;
    bool gpuEffects = false;
    bool hardwareDecoding = false;
    bool hardwareEncoding = false;
    bool lowVideoMemory = false; // <= 1 GB: smaller preview textures and GPU cache (SPEC 1bis rule 8)
    bool safeMode = false;
    QStringList reasons;
};

struct DecisionInput
{
    GpuCapabilities capabilities;
    GraphicsPreferences preferences;
    bool safeMode = false;
    // Backends that failed at runtime on this driver (sceneGraphError, device lost): never chosen automatically.
    QList<UiBackend> failedBackends;
    // Explicit request through QT_QUICK_BACKEND=software or QSG_RHI_BACKEND (always honoured).
    std::optional<UiBackend> environmentOverride;
};

// The fallback chains of SPEC 1bis rule 3, as a pure function (tested).
GraphicsDecision decideGraphics(const DecisionInput &input);

// Reads QT_QUICK_BACKEND / QSG_RHI_BACKEND.
std::optional<UiBackend> uiBackendFromEnvironment();

// Environment facts that need no driver (read in-process).
EnvironmentInfo probeEnvironment();

// Identifies the installed graphics stack: changes when drivers, GPUs, kernel, Qt or relevant
// environment variables change, which invalidates the capability cache (docs/ARCHITECTURE.md §7.2).
QString driverFingerprint();

// Runs vedit-gpuprobe in child processes, with timeouts; a crash or hang marks only that API unusable.
class CapabilityProber
{
public:
    explicit CapabilityProber(QString probeExecutable);

    // Vulkan and OpenGL, probed in parallel in two processes.
    GpuCapabilities probeGraphics(int timeoutMs = 4000) const;
    VideoInfo probeVideo(int timeoutMs = 20000) const;

private:
    QString m_executable;
};

// ~/.cache/vedit/gpu-caps.json
class CapabilityCache
{
public:
    explicit CapabilityCache(QString path);
    static QString defaultPath();

    std::optional<GpuCapabilities> load(const QString &fingerprint) const;
    bool save(const GpuCapabilities &capabilities, const QString &fingerprint) const;

private:
    QString m_path;
};

// Detects repeated crashes at startup (SPEC 1bis rule 5): after two consecutive starts that never
// reached a stable state, safe mode is forced. Also remembers backends that failed at runtime.
class StartupGuard
{
public:
    explicit StartupGuard(QString stateFile);
    static QString defaultPath();

    // Call once at startup, before creating any window.
    void begin(const QString &driverFingerprint);
    int consecutiveCrashes() const { return m_crashes; }
    bool requiresSafeMode() const { return m_crashes >= kCrashesBeforeSafeMode; }
    // Call when the app is stable (first frame shown + a few seconds) and at clean exit.
    void markStable();
    void recordBackendFailure(UiBackend backend);
    QList<UiBackend> failedBackends() const { return m_failedBackends; }

    static constexpr int kCrashesBeforeSafeMode = 2;

private:
    void save(bool inProgress) const;

    QString m_path;
    QString m_fingerprint;
    int m_crashes = 0;
    QList<UiBackend> m_failedBackends;
};

} // namespace vedit::gpu
