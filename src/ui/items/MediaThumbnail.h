// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QHash>
#include <QImage>
#include <QPointer>
#include <QQuickItem>
#include <QtQml/qqmlregistration.h>

#include "items/ImageTiles.h"

#include <vector>

namespace velacut::ui {

class EditorController;

// Frames of a media item from its cached thumbnail strips (D-16), drawn by the scene graph: each strip becomes a
// texture once, and the tiles are image nodes that show a part of it (scrolling changes coordinates only, nothing is
// painted again; image nodes work on every backend, the software one included). Two uses:
//  - timeline clip: tiles along the width, each showing the frame of the source time under it
//    (sourceIn/sourceDuration in project frames). Zoomed in so far that the strip has less than one frame per tile,
//    the tiles come from the denser strips (4 frames a second, made a few seconds at a time for the part in view);
//  - media pool tile: one frame filling the item (cropped), the one at `position` (0–1) while skimming.
// A clip minutes long is tens of thousands of pixels wide: the item then covers only the part in view, `offset`
// pixels from the clip's start, and `fullWidth` is the width of the whole clip (0: the item is the whole clip).
class MediaThumbnail : public QQuickItem
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(velacut::ui::EditorController *editor READ editor WRITE setEditor NOTIFY editorChanged FINAL)
    Q_PROPERTY(QString mediaId READ mediaId WRITE setMediaId NOTIFY mediaIdChanged FINAL)
    Q_PROPERTY(bool tiled READ tiled WRITE setTiled NOTIFY tiledChanged FINAL)
    Q_PROPERTY(int sourceIn READ sourceIn WRITE setSourceIn NOTIFY sourceChanged FINAL)
    Q_PROPERTY(int sourceDuration READ sourceDuration WRITE setSourceDuration NOTIFY sourceChanged FINAL)
    Q_PROPERTY(double position READ position WRITE setPosition NOTIFY positionChanged FINAL)
    Q_PROPERTY(bool ready READ ready NOTIFY readyChanged FINAL)
    Q_PROPERTY(qreal fullWidth READ fullWidth WRITE setFullWidth NOTIFY spanChanged FINAL)
    Q_PROPERTY(qreal offset READ offset WRITE setOffset NOTIFY spanChanged FINAL)
    // Tiles drawn from the denser strips now (tests).
    Q_PROPERTY(int denseTiles READ denseTiles NOTIFY tilesChanged FINAL)

public:
    explicit MediaThumbnail(QQuickItem *parent = nullptr);

    EditorController *editor() const { return m_editor; }
    void setEditor(EditorController *editor);
    QString mediaId() const { return m_mediaId; }
    void setMediaId(const QString &mediaId);
    bool tiled() const { return m_tiled; }
    void setTiled(bool tiled);
    int sourceIn() const { return m_sourceIn; }
    void setSourceIn(int frames);
    int sourceDuration() const { return m_sourceDuration; }
    void setSourceDuration(int frames);
    double position() const { return m_position; }
    void setPosition(double position);
    bool ready() const { return !m_strip.isNull(); }
    int denseTiles() const { return m_denseTiles; }

    qreal fullWidth() const { return m_fullWidth; }
    void setFullWidth(qreal width);
    qreal offset() const { return m_offset; }
    void setOffset(qreal offset);

signals:
    void editorChanged();
    void mediaIdChanged();
    void tiledChanged();
    void sourceChanged();
    void positionChanged();
    void readyChanged();
    void spanChanged();
    void tilesChanged();

protected:
    QSGNode *updatePaintNode(QSGNode *old, UpdatePaintNodeData *data) override;
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;

private:
    void reload();
    // Which tiles, from which strips (on the GUI thread: asks for the missing dense strips), then a new frame.
    void layoutTiles();

    QPointer<EditorController> m_editor;
    QMetaObject::Connection m_readyConnection;
    QString m_mediaId;
    bool m_tiled = false;
    int m_sourceIn = 0;
    int m_sourceDuration = 0;
    double m_position = 0.5;
    QImage m_strip;
    int m_count = 1;
    double m_mediaSeconds = 0.0;
    qreal m_fullWidth = 0.0;
    qreal m_offset = 0.0;
    std::vector<ImageTile> m_tiles;
    QHash<qint64, QImage> m_images; // the strips the tiles use, read by the render thread while the GUI waits
    int m_denseTiles = 0;
};

} // namespace velacut::ui
