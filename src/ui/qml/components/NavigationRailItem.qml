import QtQuick
import Vedit.Theme

Item {
    id: root

    property string text
    property string iconName
    property bool selected: false
    property bool focusIndicator: false
    property int badgeCount: -1
    property bool compact: false
    signal clicked()

    implicitHeight: compact ? 52 : 56
    implicitWidth: 80

    Accessible.role: Accessible.PageTab
    Accessible.name: text
    Accessible.selected: selected

    Rectangle {
        id: pill
        anchors.horizontalCenter: parent.horizontalCenter
        y: 0
        width: 56
        height: 32
        radius: Theme.shape.full
        color: root.selected ? Theme.color.secondaryContainer : "transparent"
        Behavior on color { ColorAnimation { duration: Theme.motion.short4 } }
        StateLayer {
            radius: parent.radius
            color: root.selected ? Theme.color.onSecondaryContainer : Theme.color.onSurfaceVariant
            hovered: mouse.containsMouse
            pressed: mouse.pressed
            focused: root.focusIndicator
            pressPoint: mouse.mapToItem(pill, mouse.mouseX, mouse.mouseY)
        }
        Icon {
            anchors.centerIn: parent
            name: root.iconName
            filled: root.selected
            color: root.selected ? Theme.color.onSecondaryContainer : Theme.color.onSurfaceVariant
        }
        Badge {
            visible: root.badgeCount >= 0
            count: root.badgeCount
            anchors.left: parent.horizontalCenter
            anchors.leftMargin: 6
            anchors.top: parent.top
            anchors.topMargin: 2
        }
    }
    TypeText {
        anchors.top: pill.bottom
        anchors.topMargin: root.compact ? 2 : 4
        anchors.horizontalCenter: parent.horizontalCenter
        width: parent.width - 4
        horizontalAlignment: Text.AlignHCenter
        elide: Text.ElideRight
        text: root.text
        role: root.compact ? "labelSmall" : "labelMedium"
        color: root.selected ? Theme.color.onSurface : Theme.color.onSurfaceVariant
    }
    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        onClicked: root.clicked()
    }
}
