// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/project/Media.h"

#include <QByteArray>
#include <QString>

#include <atomic>
#include <optional>
#include <vector>

namespace velacut::engine {

// Loudness of the audio in log-spaced frequency bands over time, for the audio visualizers and the beat detection
// (docs/ARCHITECTURE.md D-49). The audio is mixed down to mono at 22 050 Hz; every 1/`framesPerSecond` s a 2048-sample
// Hann window is analysed with an FFT and its power summed into `bands` bands from 40 Hz to 11 kHz.
// A level byte is the band's level in dB mapped linearly: 0 = −70 dBFS or less, 255 = 0 dBFS (a full-scale sine).
struct Spectrum
{
    static constexpr int kSampleRate = 22050;
    static constexpr int kWindow = 2048;
    static constexpr double kFloorDb = -70.0;
    static constexpr double kLowHz = 40.0;
    static constexpr double kHighHz = 11000.0;

    int framesPerSecond = 30;
    int bands = 32;
    QByteArray levels; // frame after frame, `bands` bytes each

    int frameCount() const { return bands > 0 ? static_cast<int>(levels.size() / bands) : 0; }
    double seconds() const { return framesPerSecond > 0 ? static_cast<double>(frameCount()) / framesPerSecond : 0.0; }
    // Levels 0–1 of every band at `seconds` from the start (linear between analysis frames); all 0 outside the audio.
    void levelsAt(double seconds, std::vector<float> &out) const;
    // Overall level 0–1 at `seconds` (the mean of the bands).
    double levelAt(double seconds) const;
};

// Decodes the file's audio (FFmpeg, in the calling thread). Null without audio, on error or if cancelled.
std::optional<Spectrum> extractSpectrum(const QString &path, int framesPerSecond = 30, int bands = 32,
                                        const std::atomic<bool> *cancel = nullptr);

// Disk cache format: "VSPC", version byte, framesPerSecond (uint16), bands (uint16), then the levels.
QByteArray encodeSpectrum(const Spectrum &spectrum);
std::optional<Spectrum> decodeSpectrum(const QByteArray &bytes);

// The spectrum of a media item from the disk cache (<cache>/media/<fingerprint>/spectrum-30x32.bin, next to the
// waveform), extracted and stored first if missing. Decodes in the calling thread: never on the UI thread.
std::optional<Spectrum> cachedSpectrum(const Media &media, const std::atomic<bool> *cancel = nullptr);
QString spectrumCacheFile(const Media &media);

} // namespace velacut::engine
