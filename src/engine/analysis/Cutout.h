// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/project/Clip.h"
#include "core/project/Media.h"

#include <QString>

#include <atomic>
#include <functional>
#include <optional>

namespace velacut::engine {

// "Remove background" (MediaClipData::cutout, SPEC §5.12): the part of a file a clip plays with the background made
// transparent, as a copy in the cache (QuickTime RLE with alpha — kept by MLT, verified — and the original sound).
// The frames are cut out by rembg (MIT), an optional external program; whole seconds, rounded outward.
struct CutoutCopy
{
    Media source;
    RationalTime from;
    RationalTime to;

    QString path() const;
    bool ready() const;
    Media asMedia() const;
};

std::optional<CutoutCopy> cutoutCopyFor(const Clip &clip, const Media &media, bool evenWhenOff = false);

namespace rembg {
QString executable();
QString installCommand();
// Whether rembg already has its model (it downloads it, about 170 MB, the first time it runs).
bool hasModel();
} // namespace rembg

// Makes the copy now: frames by ffmpeg, cut out by `rembg p`, put back together with the sound. An error for the
// user, empty when done.
QString makeCutoutCopy(const CutoutCopy &copy, const std::function<void(double)> &progress = {},
                       const std::atomic<bool> *cancel = nullptr);

} // namespace velacut::engine
