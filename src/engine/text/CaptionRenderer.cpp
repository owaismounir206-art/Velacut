// SPDX-License-Identifier: GPL-3.0-or-later
#include "CaptionRenderer.h"

#include "common/fonts/BundledFonts.h"
#include "core/project/Captions.h"
#include "fx/Color.h"

#include <QFont>
#include <QFontMetricsF>
#include <QPainter>
#include <QTransform>

#include <algorithm>
#include <cmath>

namespace vedit::engine {

namespace {

// Lengths of the animations, in seconds.
constexpr double kPopSeconds = 0.2;
constexpr double kFadeSeconds = 0.2;
constexpr double kBounceSeconds = 0.35;
constexpr double kWordGrowSeconds = 0.12;
// The size of the word being said with the "scale" highlight, and the room around the box of the "box" highlight
// (share of the font size).
constexpr double kWordScale = 1.25;
constexpr double kBoxPadX = 0.14;
constexpr double kBoxPadY = 0.03;
constexpr double kBoxRadius = 0.2;
// Lines of captions never get wider than this share of the canvas (safe from the edges and the buttons of the apps).
constexpr double kMaxLineWidth = 0.84;

QColor toQColor(const Color &c)
{
    return QColor(c.r, c.g, c.b, c.a);
}

Color colorOf(const Param &param, Color fallback)
{
    const ParamValue value = param.staticValue();
    return std::holds_alternative<Color>(value) ? std::get<Color>(value) : fallback;
}

double numberOf(const Param &param, double fallback)
{
    const ParamValue value = param.staticValue();
    return std::holds_alternative<double>(value) ? std::get<double>(value) : fallback;
}

std::int64_t framesOf(double seconds, double fps)
{
    return std::max<std::int64_t>(1, static_cast<std::int64_t>(std::ceil(seconds * fps)));
}

double progress(std::int64_t done, std::int64_t length)
{
    return length <= 0 ? 1.0 : std::clamp(static_cast<double>(done) / static_cast<double>(length), 0.0, 1.0);
}

double easeOutCubic(double t)
{
    const double u = 1.0 - t;
    return 1.0 - u * u * u;
}

double easeOutBack(double t)
{
    constexpr double c1 = 1.70158;
    constexpr double c3 = c1 + 1.0;
    const double u = t - 1.0;
    return 1.0 + c3 * u * u * u + c1 * u * u;
}

double easeOutBounce(double t)
{
    constexpr double n = 7.5625;
    constexpr double d = 2.75;
    if (t < 1.0 / d) {
        return n * t * t;
    }
    if (t < 2.0 / d) {
        t -= 1.5 / d;
        return n * t * t + 0.75;
    }
    if (t < 2.5 / d) {
        t -= 2.25 / d;
        return n * t * t + 0.9375;
    }
    t -= 2.625 / d;
    return n * t * t + 0.984375;
}

std::int64_t entryFrames(const CaptionLayout &layout)
{
    switch (layout.style.animation) {
    case CaptionAnimation::None:
        return 0;
    case CaptionAnimation::Pop:
        return framesOf(kPopSeconds, layout.fps);
    case CaptionAnimation::Fade:
        return framesOf(kFadeSeconds, layout.fps);
    case CaptionAnimation::Bounce:
        return framesOf(kBounceSeconds, layout.fps);
    }
    return 0;
}

int groupAt(const CaptionLayout &layout, std::int64_t frame)
{
    for (size_t i = 0; i < layout.groups.size(); ++i) {
        if (frame >= layout.groups[i].start && frame < layout.groups[i].end) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

// The word of the group being said at `frame` (index in layout.words), -1 before its first word.
int activeIn(const CaptionLayout &layout, const CaptionLayout::Group &group, std::int64_t frame)
{
    int active = -1;
    for (int i = group.first; i < group.first + group.count && layout.words[static_cast<size_t>(i)].start <= frame; ++i) {
        active = i;
    }
    return active;
}

QTransform aroundCenter(const QRectF &box, double scale)
{
    const QPointF c = box.center();
    return QTransform::fromTranslate(c.x(), c.y()).scale(scale, scale).translate(-c.x(), -c.y());
}

} // namespace

std::shared_ptr<CaptionLayout> CaptionRenderer::layout(const SubtitleClipData &line, const CaptionStyle &style,
                                                       std::int64_t lengthFrames, QSize canvas, Rational frameRate)
{
    auto result = std::make_shared<CaptionLayout>();
    CaptionLayout &layout = *result;
    layout.canvas = canvas;
    layout.fps = frameRate.toDouble() > 0 ? frameRate.toDouble() : 30.0;
    layout.style = style;
    if (line.styleOverride) {
        layout.style.text = *line.styleOverride;
    }
    if (canvas.isEmpty() || lengthFrames <= 0) {
        return result;
    }
    fonts::loadBundled(); // "Inter" is the font shipped with vedit, in every process
    const RationalTime duration(lengthFrames, frameRate);
    const std::vector<TimedWord> words = captions::timedWords(line, duration);
    const std::vector<captions::WordGroup> groups = captions::groupsOf(words, style.maxWordsPerLine, duration);
    const auto frames = [&frameRate](const RationalTime &time) {
        return time.rescaled(frameRate, Rounding::NearestEven).value();
    };

    const TextStyle &text = layout.style.text;
    layout.pixelSize = std::max(1.0, numberOf(text.size, 0.055) * canvas.height());
    QFont font(text.fontFamily);
    font.setPixelSize(static_cast<int>(std::lround(layout.pixelSize)));
    font.setWeight(static_cast<QFont::Weight>(std::clamp(text.fontWeight, 100, 900)));
    font.setItalic(text.italic);
    const double spacing = numberOf(text.letterSpacing, 0.0);
    if (spacing != 0.0) {
        font.setLetterSpacing(QFont::AbsoluteSpacing, spacing * layout.pixelSize);
    }
    font.setHintingPreference(QFont::PreferNoHinting);
    const QFontMetricsF metrics(font);
    const double space = metrics.horizontalAdvance(QLatin1Char(' '));
    const double lineStep = metrics.height() * text.lineHeight;
    const double maxWidth = canvas.width() * kMaxLineWidth;
    const double centerY = canvas.height() * (0.5 + layout.style.position);

    layout.words.resize(words.size());
    for (const captions::WordGroup &source : groups) {
        CaptionLayout::Group group;
        group.first = source.first;
        group.count = source.count;
        group.start = frames(source.start);
        group.end = std::min(frames(source.end), lengthFrames);
        // Words into lines no wider than maxWidth.
        std::vector<std::vector<int>> lines(1);
        std::vector<double> widths(1, 0.0);
        std::vector<double> wordWidths(words.size(), 0.0);
        std::vector<QString> texts(words.size());
        for (int i = source.first; i < source.first + source.count; ++i) {
            const size_t w = static_cast<size_t>(i);
            texts[w] = layout.style.uppercase ? words[w].text.toUpper() : words[w].text;
            wordWidths[w] = metrics.horizontalAdvance(texts[w]);
            const double added = lines.back().empty() ? wordWidths[w] : space + wordWidths[w];
            if (!lines.back().empty() && widths.back() + added > maxWidth) {
                lines.emplace_back();
                widths.push_back(0.0);
                widths.back() = wordWidths[w];
            } else {
                widths.back() += added;
            }
            lines.back().push_back(i);
        }
        const double widest = *std::max_element(widths.begin(), widths.end());
        const double blockHeight = lineStep * static_cast<double>(lines.size() - 1) + metrics.height();
        const double top = centerY - blockHeight / 2.0;
        for (size_t l = 0; l < lines.size(); ++l) {
            double x = (canvas.width() - widest) / 2.0;
            if (text.align == TextAlign::Center) {
                x = (canvas.width() - widths[l]) / 2.0;
            } else if (text.align == TextAlign::Right) {
                x = (canvas.width() + widest) / 2.0 - widths[l];
            }
            const double lineTop = top + lineStep * static_cast<double>(l);
            const double baseline = lineTop + metrics.ascent();
            group.lines.push_back(QRectF(x, lineTop, widths[l], metrics.height()));
            for (const int i : lines[l]) {
                const size_t w = static_cast<size_t>(i);
                CaptionLayout::Word &word = layout.words[w];
                word.path.addText(QPointF(x, baseline), font, texts[w]);
                word.box = QRectF(x, lineTop, wordWidths[w], metrics.height());
                word.start = frames(words[w].start);
                word.end = frames(words[w].end);
                x += wordWidths[w] + space;
            }
        }
        for (const QRectF &rect : group.lines) {
            group.bounds = group.bounds.united(rect);
        }
        layout.groups.push_back(std::move(group));
    }
    return result;
}

std::int64_t CaptionRenderer::stateKey(const CaptionLayout &layout, std::int64_t frame)
{
    const int g = groupAt(layout, frame);
    if (g < 0) {
        return -1;
    }
    const CaptionLayout::Group &group = layout.groups[static_cast<size_t>(g)];
    const int active = activeIn(layout, group, frame);
    const std::int64_t entry = std::min(frame - group.start, entryFrames(layout));
    std::int64_t grow = 0;
    std::int64_t sung = 0;
    if (active >= 0) {
        const CaptionLayout::Word &word = layout.words[static_cast<size_t>(active)];
        if (layout.style.highlight == CaptionHighlight::Scale) {
            grow = std::min(frame - word.start, framesOf(kWordGrowSeconds, layout.fps));
        } else if (layout.style.highlight == CaptionHighlight::Karaoke) {
            sung = std::clamp<std::int64_t>(std::min(frame, word.end) - word.start, 0, 4095);
        }
    }
    // 20 bits of group, 16 of word, 8 + 8 of animation steps, 12 of karaoke fill.
    const auto bits = [](std::int64_t value, int count) {
        return static_cast<std::uint64_t>(std::clamp<std::int64_t>(value, 0, (std::int64_t(1) << count) - 1));
    };
    const std::uint64_t key = bits(g, 20) << 44 | bits(active + 1, 16) << 28 | bits(entry, 8) << 20 | bits(grow, 8) << 12
                              | bits(sung, 12);
    return static_cast<std::int64_t>(key & 0x7fffffffffffffffULL);
}

QRectF CaptionRenderer::bounds(const CaptionLayout &layout, std::int64_t frame)
{
    const int g = groupAt(layout, frame);
    return g < 0 ? QRectF() : layout.groups[static_cast<size_t>(g)].bounds;
}

QImage CaptionRenderer::render(const CaptionLayout &layout, std::int64_t frame, QSize size)
{
    QImage image(size, QImage::Format_RGBA8888_Premultiplied);
    image.fill(Qt::transparent);
    const int g = groupAt(layout, frame);
    if (g < 0 || size.isEmpty() || layout.canvas.isEmpty()) {
        return image.convertToFormat(QImage::Format_RGBA8888);
    }
    const CaptionLayout::Group &group = layout.groups[static_cast<size_t>(g)];
    const CaptionStyle &style = layout.style;
    const TextStyle &text = style.text;
    const double px = layout.pixelSize;

    // Canvas → image, then the entry animation of the group.
    QTransform view = QTransform::fromScale(size.width() / static_cast<double>(layout.canvas.width()),
                                            size.height() / static_cast<double>(layout.canvas.height()));
    const double entry = progress(frame - group.start, entryFrames(layout));
    double opacity = 1.0;
    QTransform motion;
    switch (style.animation) {
    case CaptionAnimation::None:
        break;
    case CaptionAnimation::Pop:
        motion = aroundCenter(group.bounds, 0.6 + 0.4 * easeOutBack(entry));
        opacity = std::min(1.0, entry * 3.0);
        break;
    case CaptionAnimation::Fade:
        opacity = entry;
        break;
    case CaptionAnimation::Bounce:
        motion = QTransform::fromTranslate(0.0, (1.0 - easeOutBounce(entry)) * 0.06 * layout.canvas.height());
        opacity = std::min(1.0, entry * 3.0);
        break;
    }
    if (opacity <= 0.0) {
        return image.convertToFormat(QImage::Format_RGBA8888);
    }
    const QTransform world = motion * view;

    const int active = activeIn(layout, group, frame);
    // The "size" highlight: the word being said grows and the other words of its line make room for it.
    double grownBy = 0.0;
    if (style.highlight == CaptionHighlight::Scale && active >= 0) {
        const CaptionLayout::Word &word = layout.words[static_cast<size_t>(active)];
        grownBy = (kWordScale - 1.0) * easeOutCubic(progress(frame - word.start, framesOf(kWordGrowSeconds, layout.fps)));
    }
    const auto wordTransform = [&](int i) {
        if (grownBy <= 0.0) {
            return QTransform();
        }
        const CaptionLayout::Word &said = layout.words[static_cast<size_t>(active)];
        const CaptionLayout::Word &word = layout.words[static_cast<size_t>(i)];
        if (i == active) {
            return aroundCenter(word.box, 1.0 + grownBy);
        }
        if (std::abs(word.box.top() - said.box.top()) > 0.5) {
            return QTransform(); // another line
        }
        const double room = said.box.width() * grownBy / 2.0;
        return QTransform::fromTranslate(i < active ? -room : room, 0.0);
    };
    std::vector<QPainterPath> paths;
    QPainterPath all;
    for (int i = group.first; i < group.first + group.count; ++i) {
        paths.push_back(wordTransform(i).map(layout.words[static_cast<size_t>(i)].path));
        all.addPath(paths.back());
    }
    const auto pathOf = [&](int i) -> const QPainterPath & { return paths[static_cast<size_t>(i - group.first)]; };

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setOpacity(opacity);
    painter.setTransform(world);

    // Coloured blocks behind the lines.
    if (text.background && text.background->color.a > 0) {
        const double pad = text.background->padding * px;
        const double radius = text.background->radius * px;
        painter.setPen(Qt::NoPen);
        painter.setBrush(toQColor(text.background->color));
        for (const QRectF &line : group.lines) {
            painter.drawRoundedRect(line.adjusted(-pad, -pad * 0.4, pad, pad * 0.4), radius, radius);
        }
    }
    // The box behind the word being said.
    if (style.highlight == CaptionHighlight::Box && active >= 0) {
        const CaptionLayout::Word &word = layout.words[static_cast<size_t>(active)];
        const QRectF box = word.box.adjusted(-kBoxPadX * px, -kBoxPadY * px, kBoxPadX * px, kBoxPadY * px);
        painter.setPen(Qt::NoPen);
        painter.setBrush(toQColor(style.highlightColor));
        painter.drawRoundedRect(box, kBoxRadius * px, kBoxRadius * px);
    }

    const double strokeWidth = text.stroke ? text.stroke->width * px : 0.0;
    const QColor strokeColor = text.stroke ? toQColor(colorOf(text.stroke->color, Color{0, 0, 0, 255})) : QColor();
    if (text.shadow && text.shadow->color.a > 0) {
        QImage shadow(size, QImage::Format_RGBA8888_Premultiplied);
        shadow.fill(Qt::transparent);
        {
            QPainter p(&shadow);
            p.setRenderHint(QPainter::Antialiasing);
            p.setTransform(QTransform::fromTranslate(text.shadow->offset.x * layout.canvas.height(),
                                                     text.shadow->offset.y * layout.canvas.height()) * world);
            const QColor color = toQColor(text.shadow->color);
            if (strokeWidth > 0.0) {
                p.strokePath(all, QPen(color, 2 * strokeWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            }
            p.fillPath(all, color);
        }
        const double scale = size.height() / static_cast<double>(layout.canvas.height());
        const int radius = static_cast<int>(std::lround(text.shadow->blur * px * scale / 3.0));
        if (radius > 0) {
            QImage straight = shadow.convertToFormat(QImage::Format_RGBA8888);
            fx::boxBlur(fx::ImageView{straight.bits(), straight.width(), straight.height(),
                                      static_cast<int>(straight.bytesPerLine())},
                        radius);
            shadow = straight;
        }
        painter.save();
        painter.resetTransform();
        painter.drawImage(0, 0, shadow);
        painter.restore();
    }
    if (strokeWidth > 0.0) {
        painter.strokePath(all, QPen(strokeColor, 2 * strokeWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    }

    const QColor normal = toQColor(colorOf(text.color, Color{255, 255, 255, 255}));
    const QColor highlight = toQColor(style.highlightColor);
    for (int i = group.first; i < group.first + group.count; ++i) {
        QColor color = normal;
        switch (style.highlight) {
        case CaptionHighlight::Color:
        case CaptionHighlight::Scale:
            color = i == active ? highlight : normal;
            break;
        case CaptionHighlight::Karaoke:
            color = i < active ? highlight : normal;
            break;
        case CaptionHighlight::None:
        case CaptionHighlight::Box:
            break;
        }
        painter.fillPath(pathOf(i), color);
        if (style.highlight == CaptionHighlight::Karaoke && i == active) {
            // The word fills from the left while it is said.
            const CaptionLayout::Word &word = layout.words[static_cast<size_t>(i)];
            const double sung = progress(frame - word.start, word.end - word.start);
            painter.save();
            painter.setClipRect(QRectF(word.box.left(), word.box.top() - px, word.box.width() * sung,
                                       word.box.height() + 2 * px));
            painter.fillPath(pathOf(i), highlight);
            painter.restore();
        }
    }
    painter.end();
    return image.convertToFormat(QImage::Format_RGBA8888);
}

} // namespace vedit::engine
