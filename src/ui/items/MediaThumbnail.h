// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QImage>
#include <QPointer>
#include <QQuickPaintedItem>
#include <QtQml/qqmlregistration.h>

namespace vedit::ui {

class EditorController;

// Frames of a media item from its cached thumbnail strip (D-16), painted on the CPU (every scene graph backend).
// Two uses:
//  - timeline clip: tiles along the width, each showing the frame of the source time under it
//    (sourceIn/sourceDuration in project frames);
//  - media pool tile: one frame filling the item (cropped), the one at `position` (0–1) while skimming.
class MediaThumbnail : public QQuickPaintedItem
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(vedit::ui::EditorController *editor READ editor WRITE setEditor NOTIFY editorChanged FINAL)
    Q_PROPERTY(QString mediaId READ mediaId WRITE setMediaId NOTIFY mediaIdChanged FINAL)
    Q_PROPERTY(bool tiled READ tiled WRITE setTiled NOTIFY tiledChanged FINAL)
    Q_PROPERTY(int sourceIn READ sourceIn WRITE setSourceIn NOTIFY sourceChanged FINAL)
    Q_PROPERTY(int sourceDuration READ sourceDuration WRITE setSourceDuration NOTIFY sourceChanged FINAL)
    Q_PROPERTY(double position READ position WRITE setPosition NOTIFY positionChanged FINAL)
    Q_PROPERTY(bool ready READ ready NOTIFY readyChanged FINAL)

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

    void paint(QPainter *painter) override;

signals:
    void editorChanged();
    void mediaIdChanged();
    void tiledChanged();
    void sourceChanged();
    void positionChanged();
    void readyChanged();

private:
    void reload();

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
};

} // namespace vedit::ui
