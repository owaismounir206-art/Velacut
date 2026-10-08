// The keyboard shortcuts of the editor, grouped (Menu → Keyboard shortcuts). The same list as docs/SHORTCUTS.md: only
// shortcuts that exist (EditorScreen.qml).
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Velacut.Components
import Velacut.Theme

Dialog {
    id: dialog

    title: qsTr("Keyboard shortcuts")
    iconName: "keyboard"
    standardButtons: Dialog.Close
    modal: true

    readonly property var groups: [
        { title: qsTr("Playback"), items: [
            ["Space", qsTr("Play / pause")],
            ["J  K  L", qsTr("Play backwards, pause, play forwards (press again: faster)")],
            ["←  →", qsTr("Previous / next frame")],
            ["Shift+←  Shift+→", qsTr("10 frames back / forward")],
            ["↑  ↓", qsTr("Previous / next cut")],
            ["Home  End", qsTr("Go to the start / end")]] },
        { title: qsTr("Editing"), items: [
            ["S  Ctrl+B", qsTr("Split at the playhead")],
            ["Q  W", qsTr("Delete left / right of the playhead")],
            ["[  ]", qsTr("Trim the start / end of the clip to the playhead")],
            ["Delete", qsTr("Delete the selection")],
            ["Shift+Delete", qsTr("Delete and close the gap")],
            ["Ctrl+D", qsTr("Duplicate")],
            ["Ctrl+A", qsTr("Select all")],
            ["Esc", qsTr("Deselect")],
            ["Ctrl+Alt+C  Ctrl+Alt+V", qsTr("Copy / paste attributes")],
            ["Ctrl+T", qsTr("Add a text")],
            ["Ctrl+Z  Ctrl+Shift+Z", qsTr("Undo / redo")]] },
        { title: qsTr("Timeline"), items: [
            ["M", qsTr("Add a marker")],
            ["I  O  Alt+X", qsTr("Set In / Out, clear them")],
            ["N", qsTr("Magnetic main track on / off")],
            ["\\", qsTr("Snapping on / off")],
            ["+  −", qsTr("Zoom in / out (also Ctrl+wheel)")],
            ["Ctrl+0", qsTr("Show the whole video")],
            ["1 … 9", qsTr("Switch camera (multicam clip)")]] },
        { title: qsTr("Project"), items: [
            ["Ctrl+N", qsTr("New project")],
            ["Ctrl+I", qsTr("Import media")],
            ["Ctrl+E", qsTr("Export")],
            ["Ctrl+Shift+E", qsTr("Save the current frame as an image")],
            ["Ctrl+K", qsTr("Search commands, effects, transitions, music")],
            ["Ctrl+,", qsTr("Preferences")],
            ["F11", qsTr("Full-screen preview")]] }
    ]

    ScrollView {
        id: scroll
        anchors.fill: parent
        implicitHeight: column.implicitHeight
        contentWidth: availableWidth
        clip: true

        ColumnLayout {
            id: column
            width: scroll.availableWidth
            spacing: Theme.space.lg

            Repeater {
                model: dialog.groups
                delegate: ColumnLayout {
                    id: group
                    required property var modelData
                    Layout.fillWidth: true
                    spacing: Theme.space.xs
                    Label {
                        role: "titleSmall"
                        color: Theme.color.primary
                        text: group.modelData.title
                    }
                    Repeater {
                        model: group.modelData.items
                        delegate: RowLayout {
                            id: line
                            required property var modelData
                            Layout.fillWidth: true
                            spacing: Theme.space.md
                            Rectangle {
                                Layout.preferredWidth: Theme.editor.valueWidth * 3
                                implicitHeight: keys.implicitHeight + Theme.space.xs
                                radius: Theme.shape.extraSmall
                                color: Theme.color.surfaceContainerHighest
                                Label {
                                    id: keys
                                    anchors.centerIn: parent
                                    role: "labelMedium"
                                    font.features: { "tnum": 1 }
                                    text: line.modelData[0]
                                }
                            }
                            Label {
                                Layout.fillWidth: true
                                role: "bodyMedium"
                                wrapMode: Text.WordWrap
                                text: line.modelData[1]
                            }
                        }
                    }
                }
            }
        }
    }
}
