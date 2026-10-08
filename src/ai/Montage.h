// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "engine/analysis/Decoding.h"

#include <QString>

#include <cstdint>
#include <vector>

namespace velacut::ai {

// How a shot looks, a few times a second (engine::extractShotSamples).
using ShotSample = engine::ShotSample;

// One of the files of a montage: a photo, or a video with its samples and length.
struct MontageSource
{
    bool photo = false;
    double seconds = 0.0; // length of a video
    std::vector<ShotSample> samples;
};

// The looks of the automatic montage (vlog, travel, sport, cinematic, party, meme, product).
struct MontageStyle
{
    QString id;
    int beatsMin = 2;       // beats per shot, between these
    int beatsMax = 4;
    double secondsPerCut = 2.0; // without music, or a song without a clear beat
    QString transition;     // "" = cuts
    double transitionSeconds = 0.0;
    QString filter;         // "" = none
    QString titleStyle;     // text style of the title
};
const std::vector<MontageStyle> &montageStyles();
const MontageStyle *montageStyle(const QString &id);

// A shot of the montage: `length` seconds of source `source` from its second `from` (0 for photos).
struct MontagePiece
{
    int source = 0;
    double from = 0.0;
    double length = 0.0;
    friend bool operator==(const MontagePiece &, const MontagePiece &) = default;
};

// The shots of a montage: lengths on the beats of the music (a style's beats per shot, varying) or the style's seconds,
// until `targetSeconds` (0 = every file once, the long videos twice); each file in turn, in an order and with moments
// that depend on `seed` (another seed: "Shuffle"); in a video, the best window of that length not used yet (sharp, lit,
// some movement but not shaky). The pieces end on beats.
std::vector<MontagePiece> planMontage(const std::vector<MontageSource> &sources, const std::vector<double> &beats,
                                      const MontageStyle &style, double targetSeconds, std::uint32_t seed);

} // namespace velacut::ai
