// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QImage>
#include <QString>

#include <atomic>
#include <optional>

namespace vedit::engine {

// Frames of a video (or the picture of an image) for thumbnails: `count` frames evenly spaced over the duration,
// `height` pixels high, displayed as the file says (rotation, pixel aspect), side by side in one RGB32 image.
// Decoded directly with FFmpeg (seek, then decode up to the wanted time), in the calling thread.
QImage extractThumbnailStrip(const QString &path, int count, int height, const std::atomic<bool> *cancel = nullptr);

// Peaks of the audio mixed down to mono: for every 1/`bucketsPerSecond` s a (min, max) pair of int8 (-127…127).
struct Waveform
{
    int bucketsPerSecond = 100;
    QByteArray peaks; // min0, max0, min1, max1…

    int bucketCount() const { return static_cast<int>(peaks.size() / 2); }
};
std::optional<Waveform> extractWaveform(const QString &path, int bucketsPerSecond = 100,
                                        const std::atomic<bool> *cancel = nullptr);

} // namespace vedit::engine
