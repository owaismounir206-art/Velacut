// SPDX-License-Identifier: GPL-3.0-or-later
#include "TextRenderer.h"

#include "fx/Color.h"

#include <QFont>
#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>
#include <QTextLayout>

#include <cmath>

namespace vedit::engine {

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

struct Layout
{
    QFont font;
    double pixelSize = 0;
    std::vector<QString> lines;
    std::vector<double> widths;
    double lineStep = 0;
    double ascent = 0;
    QRectF textRect; // union of the lines, centred on the canvas
    QRectF boxRect;  // with the background padding
};

Layout layoutText(const TextClipData &text, QSize canvas)
{
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
    font.setHintingPreference(QFont::PreferNoHinting); // the same shapes at every size
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
    layout.boxRect = layout.textRect;
    if (style.background) {
        const double pad = style.background->padding * layout.pixelSize;
        layout.boxRect.adjust(-pad, -pad * 0.6, pad, pad * 0.6);
    }
    return layout;
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

} // namespace

QRectF TextRenderer::bounds(const TextClipData &text, QSize canvas)
{
    return layoutText(text, canvas).boxRect;
}

QImage TextRenderer::render(const TextClipData &text, QSize canvas)
{
    QImage image(canvas, QImage::Format_RGBA8888_Premultiplied);
    image.fill(Qt::transparent);
    if (canvas.isEmpty()) {
        return image.convertToFormat(QImage::Format_RGBA8888);
    }
    const TextStyle &style = text.style;
    const Layout layout = layoutText(text, canvas);
    const QPainterPath path = textPath(text, layout);
    QPainter painter(&image);
    painter.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);

    if (style.background) {
        const double radius = style.background->radius * layout.pixelSize;
        painter.setPen(Qt::NoPen);
        painter.setBrush(toQColor(style.background->color));
        painter.drawRoundedRect(layout.boxRect, radius, radius);
    }
    const double strokeWidth = style.stroke ? style.stroke->width * layout.pixelSize : 0.0;
    if (style.shadow) {
        // The silhouette (with its outline), blurred, under the text.
        QImage shadow(canvas, QImage::Format_RGBA8888_Premultiplied);
        shadow.fill(Qt::transparent);
        {
            QPainter p(&shadow);
            p.setRenderHint(QPainter::Antialiasing);
            p.translate(style.shadow->offset.x * canvas.height(), style.shadow->offset.y * canvas.height());
            if (strokeWidth > 0) {
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
    if (strokeWidth > 0) {
        // Outer outline: stroked at twice the width, the fill covers the inner half.
        painter.strokePath(path, QPen(toQColor(colorOf(style.stroke->color, Color{0, 0, 0, 255})), 2 * strokeWidth,
                                      Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    }
    painter.fillPath(path, toQColor(colorOf(style.color, Color{255, 255, 255, 255})));
    painter.end();
    return image.convertToFormat(QImage::Format_RGBA8888);
}

} // namespace vedit::engine
