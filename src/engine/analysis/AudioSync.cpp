// SPDX-License-Identifier: GPL-3.0-or-later
#include "AudioSync.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numeric>
#include <vector>

namespace vedit::engine {

namespace {

std::vector<double> extractEnvelope(const Waveform &wf)
{
    const int count = wf.bucketCount();
    std::vector<double> env;
    env.reserve(count);
    const auto *p = reinterpret_cast<const std::int8_t *>(wf.peaks.constData());
    for (int i = 0; i < count; ++i) {
        const double mn = p[2 * i];
        const double mx = p[2 * i + 1];
        env.push_back((mx - mn) / 2.0);
    }
    return env;
}

} // namespace

AudioSyncResult alignWaveforms(const Waveform &ref, const Waveform &target, double maxSearchSeconds)
{
    AudioSyncResult result;
    const int bps = ref.bucketsPerSecond > 0 ? ref.bucketsPerSecond : 100;
    const std::vector<double> A = extractEnvelope(ref);
    const std::vector<double> B = extractEnvelope(target);
    const int N = static_cast<int>(A.size());
    const int M = static_cast<int>(B.size());

    if (N < 20 || M < 20) {
        return result;
    }

    const double minA = *std::min_element(A.begin(), A.end());
    const double minB = *std::min_element(B.begin(), B.end());

    std::vector<double> normA(N);
    std::vector<double> normB(M);
    double energyA = 0.0;
    double energyB = 0.0;
    for (int i = 0; i < N; ++i) {
        normA[i] = std::max(0.0, A[i] - minA);
        energyA += normA[i] * normA[i];
    }
    for (int j = 0; j < M; ++j) {
        normB[j] = std::max(0.0, B[j] - minB);
        energyB += normB[j] * normB[j];
    }

    const double totalDenom = std::sqrt(energyA * energyB);
    if (totalDenom <= 1e-6) {
        return result;
    }

    const int maxShift = std::min({static_cast<int>(std::ceil(maxSearchSeconds * bps)), N, M});
    double bestCorr = -1.0;
    int bestShift = 0;

    for (int k = -maxShift; k <= maxShift; ++k) {
        const int iStart = std::max(0, -k);
        const int iEnd = std::min(N, M - k);
        if (iEnd <= iStart) {
            continue;
        }

        double sumAB = 0.0;
        for (int i = iStart; i < iEnd; ++i) {
            sumAB += normA[i] * normB[i + k];
        }

        const double r = sumAB / totalDenom;
        if (r > bestCorr) {
            bestCorr = r;
            bestShift = k;
        }
    }

    if (bestCorr >= 0.25) {
        result.matched = true;
        result.confidence = std::clamp(bestCorr, 0.0, 1.0);
        result.offsetSeconds = static_cast<double>(bestShift) / bps;
    }
    return result;
}

AudioSyncResult alignAudioFiles(const QString &refPath, const QString &targetPath, double maxSearchSeconds)
{
    const auto refWf = extractWaveform(refPath);
    const auto tgtWf = extractWaveform(targetPath);
    if (!refWf || !tgtWf) {
        return AudioSyncResult{};
    }
    return alignWaveforms(*refWf, *tgtWf, maxSearchSeconds);
}

} // namespace vedit::engine
