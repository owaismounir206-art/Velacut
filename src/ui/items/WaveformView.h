// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QColor>
#include <QPointer>
#include <QHash>
#include <QImage>
#include <QQuickItem>
#include <QtQml/qqmlregistration.h>

#include <memory>
#include <vector>

#include "items/ImageTiles.h"

namespace velacut::engine {
struct Waveform;
}

namespace velacut::ui {

class EditorController;

// The waveform of a clip's audio (min/max peaks of the cached waveform, D-16), for the source range
// sourceIn…sourceIn+sourceDuration (project frames). Painted on the CPU in chunks of the clip 512 pixels wide, each
// once for a zoom level and kept as a texture: scrolling moves the chunks (image nodes, every backend), and only the
// chunks coming into view are painted.
// A clip minutes long is tens of thousands of pixels wide: the item then covers only the part in view, `offset`
// pixels from the clip's start, and `fullWidth` is the width of the whole clip (0: the item is the whole clip).
class WaveformView : public QQuickItem
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(velacut::ui::EditorController *editor READ editor WRITE setEditor NOTIFY editorChanged FINAL)
    Q_PROPERTY(QString mediaId READ mediaId WRITE setMediaId NOTIFY mediaIdChanged FINAL)
    Q_PROPERTY(int sourceIn READ sourceIn WRITE setSourceIn NOTIFY sourceChanged FINAL)
    Q_PROPERTY(int sourceDuration READ sourceDuration WRITE setSourceDuration NOTIFY sourceChanged FINAL)
    Q_PROPERTY(QColor color READ color WRITE setColor NOTIFY colorChanged FINAL)
    Q_PROPERTY(qreal fullWidth READ fullWidth WRITE setFullWidth NOTIFY spanChanged FINAL)
    Q_PROPERTY(qreal offset READ offset WRITE setOffset NOTIFY spanChanged FINAL)

public:
    explicit WaveformView(QQuickItem *parent = nullptr);
    ~WaveformView() override;

    EditorController *editor() const { return m_editor; }
    void setEditor(EditorController *editor);
    QString mediaId() const { return m_mediaId; }
    void setMediaId(const QString &mediaId);
    int sourceIn() const { return m_sourceIn; }
    void setSourceIn(int frames);
    int sourceDuration() const { return m_sourceDuration; }
    void setSourceDuration(int frames);
    QColor color() const { return m_color; }
    void setColor(const QColor &color);

    qreal fullWidth() const { return m_fullWidth; }
    void setFullWidth(qreal width);
    qreal offset() const { return m_offset; }
    void setOffset(qreal offset);

    // Chunks painted since the item was made (tests: scrolling paints only what comes into view).
    int paintedChunks() const { return m_paintedChunks; }

signals:
    void editorChanged();
    void mediaIdChanged();
    void sourceChanged();
    void colorChanged();
    void spanChanged();

protected:
    QSGNode *updatePaintNode(QSGNode *old, UpdatePaintNodeData *data) override;
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;

private:
    void reload();
    // The painted chunks are no longer right (zoom, source range, height, colour, waveform).
    void invalidate();
    // The chunks in view (painting the missing ones) and their tiles, then a new frame.
    void layoutTiles();
    QImage paintChunk(int chunk) const;

    QPointer<EditorController> m_editor;
    QMetaObject::Connection m_readyConnection;
    QString m_mediaId;
    int m_sourceIn = 0;
    int m_sourceDuration = 0;
    QColor m_color;
    qreal m_fullWidth = 0.0;
    qreal m_offset = 0.0;
    std::shared_ptr<const engine::Waveform> m_waveform;
    QHash<int, QImage> m_chunks; // by index along the clip
    std::vector<ImageTile> m_tiles;
    QHash<qint64, QImage> m_images;
    int m_paintedChunks = 0;
};

} // namespace velacut::ui
