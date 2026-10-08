// The keyframe diamond next to an animatable property (SPEC §4, §5.6): empty grey = not animated, empty coloured =
// animated, filled = a keyframe at the playhead. A click adds or removes the keyframe at the playhead.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import Velacut.Components
import Velacut.Theme
import Velacut.UI

Item {
    id: button

    required property Inspector inspector
    required property string key // "position", "scale", "rotation", "opacity"
    readonly property int keyState: inspector.values["kf." + key] ?? 0
    readonly property bool available: inspector.values["kf.available"] ?? false

    objectName: "keyframe_" + key
    implicitWidth: Theme.space.control(Theme.space.xl + Theme.space.sm)
    implicitHeight: implicitWidth
    opacity: available ? 1 : Theme.state.disabledContent
    Accessible.role: Accessible.Button
    Accessible.name: keyState === 2 ? qsTr("Remove keyframe") : qsTr("Add keyframe")

    Icon {
        anchors.centerIn: parent
        name: "diamond"
        filled: button.keyState === 2
        size: Theme.space.lg
        color: button.keyState > 0 ? Theme.color.primary : Theme.color.onSurfaceVariant
    }
    StateLayer {
        radius: width / 2
        hovered: mouse.containsMouse && button.available
        pressed: mouse.pressed && button.available
    }
    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        enabled: button.available
        cursorShape: Qt.PointingHandCursor
        ToolTip.visible: containsMouse
        ToolTip.text: button.keyState === 2 ? qsTr("Remove the keyframe at the playhead") : qsTr("Add a keyframe at the playhead")
        onClicked: button.inspector.toggleKeyframe(button.key)
    }
}
