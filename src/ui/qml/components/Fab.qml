// Material 3 floating action button; with `text` it becomes an extended FAB (e.g. "Esporta").
// color: "primary" (default), "secondary", "tertiary", "surface".
import QtQuick
import QtQuick.Templates as T
import Vedit.Theme

T.AbstractButton {
    id: control

    property string iconName
    property string color: "primary"
    property bool lowered: false

    readonly property bool extended: text !== ""
    readonly property color containerColor: color === "secondary" ? Theme.color.secondaryContainer
                                          : color === "tertiary" ? Theme.color.tertiaryContainer
                                          : color === "surface" ? Theme.color.surfaceContainerHigh
                                          : Theme.color.primaryContainer
    readonly property color contentColor: color === "secondary" ? Theme.color.onSecondaryContainer
                                        : color === "tertiary" ? Theme.color.onTertiaryContainer
                                        : color === "surface" ? Theme.color.primary
                                        : Theme.color.onPrimaryContainer

    implicitHeight: 56
    implicitWidth: extended ? contentItem.implicitWidth + leftPadding + rightPadding : 56
    leftPadding: extended ? 16 : 16
    rightPadding: extended ? 20 : 16
    focusPolicy: Qt.StrongFocus
    opacity: enabled ? 1 : 0.38

    Accessible.role: Accessible.Button
    Accessible.name: text

    contentItem: Row {
        spacing: 12
        Icon {
            name: control.iconName
            size: 24
            filled: true
            color: control.contentColor
            anchors.verticalCenter: parent.verticalCenter
        }
        TypeText {
            visible: control.extended
            text: control.text
            role: "labelLarge"
            color: control.contentColor
            anchors.verticalCenter: parent.verticalCenter
        }
    }

    background: Rectangle {
        radius: Theme.shape.large
        color: control.containerColor
        Shadow {
            level: control.lowered ? 1 : (control.hovered ? 4 : 3)
            radius: parent.radius
        }
        StateLayer {
            radius: parent.radius
            color: control.contentColor
            hovered: control.hovered
            pressed: control.pressed
            focused: control.visualFocus
            pressPoint: Qt.point(control.pressX, control.pressY)
        }
    }
}
