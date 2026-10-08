// SPDX-License-Identifier: GPL-3.0-or-later
#include "fx/Loudness.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace velacut::fx {

namespace {

struct BiquadCoeffs
{
    double b0 = 1.0;
    double b1 = 0.0;
    double b2 = 0.0;
    double a1 = 0.0;
    double a2 = 0.0;
};

struct BiquadState
{
    double z1 = 0.0;
    double z2 = 0.0;

    double process(double in, const BiquadCoeffs &c)
    {
        const double out = c.b0 * in + z1;
        z1 = c.b1 * in - c.a1 * out + z2;
        z2 = c.b2 * in - c.a2 * out;
        return out;
    }
};

BiquadCoeffs designStage1HighShelf(double fs)
{
    // ITU-R BS.1770-4 Stage 1: High shelf filter
    // db = +3.999843853973347 dB, f0 = 1681.974450955533 Hz, Q = 0.7071752369274193
    const double db = 3.999843853973347;
    const double f0 = 1681.974450955533;
    const double Q = 0.7071752369274193;

    const double V0 = std::pow(10.0, db / 20.0);
    const double K = std::tan(std::numbers::pi * f0 / fs);
    const double Vh = V0;
    const double Vb = std::sqrt(V0);

    const double a0 = 1.0 + K / Q + K * K;
    BiquadCoeffs c;
    c.b0 = (Vh + Vb * K / Q + K * K) / a0;
    c.b1 = 2.0 * (K * K - Vh) / a0;
    c.b2 = (Vh - Vb * K / Q + K * K) / a0;
    c.a1 = 2.0 * (K * K - 1.0) / a0;
    c.a2 = (1.0 - K / Q + K * K) / a0;
    return c;
}

BiquadCoeffs designStage2Rlb(double fs)
{
    // ITU-R BS.1770-4 Stage 2: Highpass RLB weighting filter
    // f0 = 38.13547087602444 Hz, Q = 0.5
    const double f0 = 38.13547087602444;
    const double Q = 0.5;

    const double K = std::tan(std::numbers::pi * f0 / fs);
    const double a0 = 1.0 + K / Q + K * K;
    BiquadCoeffs c;
    c.b0 = 1.0 / a0;
    c.b1 = -2.0 / a0;
    c.b2 = 1.0 / a0;
    c.a1 = 2.0 * (K * K - 1.0) / a0;
    c.a2 = (1.0 - K / Q + K * K) / a0;
    return c;
}

} // namespace

