// SPDX-License-Identifier: GPL-3.0-or-later
#include "GraphicsSetup.h"

#include "common/Paths.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QSaveFile>
#include <QSettings>
#include <QSysInfo>
#include <QTimeZone>

using namespace Qt::StringLiterals;

namespace velacut::gpu {

namespace {

constexpr std::pair<UiBackend, QLatin1StringView> kBackendNames[] = {
    {UiBackend::Vulkan, "vulkan"_L1}, {UiBackend::OpenGL, "opengl"_L1}, {UiBackend::Software, "software"_L1}};

// Version of the probe output format: bump when probes change, to invalidate old caches.
constexpr int kProbeVersion = 1;

QString readSmallFile(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()).trimmed() : QString();
}

QStringList drmDevices()
{
    QStringList devices;
    const QDir drm(u"/sys/class/drm"_s);
    for (const QString &card : drm.entryList({u"card?"_s, u"card??"_s}, QDir::Dirs | QDir::System)) {
        const QString device = drm.filePath(card) + u"/device"_s;
        const QString driver = QFileInfo(device + u"/driver"_s).symLinkTarget();
        devices << u"%1 %2:%3 %4"_s.arg(card, readSmallFile(device + u"/vendor"_s), readSmallFile(device + u"/device"_s),
                                        QFileInfo(driver).fileName());
    }
    return devices;
}

struct ProcessOutcome
{
    ProbeStatus status = ProbeStatus::Unavailable;
    QString error;
    std::optional<GpuCapabilities> capabilities;
};

ProcessOutcome finish(QProcess &process, int timeoutMs)
{
    ProcessOutcome outcome;
    if (!process.waitForStarted(timeoutMs)) {
        outcome.error = u"cannot start the GPU probe: "_s + process.errorString();
        return outcome;
    }
    if (!process.waitForFinished(timeoutMs)) {
        process.kill();
        process.waitForFinished(1000);
        outcome.status = ProbeStatus::TimedOut;
        outcome.error = u"the probe did not answer within %1 ms"_s.arg(timeoutMs);
        return outcome;
    }
    if (process.exitStatus() == QProcess::CrashExit) {
        outcome.status = ProbeStatus::Crashed;
        outcome.error = u"the probe crashed (driver problem)"_s;
        return outcome;
    }
    const QByteArray output = process.readAllStandardOutput().trimmed();
    const QJsonDocument document = QJsonDocument::fromJson(output.mid(output.lastIndexOf('\n') + 1));
    if (process.exitCode() != 0 || !document.isObject()) {
        outcome.error = u"the probe failed: "_s + QString::fromUtf8(process.readAllStandardError()).left(500);
        return outcome;
    }
    outcome.status = ProbeStatus::Ok;
    outcome.capabilities = GpuCapabilities::fromJson(document.object());
    return outcome;
}

} // namespace

QString uiBackendName(UiBackend backend)
{
    for (const auto &[value, name] : kBackendNames) {
        if (value == backend) {
            return name;
        }
    }
    return {};
}

std::optional<UiBackend> uiBackendFromName(QStringView name)
{
    for (const auto &[value, text] : kBackendNames) {
        if (name == text) {
            return value;
        }
    }
    return std::nullopt;
}

GraphicsPreferences GraphicsPreferences::load()
{
    QSettings settings;
    settings.beginGroup(u"graphics"_s);
    GraphicsPreferences preferences;
    const QString ui = settings.value(u"uiBackend"_s, u"auto"_s).toString();
    if (const auto backend = uiBackendFromName(ui)) {
        preferences.ui = *backend == UiBackend::Vulkan   ? UiBackendChoice::Vulkan
                         : *backend == UiBackend::OpenGL ? UiBackendChoice::OpenGL
                                                         : UiBackendChoice::Software;
    }
    preferences.gpuEffects = settings.value(u"gpuEffects"_s, true).toBool();
    preferences.hardwareDecoding = settings.value(u"hardwareDecoding"_s, true).toBool();
    preferences.hardwareEncoding = settings.value(u"hardwareEncoding"_s, true).toBool();
    return preferences;
}

void GraphicsPreferences::save() const
{
    QSettings settings;
    settings.beginGroup(u"graphics"_s);
    QString ui = u"auto"_s;
    switch (this->ui) {
    case UiBackendChoice::Auto:
        break;
    case UiBackendChoice::Vulkan:
        ui = uiBackendName(UiBackend::Vulkan);
        break;
    case UiBackendChoice::OpenGL:
        ui = uiBackendName(UiBackend::OpenGL);
        break;
    case UiBackendChoice::Software:
        ui = uiBackendName(UiBackend::Software);
        break;
    }
    settings.setValue(u"uiBackend"_s, ui);
    settings.setValue(u"gpuEffects"_s, gpuEffects);
    settings.setValue(u"hardwareDecoding"_s, hardwareDecoding);
    settings.setValue(u"hardwareEncoding"_s, hardwareEncoding);
}

