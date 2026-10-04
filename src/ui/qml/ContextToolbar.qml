// Toolbar between the player and the timeline (SPEC §4, 0bis rule 3): undo/redo, then the actions of what is selected
// (always one click away, the same as the right-click menu and Ctrl+K, from the ActionRegistry), then the timeline
// switches (magnetic main track, snapping, preview axis) and the zoom. Compact icon buttons, the name and the shortcut
// in the tooltip, as in desktop video editors.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Vedit.Components
import Vedit.Theme
import Vedit.UI

Item {
    id: bar

    required property Editor editor
    required property Item timeline

    implicitHeight: Theme.editor.toolbarHeight

    component Tool: IconButton {
        implicitWidth: Theme.editor.toolButtonSize
        implicitHeight: Theme.editor.toolButtonSize
        iconSize: Theme.editor.toolIconSize
    }
    // A switch of the timeline: tinted while on.
    component Toggle: IconButton {
        implicitWidth: Theme.editor.toolButtonSize
        implicitHeight: Theme.editor.toolButtonSize
        iconSize: Theme.editor.toolIconSize
        checkable: true
    }
    component Separator: Rectangle {
        implicitWidth: Theme.editor.hairline
        implicitHeight: Theme.editor.toolButtonSize - Theme.space.sm
        color: Theme.color.outlineVariant
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.space.sm
        anchors.rightMargin: Theme.space.sm
        spacing: Theme.space.xxs

        Tool {
            objectName: "undoButton"
            iconName: "undo"
            enabled: bar.editor.canUndo
            label: bar.editor.undoText !== "" ? qsTr("Undo: %1").arg(bar.editor.undoText) : qsTr("Undo")
            shortcutText: qsTr("Ctrl+Z")
            onClicked: bar.editor.undo()
        }
        Tool {
            objectName: "redoButton"
            iconName: "redo"
            enabled: bar.editor.canRedo
            label: bar.editor.redoText !== "" ? qsTr("Redo: %1").arg(bar.editor.redoText) : qsTr("Redo")
            shortcutText: qsTr("Ctrl+Shift+Z")
            onClicked: bar.editor.redo()
        }
        Separator { Layout.leftMargin: Theme.space.xs; Layout.rightMargin: Theme.space.xs }

        // The actions of the selection: a narrow window scrolls them instead of hiding any.
        Flickable {
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: actions.implicitWidth
            clip: true
            flickableDirection: Flickable.HorizontalFlick
            boundsBehavior: Flickable.StopAtBounds
            Row {
                id: actions
                height: parent.height
                spacing: Theme.space.xxs
                Repeater {
                    model: bar.editor.actions.toolbar
                    delegate: Tool {
                        required property var modelData
                        objectName: modelData.id + "Button"
                        y: (actions.height - height) / 2
                        iconName: modelData.icon
                        text: modelData.text
                        label: modelData.text
                        shortcutText: modelData.shortcut
                        enabled: modelData.enabled
                        onClicked: bar.editor.actions.trigger(modelData.id)
                    }
                }
            }
        }

        Separator { Layout.leftMargin: Theme.space.xs; Layout.rightMargin: Theme.space.xs }

        Toggle {
            objectName: "magneticButton"
            iconName: "auto_awesome_motion"
            checked: bar.editor.magneticMain
            label: bar.editor.magneticMain ? qsTr("Magnetic main track (On)") : qsTr("Magnetic main track (Off)")
            shortcutText: "N"
            onClicked: bar.editor.toggleMagneticMain()
        }
        Toggle {
            objectName: "snappingButton"
            iconName: "straighten"
            checked: bar.editor.snappingEnabled
            label: bar.editor.snappingEnabled ? qsTr("Snapping (On)") : qsTr("Snapping (Off)")
            shortcutText: "\\"
            onClicked: bar.editor.toggleSnapping()
        }
        Toggle {
            objectName: "skimmingButton"
            iconName: "preview"
            checked: bar.editor.skimmingEnabled
            label: bar.editor.skimmingEnabled ? qsTr("Preview axis (On): point at the timeline to see that frame")
                                              : qsTr("Preview axis (Off)")
            onClicked: bar.editor.toggleSkimming()
        }
        Tool {
            visible: bar.editor.hasInOut
            iconName: "clear"
            label: qsTr("Clear In/Out points")
            shortcutText: "Alt+X"
            onClicked: bar.editor.clearInOut()
        }

        Separator { Layout.leftMargin: Theme.space.xs; Layout.rightMargin: Theme.space.xs }

        Tool {
            iconName: "zoom_out"
            label: qsTr("Zoom out")
            shortcutText: qsTr("Ctrl+wheel")
            onClicked: bar.timeline.zoomBy(1 / Theme.editor.zoomStep)
        }
        Slider {
            Layout.preferredWidth: Theme.editor.libraryWidth / 3
            // Logarithmic: equal steps feel equal at every scale.
            from: Math.log(Theme.editor.zoomMinimum)
            to: Math.log(Theme.editor.zoomMaximum)
            value: Math.log(bar.timeline.zoom)
            Accessible.name: qsTr("Timeline zoom")
            onMoved: bar.timeline.setZoom(Math.exp(value))
        }
        Tool {
            iconName: "zoom_in"
            label: qsTr("Zoom in")
            shortcutText: qsTr("Ctrl+wheel")
            onClicked: bar.timeline.zoomBy(Theme.editor.zoomStep)
        }
        Tool {
            iconName: "fit_screen"
            label: qsTr("Show the whole video")
            shortcutText: qsTr("Ctrl+0")
            onClicked: bar.timeline.zoomToFit()
        }
    }
}
