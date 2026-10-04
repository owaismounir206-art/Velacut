// Contextual toolbar above the timeline (SPEC 0bis rule 3): the actions for what is selected, always one click away.
// The actions come from the ActionRegistry (the same ones as the right-click menu and Ctrl+K); zoom on the right.
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

    implicitHeight: Theme.editor.toolbarHeight
    color: Theme.color.surfaceContainerLow

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.space.sm
        anchors.rightMargin: Theme.space.sm
        spacing: Theme.space.xs

        // The actions of the selection (ActionRegistry): a narrow window scrolls them instead of hiding any.
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
                spacing: Theme.space.xs
                Repeater {
                    model: bar.editor.actions.toolbar
                    delegate: Button {
                        required property var modelData
                        objectName: modelData.id + "Button"
                        y: (actions.height - height) / 2
                        variant: "text"
                        iconName: modelData.icon
                        text: modelData.text
                        enabled: modelData.enabled
                        ToolTip.visible: hovered
                        ToolTip.text: modelData.shortcut !== "" ? qsTr("%1 (%2)").arg(modelData.text).arg(modelData.shortcut)
                                                              : modelData.text
                        onClicked: bar.editor.actions.trigger(modelData.id)
                    }
                }
            }
        }

        Divider { vertical: true; Layout.fillHeight: true }

        IconButton {
            iconName: "auto_awesome_motion"
            variant: "tonal"
            checkable: true
            checked: bar.editor.magneticMain
            label: bar.editor.magneticMain ? qsTr("Magnetic main track (On)") : qsTr("Magnetic main track (Off)")
            shortcutText: "N"
            onClicked: bar.editor.toggleMagneticMain()
        }
        IconButton {
            iconName: "straighten"
            variant: "tonal"
            checkable: true
            checked: bar.editor.snappingEnabled
            label: bar.editor.snappingEnabled ? qsTr("Snapping (On)") : qsTr("Snapping (Off)")
            shortcutText: "\\"
            onClicked: bar.editor.toggleSnapping()
        }
        IconButton {
            visible: bar.editor.hasInOut
            iconName: "clear"
            label: qsTr("Clear In/Out points")
            shortcutText: "Alt+X"
            onClicked: bar.editor.clearInOut()
        }

        Divider { vertical: true; Layout.fillHeight: true }

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
