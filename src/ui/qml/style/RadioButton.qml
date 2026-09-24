import QtQuick
import QtQuick.Templates as T
import Vedit.Components
import Vedit.Theme

T.RadioButton {
    id: control

    implicitWidth: implicitIndicatorWidth + (text ? spacing + implicitContentWidth : 0) + leftPadding + rightPadding
    implicitHeight: Math.max(Theme.space.minimumTarget, implicitContentHeight)
    padding: 0
    spacing: 4
    focusPolicy: Qt.StrongFocus

    Accessible.role: Accessible.RadioButton
    Accessible.name: text

    indicator: Item {
        implicitWidth: 40
        implicitHeight: 40
        x: control.leftPadding
        anchors.verticalCenter: parent.verticalCenter
        readonly property color ring: !control.enabled ? Theme.alpha(Theme.color.onSurface, Theme.state.disabledContent)
                                    : control.checked ? Theme.color.primary : Theme.color.onSurfaceVariant
        Rectangle {
            anchors.fill: parent
            radius: 20
            color: control.checked ? Theme.color.primary : Theme.color.onSurface
            opacity: control.pressed ? Theme.state.pressed : control.hovered || control.visualFocus ? Theme.state.hover : 0
        }
        Rectangle {
            anchors.centerIn: parent
            width: 20
            height: 20
            radius: 10
            color: "transparent"
            border.width: 2
            border.color: parent.ring
            Rectangle {
                anchors.centerIn: parent
                width: control.checked ? 10 : 0
                height: width
                radius: width / 2
                color: parent.border.color
                Behavior on width { NumberAnimation { duration: Theme.motion.short3 } }
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