GraphicsDecision decideGraphics(const DecisionInput &input)
{
    GraphicsDecision decision;
    const GpuCapabilities &caps = input.capabilities;
    const GraphicsPreferences &preferences = input.preferences;
    if (input.safeMode) {
        decision.safeMode = true;
        decision.ui = UiBackend::Software;
        decision.reasons << u"safe mode: every GPU acceleration is disabled"_s;
        return decision;
    }

    const bool hardwareVulkan = std::any_of(caps.vulkan.devices.begin(), caps.vulkan.devices.end(),
                                            [](const VulkanDevice &d) { return d.type != DeviceType::Cpu; });
    const bool vulkanOk = caps.vulkan.usable() && hardwareVulkan && !input.failedBackends.contains(UiBackend::Vulkan);
    const bool openglOk = caps.opengl.usable() && !input.failedBackends.contains(UiBackend::OpenGL);
    const auto usable = [&](UiBackend backend) {
        switch (backend) {
        case UiBackend::Vulkan:
            return vulkanOk;
        case UiBackend::OpenGL:
            return openglOk;
        case UiBackend::Software:
            return true;
        }
        return false;
    };

    std::optional<UiBackend> chosen;
    if (input.environmentOverride) {
        chosen = input.environmentOverride;
        decision.reasons << u"UI backend requested by the environment: "_s + uiBackendName(*chosen);
    } else if (preferences.ui != UiBackendChoice::Auto) {
        const UiBackend wanted = preferences.ui == UiBackendChoice::Vulkan   ? UiBackend::Vulkan
                                 : preferences.ui == UiBackendChoice::OpenGL ? UiBackend::OpenGL
                                                                             : UiBackend::Software;
        if (usable(wanted)) {
            chosen = wanted;
            decision.reasons << u"UI backend chosen in Preferences: "_s + uiBackendName(wanted);
        } else {
            decision.reasons << u"preferred UI backend %1 is not usable here, automatic choice"_s.arg(uiBackendName(wanted));
        }
    }
    if (!chosen) {
        // Chain: Vulkan -> OpenGL (3.3+, then 2.1 / ES 2.0) -> software.
        if (vulkanOk) {
            chosen = UiBackend::Vulkan;
            decision.reasons << u"Vulkan available on a hardware device"_s;
        } else if (openglOk) {
            chosen = UiBackend::OpenGL;
            decision.reasons << (caps.opengl.softwareRasterizer ? u"OpenGL through a software rasterizer (%1)"_s.arg(caps.opengl.renderer)
                                                                : u"OpenGL %1"_s.arg(caps.opengl.version));
            if (caps.vulkan.status == ProbeStatus::Crashed || caps.vulkan.status == ProbeStatus::TimedOut) {
                decision.reasons << u"Vulkan probe "_s + probeStatusName(caps.vulkan.status);
            }
        } else {
            chosen = UiBackend::Software;
            decision.reasons << u"no usable GPU API: software rendering"_s;
        }
    }
    decision.ui = *chosen;

    decision.gpuEffects = preferences.gpuEffects && caps.opengl.supportsGpuEffects() &&
                          !input.failedBackends.contains(UiBackend::OpenGL);
    decision.hardwareDecoding = preferences.hardwareDecoding && !caps.video.decoders.isEmpty();
    decision.hardwareEncoding = preferences.hardwareEncoding && !caps.video.encoders.isEmpty();

    qint64 videoMemory = caps.opengl.videoMemoryMB;
    bool integrated = false;
    bool discrete = false;
    for (const VulkanDevice &device : caps.vulkan.devices) {
        if (device.type == DeviceType::Discrete || device.type == DeviceType::Integrated) {
            videoMemory = std::max(videoMemory, device.deviceLocalMemoryMB);
        }
        integrated = integrated || device.type == DeviceType::Integrated;
        discrete = discrete || device.type == DeviceType::Discrete;
    }
    decision.lowVideoMemory = videoMemory > 0 && videoMemory <= 1024;
    if (decision.lowVideoMemory) {
        decision.reasons << u"low video memory (%1 MB): reduced preview textures and GPU cache"_s.arg(videoMemory);
    }
    // An iGPU without a discrete GPU in the machine shares the system memory: the preview is rendered at a
    // reduced size (the export always stays at full size). Reported so Preferences can show why.
    decision.integratedGpu = integrated && !discrete;
    if (decision.integratedGpu) {
        decision.reasons << u"integrated GPU: preview rendered at reduced size for smooth playback"_s;
    }
    return decision;
}

