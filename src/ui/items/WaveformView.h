// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QColor>
#include <QPointer>
#include <QQuickPaintedItem>
#include <QtQml/qqmlregistration.h>

#include <memory>

namespace vedit::engine {
struct Waveform;
}

namespace vedit::ui {

class EditorController;

// The waveform of a clip's audio (min/max peaks of the cached waveform, D-16), for the source range
// sourceIn…sourceIn+sourceDuration (project frames), painted on the CPU.
class WaveformView : public QQuickPaintedItem
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(vedit::ui::EditorController *editor READ editor WRITE setEditor NOTIFY editorChanged FINAL)
    Q_PROPERTY(QString mediaId READ mediaId WRITE setMediaId NOTIFY mediaIdChanged FINAL)
    Q_PROPERTY(int sourceIn READ sourceIn WRITE setSourceIn NOTIFY sourceChanged FINAL)
    Q_PROPERTY(int sourceDuration READ sourceDuration WRITE setSourceDuration NOTIFY sourceChanged FINAL)
    Q_PROPERTY(QColor color READ color WRITE setColor NOTIFY colorChanged FINAL)

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

    void paint(QPainter *painter) override;

signals:
    void editorChanged();
    void mediaIdChanged();
    void sourceChanged();
    void colorChanged();

private:
    void reload();

    QPointer<EditorController> m_editor;
    QMetaObject::Connection m_readyConnection;
    QString m_mediaId;
    int m_sourceIn = 0;
    int m_sourceDuration = 0;
    QColor m_color;
    std::shared_ptr<const engine::Waveform> m_waveform;
};

} // namespace vedit::ui