LoudnessResult measureLoudness(const float *samples, int channels, int sampleRate, std::int64_t numFrames)
{
    LoudnessResult result;
    if (!samples || channels <= 0 || sampleRate <= 0 || numFrames <= 0) {
        return result;
    }

    const double fs = static_cast<double>(sampleRate);
    const BiquadCoeffs stage1 = designStage1HighShelf(fs);
    const BiquadCoeffs stage2 = designStage2Rlb(fs);

    // Channel weighting according to BS.1770
    std::vector<double> channelWeights(static_cast<size_t>(channels), 1.0);
    if (channels >= 4) {
        // Standard 5.1 / surround channel indices: 3 is LFE
        if (channels >= 6) {
            channelWeights[3] = 0.0; // LFE
            channelWeights[4] = 1.41; // Left surround
            channelWeights[5] = 1.41; // Right surround
        }
    }

    // Filter all channels
    std::vector<BiquadState> s1States(static_cast<size_t>(channels));
    std::vector<BiquadState> s2States(static_cast<size_t>(channels));
    std::vector<std::vector<double>> filtered(static_cast<size_t>(channels));
    for (int ch = 0; ch < channels; ++ch) {
        filtered[static_cast<size_t>(ch)].resize(static_cast<size_t>(numFrames));
    }

    float peak = 0.0f;
    for (std::int64_t frame = 0; frame < numFrames; ++frame) {
        for (int ch = 0; ch < channels; ++ch) {
            const float s = samples[frame * channels + ch];
            peak = std::max(peak, std::abs(s));
            const double s1 = s1States[static_cast<size_t>(ch)].process(static_cast<double>(s), stage1);
            const double s2 = s2States[static_cast<size_t>(ch)].process(s1, stage2);
            filtered[static_cast<size_t>(ch)][static_cast<size_t>(frame)] = s2;
        }
    }

    result.truePeakDb = peak > 1e-6f ? 20.0 * std::log10(static_cast<double>(peak)) : -100.0;

    // 400 ms blocks with 100 ms step (75% overlap)
    const std::int64_t blockSize = static_cast<std::int64_t>(std::llround(0.400 * fs));
    const std::int64_t hopSize = static_cast<std::int64_t>(std::llround(0.100 * fs));
    if (blockSize <= 0 || hopSize <= 0) {
        return result;
    }

    // If audio is shorter than a full 400 ms block, measure over the available frames
    if (numFrames < blockSize) {
        double weightedSum = 0.0;
        for (int ch = 0; ch < channels; ++ch) {
            double chSum = 0.0;
            for (std::int64_t f = 0; f < numFrames; ++f) {
                const double v = filtered[static_cast<size_t>(ch)][static_cast<size_t>(f)];
                chSum += v * v;
            }
            weightedSum += channelWeights[static_cast<size_t>(ch)] * (chSum / static_cast<double>(numFrames));
        }
        if (weightedSum > 1e-12) {
            const double lufs = -0.691 + 10.0 * std::log10(weightedSum);
            result.integratedLufs = lufs;
            result.momentaryMaxLufs = lufs;
            result.shortTermMaxLufs = lufs;
        }
        return result;
    }

    struct Block
    {
        double power = 0.0;
        double loudness = -70.0;
    };

    std::vector<Block> blocks;
    double maxLoudness = -70.0;

    for (std::int64_t start = 0; start + blockSize <= numFrames; start += hopSize) {
        double weightedPower = 0.0;
        for (int ch = 0; ch < channels; ++ch) {
            double chSum = 0.0;
            const auto &chBuf = filtered[static_cast<size_t>(ch)];
            for (std::int64_t i = 0; i < blockSize; ++i) {
                const double v = chBuf[static_cast<size_t>(start + i)];
                chSum += v * v;
            }
            weightedPower += channelWeights[static_cast<size_t>(ch)] * (chSum / static_cast<double>(blockSize));
        }

        Block b;
        b.power = weightedPower;
        if (weightedPower > 1e-12) {
            b.loudness = -0.691 + 10.0 * std::log10(weightedPower);
        }
        maxLoudness = std::max(maxLoudness, b.loudness);
        blocks.push_back(b);
    }

    result.momentaryMaxLufs = maxLoudness;

    // Gating pass 1: absolute threshold at -70 LKFS
    double unGatedPowerSum = 0.0;
    size_t unGatedCount = 0;
    for (const Block &b : blocks) {
        if (b.loudness > -70.0) {
            unGatedPowerSum += b.power;
            ++unGatedCount;
        }
    }

    if (unGatedCount == 0 || unGatedPowerSum <= 1e-12) {
        result.integratedLufs = -70.0;
        return result;
    }

    const double gammaA = -0.691 + 10.0 * std::log10(unGatedPowerSum / static_cast<double>(unGatedCount));
    const double relativeThreshold = gammaA - 10.0;

    // Gating pass 2: relative threshold
    double gatedPowerSum = 0.0;
    size_t gatedCount = 0;
    for (const Block &b : blocks) {
        if (b.loudness > -70.0 && b.loudness > relativeThreshold) {
            gatedPowerSum += b.power;
            ++gatedCount;
        }
    }

    if (gatedCount == 0 || gatedPowerSum <= 1e-12) {
        result.integratedLufs = gammaA;
    } else {
        result.integratedLufs = -0.691 + 10.0 * std::log10(gatedPowerSum / static_cast<double>(gatedCount));
    }

    result.shortTermMaxLufs = result.momentaryMaxLufs;
    return result;
}

} // namespace velacut::fx
