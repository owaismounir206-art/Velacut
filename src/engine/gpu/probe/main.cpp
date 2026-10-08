// SPDX-License-Identifier: GPL-3.0-or-later
// velacut-gpuprobe: probes one subsystem per run and prints GpuCapabilities JSON on stdout.
// The main application runs it as a child process (docs/ARCHITECTURE.md §7.1), so that a crashing or
// hanging driver can never take the editor down.
//   velacut-gpuprobe --vulkan | --opengl | --video
#include "Probes.h"

#include "common/DevSandbox.h"

#include <QGuiApplication>
#include <QJsonDocument>

#include <cstdio>
#include <cstring>

int main(int argc, char **argv)
{
    velacut::applyDevSandbox();
    qputenv("SVT_LOG", "1"); // SVT-AV1 (used for the AV1 test stream): errors only
    const char *mode = argc > 1 ? argv[1] : "";
    velacut::gpu::GpuCapabilities caps;
    if (std::strcmp(mode, "--video") == 0) {
        caps.video = velacut::gpu::probe::probeVideo();
    } else if (std::strcmp(mode, "--vulkan") == 0 || std::strcmp(mode, "--opengl") == 0) {
        QGuiApplication app(argc, argv); // platform integration needed for Vulkan/OpenGL contexts
        if (std::strcmp(mode, "--vulkan") == 0) {
            caps.vulkan = velacut::gpu::probe::probeVulkan();
        } else {
            caps.opengl = velacut::gpu::probe::probeOpenGL();
        }
    } else {
        std::fprintf(stderr, "usage: %s --vulkan | --opengl | --video\n", argv[0]);
        return 2;
    }
    const QByteArray json = QJsonDocument(caps.toJson()).toJson(QJsonDocument::Compact);
    std::fwrite(json.constData(), 1, static_cast<size_t>(json.size()), stdout);
    std::fputc('\n', stdout);
    std::fflush(stdout); // before any sanitizer or driver teardown code runs at exit
    return 0;
}