std::optional<UiBackend> uiBackendFromEnvironment()
{
    if (qEnvironmentVariable("QT_QUICK_BACKEND") == u"software"_s) {
        return UiBackend::Software;
    }
    const QString rhi = qEnvironmentVariable("QSG_RHI_BACKEND").toLower();
    if (rhi == u"vulkan"_s) {
        return UiBackend::Vulkan;
    }
    if (rhi == u"opengl"_s || rhi == u"gl"_s) {
        return UiBackend::OpenGL;
    }
    return std::nullopt;
}

EnvironmentInfo probeEnvironment()
{
    EnvironmentInfo info;
    info.sessionType = qEnvironmentVariable("XDG_SESSION_TYPE");
    info.desktop = qEnvironmentVariable("XDG_CURRENT_DESKTOP");
    const QString vendor = readSmallFile(u"/sys/class/dmi/id/sys_vendor"_s);
    const QString product = readSmallFile(u"/sys/class/dmi/id/product_name"_s);
    const QString hypervisor = readSmallFile(u"/sys/hypervisor/type"_s);
    const std::pair<QLatin1StringView, QLatin1StringView> kVirtualMachines[] = {
        {"QEMU"_L1, "kvm"_L1},          {"KVM"_L1, "kvm"_L1},       {"VMware"_L1, "vmware"_L1},
        {"VirtualBox"_L1, "oracle"_L1}, {"innotek"_L1, "oracle"_L1}, {"Virtual Machine"_L1, "microsoft"_L1},
        {"Parallels"_L1, "parallels"_L1}};
    for (const auto &[marker, name] : kVirtualMachines) {
        if (vendor.contains(marker) || product.contains(marker)) {
            info.virtualMachine = true;
            info.virtualization = name;
        }
    }
    if (!info.virtualMachine && !hypervisor.isEmpty()) {
        info.virtualMachine = true;
        info.virtualization = hypervisor;
    }
    info.remoteSession = qEnvironmentVariableIsSet("SSH_CONNECTION") || qEnvironmentVariableIsSet("XRDP_SESSION") ||
                         qEnvironmentVariableIsSet("VNCDESKTOP");
    info.drmDevices = drmDevices();
    return info;
}

QString driverFingerprint()
{
    QStringList parts;
    parts << u"probe=%1"_s.arg(kProbeVersion) << u"qt="_s + QString::fromLatin1(qVersion())
          << u"kernel="_s + QSysInfo::kernelVersion();
    for (const char *variable : {"LIBGL_ALWAYS_SOFTWARE", "GALLIUM_DRIVER", "MESA_LOADER_DRIVER_OVERRIDE",
                                 "__GLX_VENDOR_LIBRARY_NAME", "__NV_PRIME_RENDER_OFFLOAD", "DRI_PRIME",
                                 "VK_ICD_FILENAMES", "VK_DRIVER_FILES", "LIBVA_DRIVER_NAME", "QT_QPA_PLATFORM"}) {
        parts << QLatin1StringView(variable) + u'=' + qEnvironmentVariable(variable);
    }
    parts << drmDevices();
    const auto addFiles = [&parts](const QString &directory, const QStringList &patterns) {
        QDir dir(directory);
        for (const QFileInfo &file : dir.entryInfoList(patterns, QDir::Files, QDir::Name)) {
            parts << u"%1 %2 %3"_s.arg(file.fileName()).arg(file.size()).arg(file.lastModified(QTimeZone::UTC).toSecsSinceEpoch());
        }
    };
    addFiles(u"/usr/lib"_s, {u"libGLX_nvidia.so.*"_s, u"libnvidia-glcore.so.*"_s, u"libgallium-*.so"_s,
                             u"libvulkan_*.so"_s, u"libGLX_mesa.so.*"_s, u"libEGL_mesa.so.*"_s, u"libvpl.so.*"_s});
    addFiles(u"/usr/lib/dri"_s, {u"*_drv_video.so"_s, u"*_dri.so"_s});
    addFiles(u"/usr/share/vulkan/icd.d"_s, {u"*.json"_s});
    return QString::fromLatin1(QCryptographicHash::hash(parts.join(u'\n').toUtf8(), QCryptographicHash::Sha1).toHex());
}

CapabilityProber::CapabilityProber(QString probeExecutable)
    : m_executable(std::move(probeExecutable))
{
}

