import QtQuick
import QtQuick.Templates as T
import Vedit.Components
import Vedit.Theme

T.MenuItem {
    id: control

    property string iconName: ""
    property string shortcutText: ""

    implicitWidth: Math.max(112, row.implicitWidth + leftPadding + rightPadding)
    implicitHeight: Theme.space.control(48)
    leftPadding: 12
    rightPadding: 12

    Accessible.role: Accessible.MenuItem
    Accessible.name: text

    readonly property color contentColor: enabled ? Theme.color.onSurface : Theme.alpha(Theme.color.onSurface, Theme.state.disabledContent)

    contentItem: Row {
        id: row
        spacing: 12
        Icon {
            visible: control.iconName !== "" || control.checkable
            name: control.checkable ? (control.checked ? "check" : "") : control.iconName
            color: Theme.color.onSurfaceVariant
            anchors.verticalCenter: parent.verticalCenter
        }
        Label {
            text: control.text
            role: "labelLarge"
            color: control.contentColor
            anchors.verticalCenter: parent.verticalCenter
        }
        Item { width: 24; height: 1 }
        Label {
            visible: control.shortcutText !== ""
            text: control.shortcutText
            role: "labelLarge"
            color: Theme.color.onSurfaceVariant
            anchors.verticalCenter: parent.verticalCenter
        }
    }
    arrow: Icon {
        visible: control.subMenu
        x: control.width - width - 12
        anchors.verticalCenter: parent.verticalCenter
        name: "arrow_right"
    }
    background: Item {
        StateLayer {
            color: Theme.color.onSurface
            active: control.enabled
            hovered: control.highlighted || control.hovered
            pressed: control.pressed
            pressPoint: Qt.point(control.pressX, control.pressY)
        }
    }
}
