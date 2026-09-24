// Material 3 chip: variant "assist", "filter" (checkable, shows a check when selected), "input" (removable).
import QtQuick
import QtQuick.Templates as T
import Vedit.Theme

T.AbstractButton {
    id: control

    property string variant: "filter"
    property string iconName: ""
    signal removeClicked()

    readonly property bool _selected: checked
    checkable: variant === "filter"

    implicitHeight: 32
    implicitWidth: row.implicitWidth + leftPadding + rightPadding
    leftPadding: (_selected || iconName !== "") ? 8 : 16
    rightPadding: variant === "input" ? 8 : 16
    focusPolicy: Qt.StrongFocus

    Accessible.role: variant === "filter" ? Accessible.CheckBox : Accessible.Button
    Accessible.name: text
    Accessible.checked: checked

    readonly property color contentColor: !enabled ? Theme.alpha(Theme.color.onSurface, Theme.state.disabledContent)
                                        : _selected ? Theme.color.onSecondaryContainer : Theme.color.onSurfaceVariant

    contentItem: Row {
        id: row
        spacing: 8
        Icon {
            visible: control._selected || control.iconName !== ""
            name: control._selected ? "check" : control.iconName
            size: 18
            color: control._selected ? Theme.color.onSecondaryContainer : Theme.color.primary
            anchors.verticalCenter: parent.verticalCenter
        }
        TypeText {
            text: control.text
            role: "labelLarge"
            color: control.contentColor
            anchors.verticalCenter: parent.verticalCenter
        }
        Icon {
            visible: control.variant === "input"
            name: "close"
            size: 18
            color: control.contentColor
            anchors.verticalCenter: parent.verticalCenter
            TapHandler { onTapped: control.removeClicked() }
        }
    }

    background: Rectangle {
        radius: Theme.shape.small
        color: control._selected ? Theme.color.secondaryContainer : "transparent"
        border.width: control._selected ? 0 : 1
        border.color: control.enabled ? Theme.color.outlineVariant : Theme.alpha(Theme.color.onSurface, Theme.state.disabledContainer)
        StateLayer {
            radius: parent.radius
            color: control.contentColor
            active: control.enabled
            hovered: control.hovered
            pressed: control.pressed
            focused: control.visualFocus
            pressPoint: Qt.point(control.pressX, control.pressY)
        }
    }
}
