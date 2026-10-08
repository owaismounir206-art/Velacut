// SPDX-License-Identifier: GPL-3.0-or-later
#include "Spectrum.h"

#include "common/Paths.h"
#include "core/serialization/ProjectFile.h"

#include <QFile>

#include <algorithm>
#include <cmath>

namespace velacut::engine {

void Spectrum::levelsAt(double seconds, std::vector<float> &out) const
{
    out.assign(static_cast<size_t>(std::max(0, bands)), 0.0f);
    const int frames = frameCount();
    if (frames == 0 || framesPerSecond <= 0 || seconds < 0.0) {
        return;
    }
    const double position = seconds * framesPerSecond;
    const int first = static_cast<int>(std::floor(position));
    if (first >= frames) {
        return;
    }
    const int second = std::min(first + 1, frames - 1);
    const auto t = static_cast<float>(position - first);
    const auto *data = reinterpret_cast<const unsigned char *>(levels.constData());
    for (int band = 0; band < bands; ++band) {
        const float a = data[first * bands + band] / 255.0f;
        const float b = data[second * bands + band] / 255.0f;
        out[static_cast<size_t>(band)] = a + (b - a) * t;
    }
}

double Spectrum::levelAt(double seconds) const
{
    std::vector<float> values;
    levelsAt(seconds, values);
    if (values.empty()) {
        return 0.0;
    }
    double sum = 0.0;
    for (const float value : values) {
        sum += value;
    }
    return sum / static_cast<double>(values.size());
}

QByteArray encodeSpectrum(const Spectrum &spectrum)
{
    QByteArray bytes("VSPC");
    bytes.append(char(1));
    const auto put16 = [&bytes](int value) {
        bytes.append(static_cast<char>(value & 0xff));
        bytes.append(static_cast<char>((value >> 8) & 0xff));
    };
    put16(spectrum.framesPerSecond);
    put16(spectrum.bands);
    bytes.append(spectrum.levels);
    return bytes;
}

std::optional<Spectrum> decodeSpectrum(const QByteArray &bytes)
{
    constexpr int header = 9;
    if (bytes.size() < header || !bytes.startsWith("VSPC") || bytes.at(4) != 1) {
        return std::nullopt;
    }
    const auto *data = reinterpret_cast<const unsigned char *>(bytes.constData());
    Spectrum spectrum;
    spectrum.framesPerSecond = data[5] | (data[6] << 8);
    spectrum.bands = data[7] | (data[8] << 8);
    if (spectrum.framesPerSecond <= 0 || spectrum.bands <= 0 || (bytes.size() - header) % spectrum.bands != 0) {
        return std::nullopt;
    }
    spectrum.levels = bytes.mid(header);
    return spectrum;
}

QString spectrumCacheFile(const Media &media)
{
    return paths::cacheDir() + QStringLiteral("/media/") + media.fingerprint.value + QStringLiteral("/spectrum-30x32.bin");
}

std::optional<Spectrum> cachedSpectrum(const Media &media, const std::atomic<bool> *cancel)
{
    if (!media.info.audio || !media.fingerprint.isValid()) {
        return std::nullopt;
    }
    const QString file = spectrumCacheFile(media);
    QFile stored(file);
    if (stored.open(QIODevice::ReadOnly)) {
        if (auto loaded = decodeSpectrum(stored.readAll())) {
            return loaded;
        }
    }
    std::optional<Spectrum> computed = extractSpectrum(media.path, 30, 32, cancel);
    if (computed) {
        projectfile::writeAtomically(file, encodeSpectrum(*computed));
    }
    return computed;
}

} // namespace velacut::engine
