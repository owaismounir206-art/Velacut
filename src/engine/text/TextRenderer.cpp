// SPDX-License-Identifier: GPL-3.0-or-later
#include "TextRenderer.h"

#include "common/fonts/BundledFonts.h"
#include "fx/Color.h"

#include <QColor>
#include <QFont>
#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>
#include <QTextLayout>

#include <algorithm>
#include <cmath>
#include <vector>

namespace velacut::engine {

namespace {

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

uint32_t pseudoHash(uint32_t a, uint32_t b)
{
    uint32_t h = a * 374761393u + b * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

struct Layout
{
    QFont font;
    double pixelSize = 0;
    std::vector<QString> lines;
    std::vector<double> widths;
    double lineStep = 0;
    double ascent = 0;
    QRectF textRect;    // union of lines, centred on canvas
    QRectF baseBoxRect; // text box with padding
    QRectF boxRect;     // total bounds including tail or accent bar
};

Layout layoutText(const TextClipData &text, QSize canvas)
{
    fonts::loadBundled(); // "Inter" is the font shipped with velacut, in every process
    Layout layout;
    const TextStyle &style = text.style;
    layout.pixelSize = std::max(1.0, numberOf(style.size, 0.06) * canvas.height());
    QFont font(style.fontFamily);
    font.setPixelSize(static_cast<int>(std::lround(layout.pixelSize)));
    font.setWeight(static_cast<QFont::Weight>(std::clamp(style.fontWeight, 100, 900)));
    font.setItalic(style.italic);
    const double spacing = numberOf(style.letterSpacing, 0.0);
    if (spacing != 0.0) {
        font.setLetterSpacing(QFont::AbsoluteSpacing, spacing * layout.pixelSize);
    }
    font.setHintingPreference(QFont::PreferNoHinting); // same shapes at every size
    layout.font = font;
    const QFontMetricsF metrics(font);
    layout.ascent = metrics.ascent();
    layout.lineStep = metrics.height() * style.lineHeight;

    const QString content = text.text.isEmpty() ? QStringLiteral(" ") : text.text;
    const double wrapWidth = text.boxWidth ? *text.boxWidth * canvas.width() : 0.0;
    for (const QString &paragraph : content.split(QLatin1Char('\n'))) {
        if (wrapWidth <= 0.0 || paragraph.isEmpty()) {
            layout.lines.push_back(paragraph);
            continue;
        }
        QTextLayout wrapper(paragraph, font);
        QTextOption option;
        option.setWrapMode(QTextOption::WordWrap);
        wrapper.setTextOption(option);
        wrapper.beginLayout();
        for (QTextLine line = wrapper.createLine(); line.isValid(); line = wrapper.createLine()) {
            line.setLineWidth(wrapWidth);
            layout.lines.push_back(paragraph.mid(line.textStart(), line.textLength()).trimmed());
        }
        wrapper.endLayout();
    }
    double maxWidth = 0;
    for (const QString &line : layout.lines) {
        const double width = metrics.horizontalAdvance(line);
        layout.widths.push_back(width);
        maxWidth = std::max(maxWidth, width);
    }
    const double height = layout.lineStep * (layout.lines.size() - 1) + metrics.height();
    layout.textRect = QRectF((canvas.width() - maxWidth) / 2.0, (canvas.height() - height) / 2.0, maxWidth, height);
    layout.baseBoxRect = layout.textRect;
    layout.boxRect = layout.textRect;

    if (style.background) {
        const double pad = style.background->padding * layout.pixelSize;
        layout.baseBoxRect.adjust(-pad, -pad * 0.6, pad, pad * 0.6);
        layout.boxRect = layout.baseBoxRect;

        const double tailPx = style.background->tailSize * layout.pixelSize;
        if (style.background->shape == BubbleShape::LowerThirdBar) {
            const double barW = std::max(6.0, layout.pixelSize * 0.15);
            layout.boxRect.adjust(-barW - pad * 0.5, 0, pad * 0.5, 0);
        } else if (style.background->shape == BubbleShape::Callout) {
            layout.boxRect.adjust(-tailPx * 0.7, 0, tailPx * 0.7, tailPx * 0.7);
        } else if (style.background->tail != BubbleTail::None && tailPx > 0.0) {
            switch (style.background->tail) {
            case BubbleTail::BottomLeft:
            case BubbleTail::BottomCenter:
            case BubbleTail::BottomRight:
                layout.boxRect.adjust(0, 0, 0, tailPx);
                break;
            case BubbleTail::TopLeft:
            case BubbleTail::TopRight:
                layout.boxRect.adjust(0, -tailPx, 0, 0);
                break;
            case BubbleTail::Left:
                layout.boxRect.adjust(-tailPx, 0, 0, 0);
                break;
            case BubbleTail::Right:
                layout.boxRect.adjust(0, 0, tailPx, 0);
                break;
            default:
                break;
            }
        }
    }
    return layout;
}

void drawBackground(QPainter &painter, const TextStyle &style, const Layout &layout, double alpha)
{
    if (!style.background || alpha <= 0.001) {
        return;
    }
    painter.save();
    painter.setOpacity(alpha);

    const TextBackground &bg = *style.background;
    const double radius = bg.radius * layout.pixelSize;
    const QRectF &R = layout.baseBoxRect;
    const double tailPx = bg.tailSize * layout.pixelSize;

    const QColor fillColor = toQColor(bg.color);
    QPen borderPen = Qt::NoPen;
    if (bg.borderWidth > 0.0 && bg.borderColor.a > 0) {
        borderPen = QPen(toQColor(bg.borderColor), std::max(1.0, bg.borderWidth * layout.pixelSize),
                         Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    }
    painter.setPen(borderPen);
    painter.setBrush(fillColor);

    switch (bg.shape) {
    case BubbleShape::Rectangle:
        painter.drawRoundedRect(R, radius, radius);
        break;
    case BubbleShape::Badge: {
        const double pillR = R.height() / 2.0;
        painter.drawRoundedRect(R, pillR, pillR);
        break;
    }
    case BubbleShape::SpeechRound: {
        QPainterPath path;
        path.addRoundedRect(R, radius, radius);
        const BubbleTail tail = (bg.tail == BubbleTail::None) ? BubbleTail::BottomLeft : bg.tail;
        if (tailPx > 0.0) {
            QPainterPath tailPath;
            const double rClamped = std::min(radius, R.width() * 0.25);
            switch (tail) {
            case BubbleTail::BottomLeft: {
                const double bx = R.left() + rClamped + 10.0;
                tailPath.moveTo(bx, R.bottom());
                tailPath.lineTo(R.left() + rClamped - 5.0, R.bottom() + tailPx);
                tailPath.lineTo(bx + 20.0, R.bottom());
                tailPath.closeSubpath();
                break;
            }
            case BubbleTail::BottomCenter: {
                const double cx = R.center().x();
                tailPath.moveTo(cx - 10.0, R.bottom());
                tailPath.lineTo(cx, R.bottom() + tailPx);
                tailPath.lineTo(cx + 10.0, R.bottom());
                tailPath.closeSubpath();
                break;
            }
            case BubbleTail::BottomRight: {
                const double bx = R.right() - rClamped - 30.0;
                tailPath.moveTo(bx, R.bottom());
                tailPath.lineTo(R.right() - rClamped + 5.0, R.bottom() + tailPx);
                tailPath.lineTo(bx + 20.0, R.bottom());
                tailPath.closeSubpath();
                break;
            }
            case BubbleTail::TopLeft: {
                const double bx = R.left() + rClamped + 10.0;
                tailPath.moveTo(bx, R.top());
                tailPath.lineTo(R.left() + rClamped - 5.0, R.top() - tailPx);
                tailPath.lineTo(bx + 20.0, R.top());
                tailPath.closeSubpath();
                break;
            }
            case BubbleTail::TopRight: {
                const double bx = R.right() - rClamped - 30.0;
                tailPath.moveTo(bx, R.top());
                tailPath.lineTo(R.right() - rClamped + 5.0, R.top() - tailPx);
                tailPath.lineTo(bx + 20.0, R.top());
                tailPath.closeSubpath();
                break;
            }
            case BubbleTail::Left: {
                const double cy = R.center().y();
                tailPath.moveTo(R.left(), cy - 10.0);
                tailPath.lineTo(R.left() - tailPx, cy);
                tailPath.lineTo(R.left(), cy + 10.0);
                tailPath.closeSubpath();
                break;
            }
            case BubbleTail::Right: {
                const double cy = R.center().y();
                tailPath.moveTo(R.right(), cy - 10.0);
                tailPath.lineTo(R.right() + tailPx, cy);
                tailPath.lineTo(R.right(), cy + 10.0);
                tailPath.closeSubpath();
                break;
            }
            default:
                break;
            }
            path = path.united(tailPath);
        }
        painter.drawPath(path);
        break;
    }
    case BubbleShape::SpeechSquare: {
        QPainterPath path;
        path.addRoundedRect(R, std::min(4.0, radius), std::min(4.0, radius));
        if (tailPx > 0.0) {
            QPainterPath tailPath;
            const double bx = R.left() + 20.0;
            tailPath.moveTo(bx, R.bottom());
            tailPath.lineTo(bx - 15.0, R.bottom() + tailPx);
            tailPath.lineTo(bx + 25.0, R.bottom());
            tailPath.closeSubpath();
            path = path.united(tailPath);
        }
        painter.drawPath(path);
        break;
    }
    case BubbleShape::ThoughtCloud: {
        QPainterPath cloudPath;
        cloudPath.addRoundedRect(R, radius, radius);
        if (tailPx > 0.0) {
            const double cx = R.left() + 20.0;
            const double cy = R.bottom();
            cloudPath.addEllipse(QPointF(cx - 5.0, cy + tailPx * 0.35), tailPx * 0.18, tailPx * 0.18);
            cloudPath.addEllipse(QPointF(cx - 15.0, cy + tailPx * 0.7), tailPx * 0.12, tailPx * 0.12);
            cloudPath.addEllipse(QPointF(cx - 25.0, cy + tailPx * 1.0), tailPx * 0.07, tailPx * 0.07);
        }
        painter.drawPath(cloudPath);
        break;
    }
    case BubbleShape::ComicShout: {
        const QPointF center = R.center();
        const double rx0 = R.width() / 2.0;
        const double ry0 = R.height() / 2.0;
        QPolygonF poly;
        const int numPoints = 24;
        for (int k = 0; k < numPoints; ++k) {
            const double angle = k * (2.0 * M_PI / numPoints);
            const double rScale = (k % 2 == 0) ? 1.35 : 0.95;
            const double px = center.x() + std::cos(angle) * (rx0 * rScale);
            const double py = center.y() + std::sin(angle) * (ry0 * rScale);
            poly.append(QPointF(px, py));
        }
        painter.drawPolygon(poly);
        break;
    }
    case BubbleShape::Callout: {
        painter.drawRoundedRect(R, radius, radius);
        if (tailPx > 0.0) {
            QPen leaderPen(toQColor(bg.accentColor.a > 0 ? bg.accentColor : bg.borderColor),
                           std::max(2.0, layout.pixelSize * 0.04));
            painter.setPen(leaderPen);
            painter.setBrush(Qt::NoBrush);
            const QPointF startPt(R.left(), R.bottom());
            const QPointF midPt(R.left() - tailPx * 0.6, R.bottom() + tailPx * 0.6);
            const QPointF endPt(R.left() - tailPx * 0.9, midPt.y());
            painter.drawLine(startPt, midPt);
            painter.drawLine(midPt, endPt);
            painter.setBrush(leaderPen.color());
            painter.drawEllipse(endPt, 4.0, 4.0);
        }
        break;
    }
    case BubbleShape::LowerThirdBar: {
        painter.drawRoundedRect(R, radius, radius);
        const double accentW = std::max(6.0, layout.pixelSize * 0.12);
        const QRectF accentBar(R.left(), R.top(), accentW, R.height());
        painter.setPen(Qt::NoPen);
        painter.setBrush(toQColor(bg.accentColor));
        QPainterPath barPath;
        barPath.addRoundedRect(accentBar, radius, radius);
        painter.drawPath(barPath);
        break;
    }
    case BubbleShape::LowerThirdTwoTone: {
        painter.drawRoundedRect(R, radius, radius);
        const double cutW = std::min(R.width() * 0.25, layout.pixelSize * 1.2);
        QPolygonF chevron;
        chevron << QPointF(R.left(), R.top())
                << QPointF(R.left() + cutW, R.top())
                << QPointF(R.left() + cutW - 15.0, R.bottom())
                << QPointF(R.left(), R.bottom());
        painter.setPen(Qt::NoPen);
        painter.setBrush(toQColor(bg.accentColor));
        painter.drawPolygon(chevron);
        break;
    }
    }
    painter.restore();
}

QPainterPath textPath(const TextClipData &text, const Layout &layout)
{
    QPainterPath path;
    const QFontMetricsF metrics(layout.font);
    for (size_t i = 0; i < layout.lines.size(); ++i) {
        double x = layout.textRect.left();
        switch (text.style.align) {
        case TextAlign::Left:
            break;
        case TextAlign::Center:
            x += (layout.textRect.width() - layout.widths[i]) / 2.0;
            break;
        case TextAlign::Right:
            x += layout.textRect.width() - layout.widths[i];
            break;
        }
        const double baseline = layout.textRect.top() + layout.ascent + layout.lineStep * static_cast<double>(i);
        path.addText(QPointF(x, baseline), layout.font, layout.lines[i]);
        if (text.style.underline && layout.widths[i] > 0) {
            const double thickness = std::max(1.0, metrics.lineWidth());
            path.addRect(QRectF(x, baseline + metrics.underlinePos(), layout.widths[i], thickness));
        }
    }
    return path;
}

struct GlyphItem
{
    QString text;
    QPointF baseline;
    double width = 0.0;
    bool isWhitespace = false;
};

std::vector<GlyphItem> buildItems(const TextClipData &text, const Layout &layout)
{
    std::vector<GlyphItem> items;
    const QFontMetricsF metrics(layout.font);
    const TextAnimationScope scope = text.animation ? text.animation->scope : TextAnimationScope::Character;

    for (size_t l = 0; l < layout.lines.size(); ++l) {
        const QString &line = layout.lines[l];
        const double baselineY = layout.textRect.top() + layout.ascent + layout.lineStep * static_cast<double>(l);
        double startX = layout.textRect.left();
        switch (text.style.align) {
        case TextAlign::Left:
            break;
        case TextAlign::Center:
            startX += (layout.textRect.width() - layout.widths[l]) / 2.0;
            break;
        case TextAlign::Right:
            startX += layout.textRect.width() - layout.widths[l];
            break;
        }

        if (scope == TextAnimationScope::All) {
            items.push_back({line, QPointF(startX, baselineY), layout.widths[l], line.trimmed().isEmpty()});
        } else if (scope == TextAnimationScope::Line) {
            items.push_back({line, QPointF(startX, baselineY), layout.widths[l], line.trimmed().isEmpty()});
        } else if (scope == TextAnimationScope::Word) {
            qsizetype pos = 0;
            const qsizetype len = line.size();
            while (pos < len) {
                const qsizetype next = line.indexOf(QLatin1Char(' '), pos);
                const qsizetype end = (next == -1) ? len : next;
                if (end > pos) {
                    const QString word = line.mid(pos, end - pos);
                    const double curX = startX + metrics.horizontalAdvance(line.left(pos));
                    const double w = metrics.horizontalAdvance(word);
                    items.push_back({word, QPointF(curX, baselineY), w, false});
                }
                if (next != -1) {
                    const QString space = QStringLiteral(" ");
                    const double curX = startX + metrics.horizontalAdvance(line.left(next));
                    items.push_back({space, QPointF(curX, baselineY), metrics.horizontalAdvance(space), true});
                    pos = next + 1;
                } else {
                    break;
                }
            }
        } else {
            // Character scope
            for (qsizetype c = 0; c < line.size();) {
                const qsizetype clen = line.at(c).isHighSurrogate() ? 2 : 1;
                const QString ch = line.mid(c, clen);
                const double curX = startX + (c == 0 ? 0.0 : metrics.horizontalAdvance(line.left(c)));
                const double cw = metrics.horizontalAdvance(ch);
                const bool ws = ch.trimmed().isEmpty();
                items.push_back({ch, QPointF(curX, baselineY), cw, ws});
                c += clen;
            }
        }
    }
    return items;
}

} // namespace

QRectF TextRenderer::bounds(const TextClipData &text, QSize canvas)
{
    return layoutText(text, canvas).boxRect;
}

QImage TextRenderer::render(const TextClipData &text, QSize canvas)
{
    const double animDur = (text.animation && text.animation->duration.toSecondsDouble() > 0.05)
                               ? text.animation->duration.toSecondsDouble()
                               : 1.5;
    return render(text, canvas, animDur, animDur);
}

QImage TextRenderer::render(const TextClipData &text, QSize canvas, double progress)
{
    const double animDur = (text.animation && text.animation->duration.toSecondsDouble() > 0.05)
                               ? text.animation->duration.toSecondsDouble()
                               : 1.5;
    const double t = std::clamp(progress, 0.0, 1.0) * animDur;
    return render(text, canvas, t, animDur);
}

QImage TextRenderer::render(const TextClipData &text, QSize canvas, double timeSeconds, double durationSeconds)
{
    QImage image(canvas, QImage::Format_RGBA8888_Premultiplied);
    image.fill(Qt::transparent);
    if (canvas.isEmpty()) {
        return image.convertToFormat(QImage::Format_RGBA8888);
    }

    const TextStyle &style = text.style;
    const Layout layout = layoutText(text, canvas);
    const double t = std::max(0.0, timeSeconds);
    const double animDur = (text.animation && text.animation->duration.toSecondsDouble() > 0.05)
                               ? text.animation->duration.toSecondsDouble()
                               : (durationSeconds > 0.05 ? durationSeconds : 1.5);

    QPainter painter(&image);
    painter.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);

    // Background animation: smoothly fade in
    const double bgAlpha = (!text.animation || text.animation->type == TextAnimationType::None || t >= animDur)
                               ? 1.0
                               : std::clamp(t / std::min(0.3, animDur * 0.4), 0.0, 1.0);
    drawBackground(painter, style, layout, bgAlpha);

    const double strokeWidth = style.stroke ? style.stroke->width * layout.pixelSize : 0.0;
    const QColor textCol = toQColor(colorOf(style.color, Color{255, 255, 255, 255}));
    const QColor strokeCol = style.stroke ? toQColor(colorOf(style.stroke->color, Color{0, 0, 0, 255})) : QColor();
    const QFontMetricsF metrics(layout.font);

    // Fast path: static text with no animation
    if (!text.animation || text.animation->type == TextAnimationType::None) {
        const QPainterPath path = textPath(text, layout);
        if (style.shadow) {
            QImage shadow(canvas, QImage::Format_RGBA8888_Premultiplied);
            shadow.fill(Qt::transparent);
            {
                QPainter p(&shadow);
                p.setRenderHint(QPainter::Antialiasing);
                p.translate(style.shadow->offset.x * canvas.height(), style.shadow->offset.y * canvas.height());
                if (strokeWidth > 0.0) {
                    p.strokePath(path, QPen(toQColor(style.shadow->color), 2 * strokeWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                }
                p.fillPath(path, toQColor(style.shadow->color));
            }
            const int radius = static_cast<int>(std::lround(style.shadow->blur * layout.pixelSize / 3.0));
            if (radius > 0) {
                QImage straight = shadow.convertToFormat(QImage::Format_RGBA8888);
                fx::boxBlur(fx::ImageView{straight.bits(), straight.width(), straight.height(), static_cast<int>(straight.bytesPerLine())},
                            radius);
                shadow = straight;
            }
            painter.drawImage(0, 0, shadow);
        }
        if (strokeWidth > 0.0) {
            painter.strokePath(path, QPen(strokeCol, 2 * strokeWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        }
        painter.fillPath(path, textCol);
        painter.end();
        return image.convertToFormat(QImage::Format_RGBA8888);
    }

    // Animated text path
    const std::vector<GlyphItem> items = buildItems(text, layout);
    const int N = static_cast<int>(items.size());
    if (N == 0) {
        painter.end();
        return image.convertToFormat(QImage::Format_RGBA8888);
    }

    const TextAnimationType animType = text.animation->type;
    const bool isTypewriter = (animType == TextAnimationType::Typewriter);

    int visibleCount = N;
    if (isTypewriter) {
        if (t < animDur) {
            visibleCount = std::clamp(static_cast<int>(std::floor((t / animDur) * (N + 1))), 0, N);
        }
    }

    const double tau = std::max(0.12, animDur * 0.35);
    const double availTime = std::max(0.01, animDur - tau);

    for (int i = 0; i < N; ++i) {
        const GlyphItem &item = items[static_cast<size_t>(i)];
        if (item.isWhitespace) {
            continue;
        }

        double alpha = 1.0;
        double dx = 0.0;
        double dy = 0.0;
        double sx = 1.0;
        double sy = 1.0;
        QString textToDraw = item.text;

        if (isTypewriter) {
            if (i >= visibleCount) {
                continue; // Not yet visible
            }
        } else if (animType == TextAnimationType::Wave) {
            const double phase = 2.0 * M_PI * (t / 1.5 - static_cast<double>(i) / (N + 1));
            dy = std::sin(phase) * (layout.pixelSize * 0.22);
        } else {
            // Entrance animations
            if (t < animDur) {
                const double t0 = (static_cast<double>(i) / std::max(1, N - 1)) * availTime;
                const double u = std::clamp((t - t0) / tau, 0.0, 1.0);
                const double p = text.animation->easing.apply(u);

                switch (animType) {
                case TextAnimationType::FadeIn:
                    alpha = p;
                    break;
                case TextAnimationType::SlideUp:
                    alpha = p;
                    dy = (1.0 - p) * (layout.pixelSize * 0.7);
                    break;
                case TextAnimationType::SlideDown:
                    alpha = p;
                    dy = -(1.0 - p) * (layout.pixelSize * 0.7);
                    break;
                case TextAnimationType::Bounce: {
                    const Easing bounceEase = Easing::preset(Easing::Preset::EaseOut);
                    const double b = bounceEase.apply(u);
                    alpha = std::min(1.0, u * 2.5);
                    dy = -(1.0 - b) * (layout.pixelSize * 1.0);
                    break;
                }
                case TextAnimationType::PopIn: {
                    alpha = std::min(1.0, u * 3.0);
                    const double overshoot = 1.0 + 0.4 * std::sin(u * M_PI);
                    sx = std::max(0.0, p * overshoot);
                    sy = std::max(0.0, p * overshoot);
                    break;
                }
                case TextAnimationType::Glitch: {
                    if (u < 1.0) {
                        alpha = 0.8 + 0.2 * std::sin(30.0 * t + i);
                        const uint32_t h = pseudoHash(static_cast<uint32_t>(i), static_cast<uint32_t>(std::floor(25.0 * t)));
                        dx = ((h % 5) - 2) * (layout.pixelSize * 0.02);
                        dy = (((h >> 3) % 5) - 2) * (layout.pixelSize * 0.02);
                        if (u < 0.6) {
                            static const char cypher[] = "!@#$%^&*0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
                            textToDraw = QLatin1Char(cypher[h % (sizeof(cypher) - 1)]);
                        }
                    }
                    break;
                }
                case TextAnimationType::Blur:
                    alpha = p;
                    sy = 1.0 + (1.0 - p) * 0.7;
                    sx = 1.0 + (1.0 - p) * 0.3;
                    break;
                default:
                    break;
                }
            }
        }

        if (alpha <= 0.001) {
            continue;
        }

        painter.save();
        painter.setOpacity(alpha);

        QPainterPath itemPath;
        itemPath.addText(QPointF(0, 0), layout.font, textToDraw);

        const double posX = item.baseline.x() + dx;
        const double posY = item.baseline.y() + dy;

        painter.translate(posX, posY);
        if (sx != 1.0 || sy != 1.0) {
            const double cx = item.width / 2.0;
            const double cy = -layout.ascent / 2.0;
            painter.translate(cx, cy);
            painter.scale(sx, sy);
            painter.translate(-cx, -cy);
        }

        if (style.shadow) {
            painter.save();
            painter.translate(style.shadow->offset.x * canvas.height(), style.shadow->offset.y * canvas.height());
            if (strokeWidth > 0.0) {
                painter.strokePath(itemPath, QPen(toQColor(style.shadow->color), 2 * strokeWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            }
            painter.fillPath(itemPath, toQColor(style.shadow->color));
            painter.restore();
        }

        if (strokeWidth > 0.0) {
            painter.strokePath(itemPath, QPen(strokeCol, 2 * strokeWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        }
        painter.fillPath(itemPath, textCol);
        if (style.underline && item.width > 0) {
            const double thickness = std::max(1.0, metrics.lineWidth());
            painter.fillRect(QRectF(0, metrics.underlinePos(), item.width, thickness), textCol);
        }
        painter.restore();
    }

    // Typewriter blinking cursor
    if (isTypewriter && text.animation->cursor && visibleCount <= N) {
        const bool cursorOn = (static_cast<int>(std::floor(t * 5.0)) % 2) == 0;
        if (cursorOn) {
            QPointF cursorPt;
            if (visibleCount > 0 && visibleCount - 1 < N) {
                const GlyphItem &last = items[static_cast<size_t>(visibleCount - 1)];
                cursorPt = QPointF(last.baseline.x() + last.width + 2.0, last.baseline.y());
            } else if (!items.empty()) {
                cursorPt = items[0].baseline;
            }
            painter.save();
            painter.setPen(Qt::NoPen);
            painter.setBrush(textCol);
            const double cursorW = std::max(2.0, layout.pixelSize * 0.06);
            const double cursorH = layout.ascent;
            painter.drawRect(QRectF(cursorPt.x(), cursorPt.y() - cursorH, cursorW, cursorH));
            painter.restore();
        }
    }

    painter.end();
    return image.convertToFormat(QImage::Format_RGBA8888);
}

} // namespace velacut::engine
