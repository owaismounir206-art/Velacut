// SPDX-License-Identifier: GPL-3.0-or-later
#include "GpuCapabilities.h"

#include <QJsonArray>

using namespace Qt::StringLiterals;

namespace vedit::gpu {

namespace {

constexpr std::pair<ProbeStatus, QLatin1StringView> kStatusNames[] = {
    {ProbeStatus::Ok, "ok"_L1},           {ProbeStatus::Unavailable, "unavailable"_L1},
    {ProbeStatus::Crashed, "crashed"_L1}, {ProbeStatus::TimedOut, "timedOut"_L1},
    {ProbeStatus::NotProbed, "notProbed"_L1}};

constexpr std::pair<DeviceType, QLatin1StringView> kDeviceTypes[] = {
    {DeviceType::Other, "other"_L1},       {DeviceType::Integrated, "integrated"_L1},
    {DeviceType::Discrete, "discrete"_L1}, {DeviceType::Virtual, "virtual"_L1},
    {DeviceType::Cpu, "cpu"_L1}};

ProbeStatus statusFromName(const QString &name)
{
    for (const auto &[status, text] : kStatusNames) {
        if (name == text) {
            return status;
        }
    }
    return ProbeStatus::NotProbed;
}

DeviceType deviceTypeFromName(const QString &name)
{
    for (const auto &[type, text] : kDeviceTypes) {
        if (name == text) {
            return type;
        }
    }
    return DeviceType::Other;
}

QJsonArray toArray(const QStringList &list)
{
    return QJsonArray::fromStringList(list);
}

QStringList toStringList(const QJsonValue &value)
{
    QStringList list;
    for (const QJsonValue &item : value.toArray()) {
        list.append(item.toString());
    }
    return list;
}

} // namespace

QString probeStatusName(ProbeStatus status)
{
    for (const auto &[value, text] : kStatusNames) {
        if (value == status) {
            return text;
        }
    }
    return {};
}

QString deviceTypeName(DeviceType type)
{
    for (const auto &[value, text] : kDeviceTypes) {
        if (value == type) {
            return text;
        }
    }
    return {};
}

bool OpenGLInfo::usable() const
{
    if (status != ProbeStatus::Ok) {
        return false;
    }
    // Qt Quick's OpenGL backend needs OpenGL 2.1 or OpenGL ES 2.0.
    return gles ? major >= 2 : (major > 2 || (major == 2 && minor >= 1));
}

bool OpenGLInfo::supportsGpuEffects() const
{
    return usable() && !softwareRasterizer;
}

QJsonObject GpuCapabilities::toJson() const
{
    QJsonArray devices;
    for (const VulkanDevice &device : vulkan.devices) {
        devices.append(QJsonObject{{u"name"_s, device.name},
                                   {u"vendorId"_s, static_cast<qint64>(device.vendorId)},
                                   {u"deviceId"_s, static_cast<qint64>(device.deviceId)},
                                   {u"type"_s, deviceTypeName(device.type)},
                                   {u"apiVersion"_s, device.apiVersion},
                                   {u"driverName"_s, device.driverName},
                                   {u"driverInfo"_s, device.driverInfo},
                                   {u"deviceLocalMemoryMB"_s, device.deviceLocalMemoryMB}});
    }
    return {
        {u"vulkan"_s, QJsonObject{{u"status"_s, probeStatusName(vulkan.status)},
                                  {u"error"_s, vulkan.error},
                                  {u"devices"_s, devices}}},
        {u"opengl"_s, QJsonObject{{u"status"_s, probeStatusName(opengl.status)},
                                  {u"error"_s, opengl.error},
                                  {u"gles"_s, opengl.gles},
                                  {u"major"_s, opengl.major},
                                  {u"minor"_s, opengl.minor},
                                  {u"version"_s, opengl.version},
                                  {u"glslVersion"_s, opengl.glslVersion},
                                  {u"vendor"_s, opengl.vendor},
                                  {u"renderer"_s, opengl.renderer},
                                  {u"softwareRasterizer"_s, opengl.softwareRasterizer},
                                  {u"videoMemoryMB"_s, opengl.videoMemoryMB}}},
        {u"video"_s, QJsonObject{{u"status"_s, probeStatusName(video.status)},
                                 {u"error"_s, video.error},
                                 {u"devices"_s, toArray(video.devices)},
                                 {u"encoders"_s, toArray(video.encoders)},
                                 {u"decoders"_s, toArray(video.decoders)}}},
        {u"environment"_s, QJsonObject{{u"sessionType"_s, environment.sessionType},
                                       {u"desktop"_s, environment.desktop},
                                       {u"virtualMachine"_s, environment.virtualMachine},
                                       {u"virtualization"_s, environment.virtualization},
                                       {u"remoteSession"_s, environment.remoteSession},
                                       {u"drmDevices"_s, toArray(environment.drmDevices)}}},
    };
}

GpuCapabilities GpuCapabilities::fromJson(const QJsonObject &json)
{
    GpuCapabilities caps;
    const QJsonObject vk = json.value(u"vulkan"_s).toObject();
    caps.vulkan.status = statusFromName(vk.value(u"status"_s).toString());
    caps.vulkan.error = vk.value(u"error"_s).toString();
    for (const QJsonValue &value : vk.value(u"devices"_s).toArray()) {
        const QJsonObject d = value.toObject();
        VulkanDevice device;
        device.name = d.value(u"name"_s).toString();
        device.vendorId = static_cast<quint32>(d.value(u"vendorId"_s).toInteger());
        device.deviceId = static_cast<quint32>(d.value(u"deviceId"_s).toInteger());
        device.type = deviceTypeFromName(d.value(u"type"_s).toString());
        device.apiVersion = d.value(u"apiVersion"_s).toString();
        device.driverName = d.value(u"driverName"_s).toString();
        device.driverInfo = d.value(u"driverInfo"_s).toString();
        device.deviceLocalMemoryMB = d.value(u"deviceLocalMemoryMB"_s).toInteger();
        caps.vulkan.devices.push_back(device);
    }
    const QJsonObject gl = json.value(u"opengl"_s).toObject();
    caps.opengl.status = statusFromName(gl.value(u"status"_s).toString());
    caps.opengl.error = gl.value(u"error"_s).toString();
    caps.opengl.gles = gl.value(u"gles"_s).toBool();
    caps.opengl.major = gl.value(u"major"_s).toInt();
    caps.opengl.minor = gl.value(u"minor"_s).toInt();
    caps.opengl.version = gl.value(u"version"_s).toString();
    caps.opengl.glslVersion = gl.value(u"glslVersion"_s).toString();
    caps.opengl.vendor = gl.value(u"vendor"_s).toString();
    caps.opengl.renderer = gl.value(u"renderer"_s).toString();
    caps.opengl.softwareRasterizer = gl.value(u"softwareRasterizer"_s).toBool();
    caps.opengl.videoMemoryMB = gl.value(u"videoMemoryMB"_s).toInteger();
    const QJsonObject video = json.value(u"video"_s).toObject();
    caps.video.status = statusFromName(video.value(u"status"_s).toString());
    caps.video.error = video.value(u"error"_s).toString();
    caps.video.devices = toStringList(video.value(u"devices"_s));
    caps.video.encoders = toStringList(video.value(u"encoders"_s));
    caps.video.decoders = toStringList(video.value(u"decoders"_s));
    const QJsonObject env = json.value(u"environment"_s).toObject();
    caps.environment.sessionType = env.value(u"sessionType"_s).toString();
    caps.environment.desktop = env.value(u"desktop"_s).toString();
    caps.environment.virtualMachine = env.value(u"virtualMachine"_s).toBool();
    caps.environment.virtualization = env.value(u"virtualization"_s).toString();
    caps.environment.remoteSession = env.value(u"remoteSession"_s).toBool();
    caps.environment.drmDevices = toStringList(env.value(u"drmDevices"_s));
    return caps;
}

QString GpuCapabilities::summary() const
{
    QStringList lines;
    lines << u"Session: %1 (%2)%3%4"_s.arg(environment.sessionType, environment.desktop,
                                          environment.virtualMachine ? u", VM: "_s + environment.virtualization : QString(),
                                          environment.remoteSession ? u", remote"_s : QString());
    for (const QString &device : environment.drmDevices) {
        lines << u"DRM device: "_s + device;
    }
    lines << u"Vulkan: "_s + probeStatusName(vulkan.status) + (vulkan.error.isEmpty() ? QString() : u" ("_s + vulkan.error + u')');
    for (const VulkanDevice &device : vulkan.devices) {
        lines << u"  %1 [%2] Vulkan %3, driver %4 %5, %6 MB"_s.arg(device.name, deviceTypeName(device.type), device.apiVersion,
                                                                    device.driverName, device.driverInfo)
                     .arg(device.deviceLocalMemoryMB);
    }
    lines << u"OpenGL: %1 %2 %3 — %4 / %5%6"_s.arg(probeStatusName(opengl.status), opengl.gles ? u"ES"_s : QString(),
                                                  opengl.version, opengl.vendor, opengl.renderer,
                                                  opengl.softwareRasterizer ? u" (software)"_s : QString());
    lines << u"Video: %1; devices: %2; encoders: %3; decoders: %4"_s.arg(
        probeStatusName(video.status), video.devices.join(u", "_s), video.encoders.join(u", "_s), video.decoders.join(u", "_s));
    return lines.join(u'\n');
}

} // namespace vedit::gpu
