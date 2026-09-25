// Editor top bar: back to the drafts, the project name (click to rename), "Saved", undo/redo, format and Export.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Vedit.Components
import Vedit.Theme
import Vedit.UI

Rectangle {
    id: bar

    required property Editor editor
    signal backRequested()
    signal exportRequested()

    implicitHeight: Theme.space.control(64)
    color: Theme.color.surface

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.space.xs
        anchors.rightMargin: Theme.space.lg
        spacing: Theme.space.sm

        IconButton {
            iconName: "arrow_back"
            label: qsTr("Back to projects")
            onClicked: bar.backRequested()
        }

        // The name is edited in place: click, type, Enter.
        TextInput {
            id: nameInput
            Layout.maximumWidth: bar.width / 3
            Layout.preferredWidth: Math.max(contentWidth, Theme.space.xxxl)
            text: bar.editor.name
            font: Theme.type.titleLarge
            color: Theme.color.onSurface
            selectByMouse: true
            clip: true
            Accessible.name: qsTr("Project name")
            onEditingFinished: {
                bar.editor.name = text
                text = Qt.binding(() => bar.editor.name)
                focus = false
            }
            Keys.onEscapePressed: { text = bar.editor.name; focus = false }
            Rectangle {
                anchors { left: parent.left; right: parent.right; top: parent.bottom }
                height: Theme.editor.hairline
                color: nameInput.activeFocus ? Theme.color.primary : "transparent"
            }
            HoverHandler { cursorShape: Qt.IBeamCursor }
        }

        // "Saved" is always visible: there is no Save (SPEC 0bis rule 2).
        RowLayout {
            spacing: Theme.space.xs
            Icon {
                name: bar.editor.saveState === Editor.SaveFailed ? "cloud_off"
                    : bar.editor.saveState === Editor.Saving ? "cloud_sync" : "cloud_done"
                size: Theme.space.lg + Theme.space.xs
                color: bar.editor.saveState === Editor.SaveFailed ? Theme.color.error : Theme.color.onSurfaceVariant
            }
            Label {
                role: "labelMedium"
                color: bar.editor.saveState === Editor.SaveFailed ? Theme.color.error : Theme.color.onSurfaceVariant
                text: bar.editor.saveState === Editor.SaveFailed ? qsTr("Not saved: retrying")
                    : bar.editor.saveState === Editor.Saving ? qsTr("Saving…") : qsTr("Saved")
                ToolTip.visible: saveHover.hovered && bar.editor.saveError !== ""
                ToolTip.text: bar.editor.saveError
                HoverHandler { id: saveHover }
            }
        }

        Item { Layout.fillWidth: true }

        IconButton {
            iconName: "undo"
            enabled: bar.editor.canUndo
            label: bar.editor.undoText !== "" ? qsTr("Undo: %1").arg(bar.editor.undoText) : qsTr("Undo")
            shortcutText: qsTr("Ctrl+Z")
            onClicked: bar.editor.undo()
        }
        IconButton {
            iconName: "redo"
            enabled: bar.editor.canRedo
            label: bar.editor.redoText !== "" ? qsTr("Redo: %1").arg(bar.editor.redoText) : qsTr("Redo")
            shortcutText: qsTr("Ctrl+Shift+Z")
            onClicked: bar.editor.redo()
        }

        // Format of the canvas: one click (SPEC 0bis rule 1).
        Button {
            variant: "outlined"
            iconName: "aspect_ratio"
            text: bar.editor.formatText
            Accessible.name: qsTr("Format: %1").arg(bar.editor.formatText)
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Change the format of the video")
            onClicked: formatMenu.popup()
            Menu {
                id: formatMenu
                Repeater {
                    // Values of CanvasPreset (core/project/Sequence.h).
                    model: [{ text: qsTr("16:9 — YouTube"), preset: 0, icon: "crop_16_9" },
                            { text: qsTr("9:16 — TikTok, Reels, Shorts"), preset: 1, icon: "crop_portrait" },
                            { text: qsTr("1:1 — Square"), preset: 2, icon: "crop_square" },
                            { text: qsTr("4:5 — Instagram post"), preset: 3, icon: "crop_portrait" },
                            { text: qsTr("3:4 — Portrait"), preset: 5, icon: "crop_3_2" },
                            { text: qsTr("21:9 — Cinema"), preset: 4, icon: "panorama" }]
                    delegate: MenuItem {
                        required property var modelData
                        iconName: modelData.icon
                        text: modelData.text
                        checkable: true
                        checked: bar.editor.canvasPreset === modelData.preset
                        onTriggered: bar.editor.setCanvasPreset(modelData.preset)
                    }
                }
            }
        }

        // Export: visible while it runs, a click opens the details.
        Button {
            variant: "filled"
            iconName: bar.editor.exportJob.running ? "hourglass_top" : "file_upload"
            text: bar.editor.exportJob.running ? qsTr("Exporting %1%").arg(Math.floor(bar.editor.exportJob.progress * 100))
                                                : qsTr("Export")
            enabled: bar.editor.timeline.duration > 0 || bar.editor.exportJob.running
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Save the video (Ctrl+E)")
            onClicked: bar.exportRequested()
        }
    }
}
