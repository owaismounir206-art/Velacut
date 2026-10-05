// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/commands/EditCommand.h"
#include "core/project/Clip.h"
#include "core/project/Id.h"

#include <QObject>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <optional>

namespace vedit {
struct Track;
}

namespace vedit::ui {

class EditorController;

// The captions of the project (SPEC §5.8) for the Captions library and the caption page of the properties: the lines
// of the caption track (edit, split, join, delete, move all, find and replace), its look (a style of the library, then
// any value changed), and subtitle files in and out (SRT, WebVTT). Every change is one undo step.
//
// Style keys: wordsPerLine (0 = whole line), position (−0.5…0.5 of the canvas height from its centre), highlight
// (CaptionHighlight: 0 none, 1 colour, 2 size, 3 box, 4 karaoke), highlightColor, animation (CaptionAnimation: 0 none,
// 1 pop, 2 fade, 3 bounce), uppercase, size (share of the canvas height), color, bold, stroke, background,
// backgroundColor.
class CaptionsController : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(Captions)
    QML_UNCREATABLE("Provided by Editor.captions")

    Q_PROPERTY(bool hasCaptions READ hasCaptions NOTIFY changed FINAL)
    // [{clipId, start, end (frames), time ("0:04.2"), text}] in time order.
    Q_PROPERTY(QVariantList lines READ lines NOTIFY changed FINAL)
    // The line at the playhead (or the last one before it), -1 before the first.
    Q_PROPERTY(int currentIndex READ currentIndex NOTIFY currentChanged FINAL)
    Q_PROPERTY(QString styleId READ styleId NOTIFY changed FINAL)
    Q_PROPERTY(QVariantMap style READ style NOTIFY changed FINAL)
    Q_PROPERTY(bool locked READ locked NOTIFY changed FINAL)

public:
    explicit CaptionsController(EditorController &editor);

    bool hasCaptions() const;
    QVariantList lines() const;
    int currentIndex() const { return m_current; }
    QString styleId() const;
    QVariantMap style() const;
    bool locked() const;

    // Subtitle files: the lines are added on the caption track (or a new one), in the style chosen last.
    Q_INVOKABLE bool importFile(const QUrl &file);
    // SRT, or WebVTT when the name ends with .vtt.
    Q_INVOKABLE bool exportFile(const QUrl &file);
    Q_INVOKABLE QString exportFileName() const;
    // Where the captions are saved by default: the Videos folder, named after the project.
    Q_INVOKABLE QUrl exportUrl() const;

    // The library: shown on the player while the pointer is over a style, applied with a click.
    Q_INVOKABLE void previewStyle(const QString &presetId);
    Q_INVOKABLE void clearPreview();
    Q_INVOKABLE bool applyStyle(const QString &presetId);
    // One value of the look; a slider drag is one undo step (until endGesture()).
    Q_INVOKABLE bool setStyleValue(const QString &key, const QVariant &value);
    Q_INVOKABLE void endGesture();

    Q_INVOKABLE bool setLineText(int index, const QString &text);
    Q_INVOKABLE void seekToLine(int index);
    Q_INVOKABLE bool splitLine(int index);
    Q_INVOKABLE bool joinWithNext(int index);
    Q_INVOKABLE bool deleteLine(int index);
    // A new line at the playhead, in the free time after it (2 s at most).
    Q_INVOKABLE bool addLine();
    // All the lines earlier (negative) or later.
    Q_INVOKABLE bool shiftAll(double seconds);
    // Lines that contain `text` (any case), and replacing it in all of them at once: the number of lines changed.
    Q_INVOKABLE int countMatches(const QString &text) const;
    Q_INVOKABLE int replaceAll(const QString &text, const QString &replacement);
    // The style for new caption lines when there is no caption track yet (the last one chosen), if any.
    std::optional<CaptionStyle> nextStyle() const { return m_nextStyle; }

signals:
    void changed();
    void currentChanged();

private:
    const Track *track() const;
    const Clip *line(int index) const;
    CaptionStyle currentStyle() const;
    bool setStyle(const CaptionStyle &style, const QString &undoText, bool gesture);
    void updateCurrent();

    EditorController &m_editor;
    int m_current = -1;
    quint64 m_gesture = 1;
    // The style of new captions when there are none yet (the last one chosen in the library).
    std::optional<CaptionStyle> m_nextStyle;
};

} // namespace vedit::ui
