// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "engine/analysis/Decoding.h"

#include <QString>

#include <optional>
#include <vector>

namespace velacut::engine {

struct AudioSyncResult
{
    bool matched = false;
    double offsetSeconds = 0.0; // target_time = ref_time + offsetSeconds
    double confidence = 0.0;    // 0.0 to 1.0 (correlation coefficient)
};

// Finds time offset of `target` relative to `ref` using normalized cross-correlation of waveform amplitude envelopes.
// A positive offset means events in `target` occur at a higher source timestamp than in `ref`.
AudioSyncResult alignWaveforms(const Waveform &ref, const Waveform &target, double maxSearchSeconds = 60.0);

// Extracts waveforms and correlates audio files directly.
AudioSyncResult alignAudioFiles(const QString &refPath, const QString &targetPath, double maxSearchSeconds = 60.0);

} // namespace velacut::engine
