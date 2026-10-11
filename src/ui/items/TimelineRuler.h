// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "items/ImageTiles.h"

#include <QColor>
#include <QFont>
#include <QHash>
#include <QImage>
#include <QQuickItem>
#include <QtQml/qqmlregistration.h>

#include <vector>

namespace velacut::ui {

// The ruler of the timeline as one item (not an item and a label per tick): ticks and real-time labels painted in
// chunks of the timeline 512 pixels wide, each once for a zoom level and kept as a texture; scrolling moves the chunks
// (image nodes, every backend). Labels on whole-second steps fall on real seconds at any frame rate (29.97 included:
// core/time/Timecode.h); the step is the smallest whose labels do not overlap. Sizes and colours come from the Theme.
class TimelineRuler : public QQuickItem
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(double zoom READ zoom WRITE setZoom NOTIFY changed FINAL)           // pixels per frame
    Q_PROPERTY(double contentX READ contentX WRITE setContentX NOTIFY changed FINAL) // timeline pixel at the left edge
    Q_PROPERTY(int frameRateNum READ frameRateNum WRITE setFrameRateNum NOTIFY changed FINAL)
    Q_PROPERTY(int frameRateDen READ frameRateDen WRITE setFrameRateDen NOTIFY changed FINAL)
    Q_PROPERTY(double labelSpacing READ labelSpacing WRITE setLabelSpacing NOTIFY changed FINAL)
    Q_PROPERTY(double majorTick READ majorTick WRITE setMajorTick NOTIFY changed FINAL)
    Q_PROPERTY(double minorTick READ minorTick WRITE setMinorTick NOTIFY changed FINAL)
    Q_PROPERTY(double hairline READ hairline WRITE setHairline NOTIFY changed FINAL)
    Q_PROPERTY(double labelInset READ labelInset WRITE setLabelInset NOTIFY changed FINAL)
    Q_PROPERTY(QColor majorColor READ majorColor WRITE setMajorColor NOTIFY changed FINAL)
    Q_PROPERTY(QColor minorColor READ minorColor WRITE setMinorColor NOTIFY changed FINAL)
    Q_PROPERTY(QColor labelColor READ labelColor WRITE setLabelColor NOTIFY changed FINAL)
    Q_PROPERTY(QFont font READ font WRITE setFont NOTIFY changed FINAL)
    // The step between labelled ticks, in frames (tests).
    Q_PROPERTY(int step READ step NOTIFY changed FINAL)

public:
    explicit TimelineRuler(QQuickItem *parent = nullptr);

    double zoom() const { return m_zoom; }
    void setZoom(double zoom);
    double contentX() const { return m_contentX; }
    void setContentX(double x);
    int frameRateNum() const { return m_num; }
    void setFrameRateNum(int num);
    int frameRateDen() const { return m_den; }
    void setFrameRateDen(int den);
    double labelSpacing() const { return m_labelSpacing; }
    void setLabelSpacing(double spacing);
    double majorTick() const { return m_majorTick; }
    void setMajorTick(double size);
    double minorTick() const { return m_minorTick; }
    void setMinorTick(double size);
    double hairline() const { return m_hairline; }
    void setHairline(double size);
    double labelInset() const { return m_labelInset; }
    void setLabelInset(double inset);
    QColor majorColor() const { return m_majorColor; }
    void setMajorColor(const QColor &color);
    QColor minorColor() const { return m_minorColor; }
    void setMinorColor(const QColor &color);
    QColor labelColor() const { return m_labelColor; }
    void setLabelColor(const QColor &color);
    QFont font() const { return m_font; }
    void setFont(const QFont &font);
    int step() const;

    // The labelled ticks between two frames, with their labels (tests, and the painting).
    struct Tick
    {
        std::int64_t frame = 0;
        QString label;
    };
    std::vector<Tick> ticks(std::int64_t firstFrame, std::int64_t lastFrame) const;

signals:
    void changed();

protected:
    QSGNode *updatePaintNode(QSGNode *old, UpdatePaintNodeData *data) override;
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;

private:
    void invalidate();
    void layoutTiles();
    QImage paintChunk(int chunk) const;
    int minorDivisions() const;

    double m_zoom = 3.0;
    double m_contentX = 0.0;
    int m_num = 30;
    int m_den = 1;
    double m_labelSpacing = 96.0;
    double m_majorTick = 10.0;
    double m_minorTick = 4.0;
    double m_hairline = 1.0;
    double m_labelInset = 4.0;
    QColor m_majorColor;
    QColor m_minorColor;
    QColor m_labelColor;
    QFont m_font;
    QHash<int, QImage> m_chunks;
    std::vector<ImageTile> m_tiles;
    QHash<qint64, QImage> m_images;
};

} // namespace velacut::ui
