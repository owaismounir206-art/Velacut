// SPDX-License-Identifier: GPL-3.0-or-later
// Probes run inside the isolated velacut-gpuprobe process: a driver crash or hang only kills the probe.
#pragma once

#include "engine/gpu/GpuCapabilities.h"

namespace velacut::gpu::probe {

VulkanInfo probeVulkan();
OpenGLInfo probeOpenGL();
// Opens the FFmpeg hardware devices and really encodes/decodes a few frames with each candidate.
VideoInfo probeVideo();

} // namespace velacut::gpu::probe
