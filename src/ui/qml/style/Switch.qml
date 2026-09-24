import QtQuick
import QtQuick.Templates as T
import Vedit.Components
import Vedit.Theme

T.Switch {
    id: control

    implicitWidth: implicitIndicatorWidth + (text ? spacing + implicitContentWidth : 0) + leftPadding + rightPadding
    implicitHeight: Math.max(Theme.space.minimumTarget, implicitContentHeight)
    padding: 4
    spacing: 12
    focusPolicy: Qt.StrongFocus

    Accessible.role: Accessible.CheckBox
    Accessible.name: text

    indicator: Rectangle {
        implicitWidth: 52
        implicitHeight: 32
        x: control.leftPadding
        anchors.verticalCenter: parent.verticalCenter
        radius: Theme.shape.full
        color: !control.enabled ? Theme.alpha(Theme.color.onSurface, Theme.state.disabledContainer)
             : control.checked ? Theme.color.primary : Theme.color.surfaceContainerHighest
        border.width: control.checked ? 0 : 2
        border.color: control.enabled ? Theme.color.outline : Theme.alpha(Theme.color.onSurface, Theme.state.disabledContainer)
        Behavior on color { ColorAnimation { duration: Theme.motion.short4 } }

        Rectangle {
            readonly property real diameter: control.pressed ? 28 : control.checked ? 24 : 16
            width: diameter
            height: diameter
            radius: diameter / 2
            anchors.verticalCenter: parent.verticalCenter
            x: control.checked ? parent.width - width - 4 : (parent.height - height) / 2
            color: !control.enabled ? Theme.alpha(Theme.color.onSurface, Theme.state.disabledContent)
                 : control.checked ? Theme.color.onPrimary : Theme.color.outline
            Behavior on x { NumberAnimation { duration: Theme.motion.short4; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.motion.standard } }
            Behavior on width { NumberAnimation { duration: Theme.motion.short2 } }
            Behavior on height { NumberAnimation { duration: Theme.motion.short2 } }

            Icon {
                anchors.centerIn: parent
                visible: control.checked
                name: "check"
                size: 16
                color: Theme.color.onPrimaryContainer
            }
            Rectangle {
                anchors.centerIn: parent
                width: 40
                height: 40
                radius: 20
                z: -1
                color: control.checked ? Theme.color.primary : Theme.color.onSurface
                opacity: control.pressed ? Theme.state.pressed : control.hovered || control.visualFocus ? Theme.state.hover : 0
            }
        }
    }

    contentItem: Label {
        leftPadding: control.indicator.width + control.spacing
        text: control.text
        role: "bodyLarge"
        color: control.enabled ? Theme.color.onSurface : Theme.alpha(Theme.color.onSurface, Theme.state.disabledContent)
    }
}
