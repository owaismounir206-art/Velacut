// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QColor>
#include <QImage>
#include <QSize>

#include <vector>

namespace velacut::fx {

// Audio visualizers (SPEC §5.9): bars, spectrum, wave and pulsing circle drawn from the frequency bands of the audio
// under them, on a transparent canvas-sized layer (CPU, QPainter without text: safe in MLT's threads).
enum class VisualizerStyle
{
    Bars,
    Spectrum,
    Waveform,
    PulsingCircle,
};

struct VisualizerSettings
{
    VisualizerStyle style = VisualizerStyle::Bars;
    int barCount = 32;
    QColor primary{0, 220, 255};
    QColor secondary{255, 100, 200};
    double sensitivity = 1.0; // gain on the levels
    bool mirror = false;
    double roundness = 0.5; // bars: 0 square, 1 fully rounded
    double thickness = 3.0; // lines, in pixels at 1080p
};

struct VisualizerFrameData
{
    std::vector<double> bands; // one level 0–1 per bar
    double overallLevel = 0.0;
    double bassLevel = 0.0; // the lowest quarter of the bars
};

// Bars from spectrum band levels (0–1, low to high frequencies): every bar is the mean of its share of the bands,
// times `sensitivity`.
VisualizerFrameData visualizerFrame(const std::vector<float> &spectrumBands, int barCount, double sensitivity);

// Example levels for the library's thumbnails, where there is no audio: a steady pattern moving with `seconds`.
VisualizerFrameData exampleVisualizerFrame(double seconds, int barCount);

QImage renderAudioVisualizer(const VisualizerSettings &settings, const VisualizerFrameData &data, const QSize &canvasSize);

} // namespace velacut::fx
