// SPDX-License-Identifier: GPL-3.0-or-later
#include "Montage.h"

#include <QStringLiteral>

#include <algorithm>
#include <cmath>
#include <numeric>
#include <random>

namespace vedit::ai {

const std::vector<MontageStyle> &montageStyles()
{
    static const std::vector<MontageStyle> styles{
        {QStringLiteral("vlog"), 2, 4, 2.0, QString(), 0.0, QStringLiteral("filters/natural"), QStringLiteral("text/anim-pop-in-bold")},
        {QStringLiteral("travel"), 2, 4, 2.5, QStringLiteral("transitions/whip-pan-left"), 0.3, QStringLiteral("filters/vivid"),
         QStringLiteral("text/place-journey")},
        {QStringLiteral("sport"), 1, 2, 1.0, QStringLiteral("transitions/zoom-in"), 0.2, QStringLiteral("filters/crisp"),
         QStringLiteral("text/anim-slide-down-bold")},
        {QStringLiteral("cinematic"), 4, 8, 3.5, QStringLiteral("transitions/dissolve"), 0.8, QStringLiteral("filters/teal-orange"),
         QStringLiteral("text/anim-typewriter-cinematic")},
        {QStringLiteral("party"), 1, 2, 1.0, QStringLiteral("transitions/exposure-flash"), 0.2, QStringLiteral("filters/neon-night"),
         QStringLiteral("text/anim-bounce-pop")},
        {QStringLiteral("meme"), 1, 3, 1.5, QString(), 0.0, QStringLiteral("filters/pop"), QStringLiteral("text/bubble-comic-shout")},
        {QStringLiteral("product"), 2, 4, 2.0, QStringLiteral("transitions/slide-left"), 0.4, QStringLiteral("filters/clear"),
         QStringLiteral("text/cta-shop-now")},
    };
    return styles;
}

const MontageStyle *montageStyle(const QString &id)
{
    const auto &styles = montageStyles();
    const auto it = std::find_if(styles.begin(), styles.end(), [&id](const MontageStyle &s) { return s.id == id; });
    return it == styles.end() ? nullptr : &*it;
}

namespace {

// How good a moment is, 0…1.
double sampleScore(const ShotSample &sample, float sharpest)
{
    const double sharp = sharpest > 0 ? sample.sharpness / sharpest : 0.0;
    // Lit: dark or burnt frames are not good moments.
    const double light = 1.0 - std::clamp((std::abs(sample.brightness - 0.5) - 0.25) / 0.2, 0.0, 1.0);
    // Some movement brings life; too much is a shake or a whip.
    const double motion = sample.motion < 0.01 ? 0.3 : sample.motion < 0.12 ? 1.0 : std::max(0.0, 1.0 - (sample.motion - 0.12) * 5.0);
    return 0.45 * sharp + 0.3 * light + 0.25 * motion;
}

} // namespace

std::vector<MontagePiece> planMontage(const std::vector<MontageSource> &sources, const std::vector<double> &beats,
                                      const MontageStyle &style, double targetSeconds, std::uint32_t seed)
{
    std::vector<MontagePiece> pieces;
    if (sources.empty()) {
        return pieces;
    }
    std::mt19937 random(seed);
    // The order of the files: as given with seed 0, shuffled otherwise.
    std::vector<int> order(sources.size());
    std::iota(order.begin(), order.end(), 0);
    if (seed != 0) {
        std::shuffle(order.begin(), order.end(), random);
    }
    // How many shots: up to the target, else every file once (videos longer than 8 s twice).
    std::vector<int> turns;
    if (targetSeconds <= 0.0) {
        for (const int index : order) {
            turns.push_back(index);
        }
        for (const int index : order) {
            if (!sources[static_cast<size_t>(index)].photo && sources[static_cast<size_t>(index)].seconds > 8.0) {
                turns.push_back(index);
            }
        }
    }
    // The ends of the shots: on beats (every few beats), else every few seconds.
    std::vector<std::vector<std::pair<double, double>>> used(sources.size()); // windows taken in each video
    double time = 0.0;
    size_t beat = 0; // index of the beat where the current shot starts
    std::uniform_int_distribution<int> beatsPerShot(style.beatsMin, std::max(style.beatsMin, style.beatsMax));
    for (size_t turn = 0;; ++turn) {
        if (targetSeconds > 0.0 ? time >= targetSeconds - 0.25 : turn >= turns.size()) {
            break;
        }
        const int index = targetSeconds > 0.0 ? order[turn % order.size()] : turns[turn];
        const MontageSource &source = sources[static_cast<size_t>(index)];
        double length = style.secondsPerCut;
        if (beats.size() > 1) {
            while (beat < beats.size() && beats[beat] < time + 0.05) {
                ++beat;
            }
            const size_t next = beat + static_cast<size_t>(beatsPerShot(random)) - 1;
            if (next < beats.size()) {
                length = beats[next] - time;
                beat = next + 1;
            }
        }
        if (targetSeconds > 0.0) {
            length = std::min(length, std::max(0.5, targetSeconds - time));
        }
        MontagePiece piece{index, 0.0, length};
        if (!source.photo) {
            piece.length = std::min(length, source.seconds);
            // The best window of that length that does not overlap one already used.
            float sharpest = 0.0f;
            for (const ShotSample &sample : source.samples) {
                sharpest = std::max(sharpest, sample.sharpness);
            }
            double best = -1.0;
            double bestFrom = 0.0;
            const double step = 0.25;
            for (double from = 0.0; from + piece.length <= source.seconds + 1e-6; from += step) {
                const double to = from + piece.length;
                const bool overlaps = std::any_of(used[static_cast<size_t>(index)].begin(), used[static_cast<size_t>(index)].end(),
                                                  [&](const auto &w) { return from < w.second && to > w.first; });
                if (overlaps) {
                    continue;
                }
                double sum = 0.0;
                int count = 0;
                for (const ShotSample &sample : source.samples) {
                    if (sample.seconds >= from && sample.seconds < to) {
                        sum += sampleScore(sample, sharpest);
                        ++count;
                    }
                }
                // A little randomness lets "Shuffle" pick other good moments.
                const double jitter = seed == 0 ? 0.0 : std::uniform_real_distribution<double>(0.0, 0.08)(random);
                const double score = (count > 0 ? sum / count : 0.0) + jitter;
                if (score > best) {
                    best = score;
                    bestFrom = from;
                }
            }
            if (best < 0.0) {
                bestFrom = 0.0; // every window used: start again from the beginning
            }
            piece.from = bestFrom;
            used[static_cast<size_t>(index)].emplace_back(piece.from, piece.from + piece.length);
        }
        if (piece.length <= 0.05) {
            continue;
        }
        pieces.push_back(piece);
        time += piece.length;
        if (pieces.size() > 400) {
            break;
        }
    }
    return pieces;
}

} // namespace vedit::ai
