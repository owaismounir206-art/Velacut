// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/time/Rational.h"
#include "fx/Loudness.h"
#include "fx/Stabilization.h"

#include <QByteArray>
#include <QImage>
#include <QString>

#include <atomic>
#include <functional>
#include <optional>
#include <vector>

namespace vedit::engine {

// Frames of a video (or the picture of an image) for thumbnails: `count` frames evenly spaced over the duration,
// `height` pixels high, displayed as the file says (rotation, pixel aspect), side by side in one RGB32 image.
// Decoded directly with FFmpeg (seek, then decode up to the wanted time), in the calling thread.
QImage extractThumbnailStrip(const QString &path, int count, int height, const std::atomic<bool> *cancel = nullptr);

// One frame of a video (or the picture of an image) as displayed (rotation and pixel aspect applied), the one shown at
// `seconds` from the start; at most `maxHeight` high (0 = the decoded size). Null if it cannot be decoded.
QImage extractFrame(const QString &path, double seconds, int maxHeight = 0);

// Peaks of the audio mixed down to mono: for every 1/`bucketsPerSecond` s a (min, max) pair of int8 (-127…127).
struct Waveform
{
    int bucketsPerSecond = 100;
    QByteArray peaks; // min0, max0, min1, max1…

    int bucketCount() const { return static_cast<int>(peaks.size() / 2); }
};
std::optional<Waveform> extractWaveform(const QString &path, int bucketsPerSecond = 100,
                                        const std::atomic<bool> *cancel = nullptr);

// Called now and then with the share of the file done (0…1), from the decoding thread.
using DecodeProgress = std::function<void(double)>;

// Loudness of the audio mixed down to mono, as RMS level in dBFS (−100 for silence) for every 1/`windowsPerSecond` s
// (the analysis behind "Remove pauses").
std::optional<std::vector<float>> extractLevels(const QString &path, int windowsPerSecond,
                                                const std::atomic<bool> *cancel = nullptr,
                                                const DecodeProgress &progress = {});

// How much each frame differs from the previous one (0 = same picture, 1 = everything changed), from small grey
// pictures (pixel and histogram differences): the analysis behind "Split scenes". `times` gets the time of each
// frame in seconds of the file; the first frame's difference is 0.
std::optional<std::vector<float>> extractFrameDifferences(const QString &path, std::vector<double> *times,
                                                          const std::atomic<bool> *cancel = nullptr,
                                                          const DecodeProgress &progress = {});

// How the camera moved between consecutive frames from `fromSeconds` to `toSeconds` of the file (the analysis behind
// "Stabilize"); the first step is zero. `frameRate` gets the video's frame rate.
std::optional<std::vector<fx::CameraStep>> extractCameraSteps(const QString &path, double fromSeconds, double toSeconds,
                                                              Rational *frameRate, const std::atomic<bool> *cancel = nullptr,
                                                              const DecodeProgress &progress = {});

// Integrated and peak loudness measured via ITU-R BS.1770-4 / EBU R128.
std::optional<fx::LoudnessResult> extractLoudness(const QString &path,
                                                  const std::atomic<bool> *cancel = nullptr);

} // namespace vedit::engine
