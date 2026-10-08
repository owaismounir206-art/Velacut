// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QByteArray>

namespace velacut::engine {

// Audio levels for the mixer's meters (SPEC §5.9): the vedit.gain filters report the peak of every audio block they
// process, per key (a track id, or "master"); the interface reads them ~30 times per second. Levels fall back to
// silence when nothing is reported (paused). Thread-safe.
class AudioMeters
{
public:
    static void report(const QByteArray &key, float peak);
    // 0…1 (and above when clipping), with a peak hold that decays over ~1 s.
    static float level(const QByteArray &key);
    static void clear();
};

} // namespace velacut::engine