GpuCapabilities CapabilityProber::probeGraphics(int timeoutMs) const
{
    GpuCapabilities caps;
    caps.environment = probeEnvironment();
    QProcess vulkan;
    QProcess opengl;
    vulkan.start(m_executable, {u"--vulkan"_s});
    opengl.start(m_executable, {u"--opengl"_s});
    const ProcessOutcome vk = finish(vulkan, timeoutMs);
    const ProcessOutcome gl = finish(opengl, timeoutMs);
    if (vk.capabilities) {
        caps.vulkan = vk.capabilities->vulkan;
    } else {
        caps.vulkan.status = vk.status;
        caps.vulkan.error = vk.error;
    }
    if (gl.capabilities) {
        caps.opengl = gl.capabilities->opengl;
    } else {
        caps.opengl.status = gl.status;
        caps.opengl.error = gl.error;
    }
    return caps;
}

VideoInfo CapabilityProber::probeVideo(int timeoutMs) const
{
    QProcess process;
    process.start(m_executable, {u"--video"_s});
    const ProcessOutcome outcome = finish(process, timeoutMs);
    if (outcome.capabilities) {
        return outcome.capabilities->video;
    }
    VideoInfo info;
    info.status = outcome.status;
    info.error = outcome.error;
    return info;
}

CapabilityCache::CapabilityCache(QString path)
    : m_path(std::move(path))
{
}

QString CapabilityCache::defaultPath()
{
    return paths::cacheDir() + u"/gpu-caps.json"_s;
}

std::optional<GpuCapabilities> CapabilityCache::load(const QString &fingerprint) const
{
    QFile file(m_path);
    if (!file.open(QIODevice::ReadOnly)) {
        return std::nullopt;
    }
    const QJsonObject json = QJsonDocument::fromJson(file.readAll()).object();
    if (json.value(u"fingerprint"_s).toString() != fingerprint) {
        return std::nullopt;
    }
    return GpuCapabilities::fromJson(json.value(u"capabilities"_s).toObject());
}

bool CapabilityCache::save(const GpuCapabilities &capabilities, const QString &fingerprint) const
{
    QDir().mkpath(QFileInfo(m_path).absolutePath());
    QSaveFile file(m_path);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    const QJsonObject json{{u"fingerprint"_s, fingerprint},
                           {u"probedAt"_s, QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
                           {u"capabilities"_s, capabilities.toJson()}};
    file.write(QJsonDocument(json).toJson(QJsonDocument::Indented));
    return file.commit();
}

StartupGuard::StartupGuard(QString stateFile)
    : m_path(std::move(stateFile))
{
}

QString StartupGuard::defaultPath()
{
    return paths::stateDir() + u"/startup.json"_s;
}

void StartupGuard::begin(const QString &driverFingerprint)
{
    m_fingerprint = driverFingerprint;
    QFile file(m_path);
    QJsonObject state;
    if (file.open(QIODevice::ReadOnly)) {
        state = QJsonDocument::fromJson(file.readAll()).object();
    }
    m_crashes = state.value(u"consecutiveCrashes"_s).toInt();
    if (state.value(u"inProgress"_s).toBool()) {
        ++m_crashes; // the previous start never reached a stable state
    }
    // Runtime failures are remembered per driver: a driver update gets a new chance.
    m_failedBackends.clear();
    if (state.value(u"fingerprint"_s).toString() == driverFingerprint) {
        for (const QJsonValue &value : state.value(u"failedBackends"_s).toArray()) {
            if (const auto backend = uiBackendFromName(value.toString())) {
                m_failedBackends.append(*backend);
            }
        }
    }
    save(true);
}

void StartupGuard::markStable()
{
    m_crashes = 0;
    save(false);
}

void StartupGuard::recordBackendFailure(UiBackend backend)
{
    if (!m_failedBackends.contains(backend)) {
        m_failedBackends.append(backend);
    }
    save(true);
}

void StartupGuard::save(bool inProgress) const
{
    QJsonArray failed;
    for (UiBackend backend : m_failedBackends) {
        failed.append(uiBackendName(backend));
    }
    const QJsonObject state{{u"inProgress"_s, inProgress},
                            {u"consecutiveCrashes"_s, m_crashes},
                            {u"fingerprint"_s, m_fingerprint},
                            {u"failedBackends"_s, failed}};
    QDir().mkpath(QFileInfo(m_path).absolutePath());
    QSaveFile file(m_path);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(state).toJson(QJsonDocument::Compact));
        file.commit();
    }
}

} // namespace velacut::gpu
