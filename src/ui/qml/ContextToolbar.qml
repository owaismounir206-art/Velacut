// Contextual toolbar above the timeline (SPEC 0bis rule 3): the actions for what is selected, always one click away.
// Only actions that work today are shown (Phase 1: split, delete, duplicate); zoom on the right.
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
    required property Item timeline
    readonly property bool hasSelection: editor.selection.length > 0

    implicitHeight: Theme.editor.toolbarHeight
    color: Theme.color.surfaceContainerLow

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.space.sm
        anchors.rightMargin: Theme.space.sm
        spacing: Theme.space.xs

        Button {
            objectName: "splitButton"
            variant: "text"
            iconName: "content_cut"
            text: bar.hasSelection ? qsTr("Split") : qsTr("Split at playhead")
            enabled: bar.editor.splitAvailable
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Cut at the playhead (S)")
            onClicked: bar.editor.split()
        }
        Button {
            objectName: "deleteButton"
            variant: "text"
            iconName: "delete"
            text: qsTr("Delete")
            visible: bar.hasSelection
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Delete the selected clips (Del)")
            onClicked: bar.editor.deleteSelection()
        }
        Button {
            variant: "text"
            iconName: "content_copy"
            text: qsTr("Duplicate")
            visible: bar.hasSelection
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Place a copy right after (Ctrl+D)")
            onClicked: bar.editor.duplicateSelection()
        }

        Item { Layout.fillWidth: true }

        IconButton {
            iconName: "zoom_out"
            label: qsTr("Zoom out")
            shortcutText: qsTr("Ctrl+wheel")
            onClicked: bar.timeline.zoomBy(1 / Theme.editor.zoomStep)
        }
        Slider {
            Layout.preferredWidth: Theme.editor.libraryWidth / 2
            // Logarithmic: equal steps feel equal at every scale.
            from: Math.log(Theme.editor.zoomMinimum)
            to: Math.log(Theme.editor.zoomMaximum)
            value: Math.log(bar.timeline.zoom)
            Accessible.name: qsTr("Timeline zoom")
            onMoved: bar.timeline.setZoom(Math.exp(value))
        }
        IconButton {
            iconName: "zoom_in"
            label: qsTr("Zoom in")
            shortcutText: qsTr("Ctrl+wheel")
            onClicked: bar.timeline.zoomBy(Theme.editor.zoomStep)
        }
        IconButton {
            iconName: "fit_screen"
            label: qsTr("Show the whole video")
            onClicked: bar.timeline.zoomToFit()
        }
    }
}
