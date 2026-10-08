// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/project/Clip.h"

#include <QImage>
#include <QPainterPath>
#include <QRectF>
#include <QSize>

#include <cstdint>
#include <memory>
#include <vector>

namespace velacut::engine {

// A caption line ready to be drawn: its words as outlines laid out on the canvas, grouped as the caption style shows
// them, with their times in frames from the clip's start. Built once with fonts (CaptionRenderer::layout), then
// drawn without them, so frames can be drawn in MLT's threads (no per-thread font data, D-39).
struct CaptionLayout
{
    struct Word
    {
        QPainterPath path; // canvas pixels
        QRectF box;        // advance × line height, canvas pixels
        std::int64_t start = 0;
        std::int64_t end = 0;
    };
    struct Group
    {
        int first = 0;
        int count = 0;
        std::int64_t start = 0;
        std::int64_t end = 0;
        std::vector<QRectF> lines; // one box per line of text
        QRectF bounds;
    };

    QSize canvas;
    double fps = 30.0;
    double pixelSize = 0.0;
    CaptionStyle style; // with the line's own text style, if it has one
    std::vector<Word> words;
    std::vector<Group> groups;
    // Bilingual captions: the whole line in the other language, smaller, under the lowest line of the groups, shown
    // with every group (canvas pixels).
    QPainterPath translation;
    std::vector<QRectF> translationLines;
    QRectF translationBounds;
};

// Social media captions (SPEC §5.8): the line, or groups of a few words following the speech, at the style's height
// on the canvas, with the word being said highlighted (colour, size, box or karaoke fill) and an entry animation for
// each group. CPU (QPainter); the clip's transform then places the layer like any text.
class CaptionRenderer
{
public:
    // Uses fonts: call it on a thread that is not one of MLT's (the projection's, or the GUI's).
    static std::shared_ptr<CaptionLayout> layout(const SubtitleClipData &line, const CaptionStyle &style,
                                                 std::int64_t lengthFrames, QSize canvas, Rational frameRate);
    // Straight-alpha RGBA8888 image of `size` (the layout scaled to it) at `frame` from the clip's start. No fonts.
    static QImage render(const CaptionLayout &layout, std::int64_t frame, QSize size);
    // What render() draws depends only on this key: equal keys, equal images (frames can reuse the last one).
    static std::int64_t stateKey(const CaptionLayout &layout, std::int64_t frame);
    // The rectangle of the group shown at `frame` (canvas pixels), empty when none is shown.
    static QRectF bounds(const CaptionLayout &layout, std::int64_t frame);
};

} // namespace velacut::engine
