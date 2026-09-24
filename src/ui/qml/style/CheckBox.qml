import QtQuick
import QtQuick.Templates as T
import Vedit.Components
import Vedit.Theme

T.CheckBox {
    id: control

    implicitWidth: implicitIndicatorWidth + (text ? spacing + implicitContentWidth : 0) + leftPadding + rightPadding
    implicitHeight: Math.max(Theme.space.minimumTarget, implicitContentHeight)
    padding: 0
    spacing: 4
    focusPolicy: Qt.StrongFocus

    Accessible.role: Accessible.CheckBox
    Accessible.name: text

    readonly property bool _on: checkState !== Qt.Unchecked

    indicator: Item {
        implicitWidth: 40
        implicitHeight: 40
        x: control.leftPadding
        anchors.verticalCenter: parent.verticalCenter
        Rectangle {
            anchors.fill: parent
            radius: 20
            color: control._on ? Theme.color.primary : Theme.color.onSurface
            opacity: control.pressed ? Theme.state.pressed : control.hovered || control.visualFocus ? Theme.state.hover : 0
        }
        Rectangle {
            anchors.centerIn: parent
            width: 18
            height: 18
            radius: 2
            color: !control._on ? "transparent" : control.enabled ? Theme.color.primary : Theme.alpha(Theme.color.onSurface, Theme.state.disabledContent)
            border.width: control._on ? 0 : 2
            border.color: control.enabled ? Theme.color.onSurfaceVariant : Theme.alpha(Theme.color.onSurface, Theme.state.disabledContent)
            Icon {
                anchors.centerIn: parent
                visible: control._on
                name: control.checkState === Qt.PartiallyChecked ? "remove" : "check"
                size: 18
                weight: 700
                color: control.enabled ? Theme.color.onPrimary : Theme.color.surface
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
