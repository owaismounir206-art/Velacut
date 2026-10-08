// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <vector>

namespace velacut::fx {

// Effects on the beat (SPEC §5.9): a pulse at every beat that fades out in `decaySeconds`, driving a flash, a zoom or a
// shake of the picture. CPU reference kernels on RGBA 8-bit images.

// The pulse (1 on a beat, exponential decay to ~0 after `decaySeconds`) at `timeSeconds`; `beats` in ascending order.
// No beats: no pulse.
double computeBeatPulse(double timeSeconds, const std::vector<double> &beats, double decaySeconds = 0.18);

// Beat Flash: bright punch towards white at beat points.
void applyBeatFlash(uint8_t *rgba, int width, int height, double pulse, double intensity = 0.7);

// Beat Zoom: punch zoom-in around canvas center at beat points with bilinear filtering.
void applyBeatZoom(uint8_t *dst, const uint8_t *src, int width, int height, double pulse,
                   double zoomFactor = 0.15);

// Beat Shake: rhythmic camera shake at beat points with bilinear filtering.
void applyBeatShake(uint8_t *dst, const uint8_t *src, int width, int height, double pulse,
                    double timeSeconds, double maxShakePixels = 12.0);

} // namespace velacut::fx
