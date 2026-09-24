// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <optional>
#include <vector>

namespace vedit::gpu {

enum class DeviceType
{
    Other,
    Integrated,
    Discrete,
    Virtual,
    Cpu, // software rasterizer (lavapipe, llvmpipe)
};

struct VulkanDevice
{
    QString name;
    quint32 vendorId = 0;
    quint32 deviceId = 0;
    DeviceType type = DeviceType::Other;
    QString apiVersion;
    QString driverName;
    QString driverInfo;
    qint64 deviceLocalMemoryMB = 0;

    friend bool operator==(const VulkanDevice &, const VulkanDevice &) = default;
};

// Result of probing one graphics API in an isolated process. `status` says why an API is not usable.
enum class ProbeStatus
{
    Ok,
    Unavailable, // no driver / API not supported
    Crashed,     // the probe process crashed (driver bug): never use this API automatically
    TimedOut,    // the probe process hung
    NotProbed,
};

struct VulkanInfo
{
    ProbeStatus status = ProbeStatus::NotProbed;
    QString error;
    std::vector<VulkanDevice> devices;

    bool usable() const { return status == ProbeStatus::Ok && !devices.empty(); }
    friend bool operator==(const VulkanInfo &, const VulkanInfo &) = default;
};

struct OpenGLInfo
{
    ProbeStatus status = ProbeStatus::NotProbed;
    QString error;
    bool gles = false;
    int major = 0;
    int minor = 0;
    QString version;
    QString glslVersion;
    QString vendor;
    QString renderer;
    bool softwareRasterizer = false; // llvmpipe, softpipe, swrast
    qint64 videoMemoryMB = 0;        // if the driver reports it (GL_NVX/ATI/Mesa extensions), else 0

    bool usable() const;
    // Enough for the optional GPU effects path (GLSL for OpenGL 2.1 / GLES 2.0, hardware accelerated).
    bool supportsGpuEffects() const;
    friend bool operator==(const OpenGLInfo &, const OpenGLInfo &) = default;
};

// Hardware video: what really works, verified by encoding/decoding a few frames (docs/ARCHITECTURE.md §7.1).
struct VideoInfo
{
    ProbeStatus status = ProbeStatus::NotProbed;
    QString error;
    QStringList devices;  // FFmpeg hw device types that open: "vaapi", "cuda", "qsv", "vulkan"
    QStringList encoders; // working encoders, e.g. "h264_vaapi", "hevc_nvenc"
    QStringList decoders; // working hardware decoders as "<codec>@<device>", e.g. "h264@vaapi"

    friend bool operator==(const VideoInfo &, const VideoInfo &) = default;
};

struct EnvironmentInfo
{
    QString sessionType; // wayland, x11, …
    QString desktop;
    bool virtualMachine = false;
    QString virtualization; // e.g. "kvm", "vmware", "oracle"
    bool remoteSession = false;
    QStringList drmDevices; // "vendor:device driver" for each /sys/class/drm card

    friend bool operator==(const EnvironmentInfo &, const EnvironmentInfo &) = default;
};

struct GpuCapabilities
{
    VulkanInfo vulkan;
    OpenGLInfo opengl;
    VideoInfo video;
    EnvironmentInfo environment;

    QJsonObject toJson() const;
    static GpuCapabilities fromJson(const QJsonObject &json);
    // Human-readable multi-line summary ("Copia informazioni di sistema", logs).
    QString summary() const;

    friend bool operator==(const GpuCapabilities &, const GpuCapabilities &) = default;
};

QString probeStatusName(ProbeStatus status);
QString deviceTypeName(DeviceType type);

} // namespace vedit::gpu
