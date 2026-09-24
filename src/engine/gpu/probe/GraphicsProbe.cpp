// SPDX-License-Identifier: GPL-3.0-or-later
#include "Probes.h"

#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QRegularExpression>
#include <QVersionNumber>
#include <QVulkanFunctions>
#include <QVulkanInstance>

#include <vulkan/vulkan.h>

using namespace Qt::StringLiterals;

namespace vedit::gpu::probe {

namespace {

QString vulkanVersionString(uint32_t version)
{
    return u"%1.%2.%3"_s.arg(VK_API_VERSION_MAJOR(version)).arg(VK_API_VERSION_MINOR(version)).arg(VK_API_VERSION_PATCH(version));
}

DeviceType toDeviceType(VkPhysicalDeviceType type)
{
    switch (type) {
    case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
        return DeviceType::Integrated;
    case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
        return DeviceType::Discrete;
    case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
        return DeviceType::Virtual;
    case VK_PHYSICAL_DEVICE_TYPE_CPU:
        return DeviceType::Cpu;
    default:
        return DeviceType::Other;
    }
}

bool isSoftwareRenderer(const QString &renderer)
{
    static const QRegularExpression software(u"llvmpipe|softpipe|swrast|software rasterizer|lavapipe"_s,
                                             QRegularExpression::CaseInsensitiveOption);
    return software.match(renderer).hasMatch();
}

} // namespace

VulkanInfo probeVulkan()
{
    VulkanInfo info;
    QVulkanInstance instance;
    instance.setApiVersion(QVersionNumber(1, 1));
    if (!instance.create()) {
        info.status = ProbeStatus::Unavailable;
        info.error = u"cannot create a Vulkan instance (error %1)"_s.arg(instance.errorCode());
        return info;
    }
    QVulkanFunctions *f = instance.functions();
    uint32_t count = 0;
    if (f->vkEnumeratePhysicalDevices(instance.vkInstance(), &count, nullptr) != VK_SUCCESS || count == 0) {
        info.status = ProbeStatus::Unavailable;
        info.error = u"no Vulkan physical device"_s;
        return info;
    }
    std::vector<VkPhysicalDevice> devices(count);
    f->vkEnumeratePhysicalDevices(instance.vkInstance(), &count, devices.data());
    const auto getProperties2 = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties2>(
        instance.getInstanceProcAddr("vkGetPhysicalDeviceProperties2"));
    for (VkPhysicalDevice device : devices) {
        VkPhysicalDeviceProperties properties{};
        f->vkGetPhysicalDeviceProperties(device, &properties);
        VulkanDevice entry;
        entry.name = QString::fromUtf8(properties.deviceName);
        entry.vendorId = properties.vendorID;
        entry.deviceId = properties.deviceID;
        entry.type = toDeviceType(properties.deviceType);
        entry.apiVersion = vulkanVersionString(properties.apiVersion);
        if (getProperties2 && properties.apiVersion >= VK_API_VERSION_1_2) {
            VkPhysicalDeviceDriverProperties driver{};
            driver.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES;
            VkPhysicalDeviceProperties2 properties2{};
            properties2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
            properties2.pNext = &driver;
            getProperties2(device, &properties2);
            entry.driverName = QString::fromUtf8(driver.driverName);
            entry.driverInfo = QString::fromUtf8(driver.driverInfo);
        }
        VkPhysicalDeviceMemoryProperties memory{};
        f->vkGetPhysicalDeviceMemoryProperties(device, &memory);
        VkDeviceSize local = 0;
        for (uint32_t i = 0; i < memory.memoryHeapCount; ++i) {
            if (memory.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) {
                local += memory.memoryHeaps[i].size;
            }
        }
        entry.deviceLocalMemoryMB = static_cast<qint64>(local / (1024 * 1024));
        info.devices.push_back(entry);
    }
    info.status = ProbeStatus::Ok;
    return info;
}

OpenGLInfo probeOpenGL()
{
    OpenGLInfo info;
    QOpenGLContext context;
    if (!context.create()) {
        info.status = ProbeStatus::Unavailable;
        info.error = u"cannot create an OpenGL context"_s;
        return info;
    }
    QOffscreenSurface surface;
    surface.setFormat(context.format());
    surface.create();
    if (!surface.isValid() || !context.makeCurrent(&surface)) {
        info.status = ProbeStatus::Unavailable;
        info.error = u"cannot make the OpenGL context current"_s;
        return info;
    }
    QOpenGLFunctions *gl = context.functions();
    const auto string = [gl](GLenum name) {
        const auto *value = reinterpret_cast<const char *>(gl->glGetString(name));
        return value ? QString::fromUtf8(value) : QString();
    };
    info.gles = context.isOpenGLES();
    info.major = context.format().majorVersion();
    info.minor = context.format().minorVersion();
    info.version = string(GL_VERSION);
    info.glslVersion = string(GL_SHADING_LANGUAGE_VERSION);
    info.vendor = string(GL_VENDOR);
    info.renderer = string(GL_RENDERER);
    info.softwareRasterizer = isSoftwareRenderer(info.renderer);
    // Dedicated video memory, when the driver exposes it (NVIDIA). Vulkan reports it for every vendor.
    if (context.hasExtension("GL_NVX_gpu_memory_info")) {
        GLint kb = 0;
        gl->glGetIntegerv(0x9047 /* GL_GPU_MEMORY_INFO_DEDICATED_VIDMEM_NVX */, &kb);
        info.videoMemoryMB = kb / 1024;
    }
    context.doneCurrent();
    info.status = ProbeStatus::Ok;
    return info;
}

} // namespace vedit::gpu::probe
