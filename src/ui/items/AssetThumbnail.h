// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QImage>
#include <QPointer>
#include <QQuickPaintedItem>
#include <QtQml/qqmlregistration.h>

namespace vedit::ui {

class EditorController;

// The picture of a library item, drawn with the same CPU kernels as the video (SPEC §5.10, §5.11bis):
//  - filter: a frame of the clip it would apply to (the selected one, or the one under the playhead), else a sample
//    scene, with the filter's look;
//  - transition: two sample scenes mixed by the transition at `progress` (the panel animates it while hovered);
//  - text style: a word in the style.
// Rendered in the thread pool and cached, so scrolling a library never blocks the interface.
class AssetThumbnail : public QQuickPaintedItem
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(vedit::ui::EditorController *editor READ editor WRITE setEditor NOTIFY editorChanged FINAL)
    Q_PROPERTY(int kind READ kind WRITE setKind NOTIFY assetChanged FINAL) // AssetLibraryModel::Kind
    Q_PROPERTY(QString assetId READ assetId WRITE setAssetId NOTIFY assetChanged FINAL)
    Q_PROPERTY(double progress READ progress WRITE setProgress NOTIFY progressChanged FINAL)

public:
    explicit AssetThumbnail(QQuickItem *parent = nullptr);

    EditorController *editor() const { return m_editor; }
    void setEditor(EditorController *editor);
    int kind() const { return m_kind; }
    void setKind(int kind);
    QString assetId() const { return m_assetId; }
    void setAssetId(const QString &assetId);
    double progress() const { return m_progress; }
    void setProgress(double progress);

    void paint(QPainter *painter) override;

    // The picture itself (tests): `sample` is the frame filters apply to (null: the sample scene).
    static QImage render(int kind, const QString &assetId, double progress, const QImage &sample, QSize size);

signals:
    void editorChanged();
    void assetChanged();
    void progressChanged();

private:
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;
    void request();
    QImage sample(QString *key) const;

    QPointer<EditorController> m_editor;
    QMetaObject::Connection m_selectionConnection;
    int m_kind = 0;
    QString m_assetId;
    double m_progress = 0.5;
    QImage m_image;
    QString m_pending;
};

} // namespace vedit::ui
