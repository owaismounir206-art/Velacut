// Material 3 common button: variant "filled" (default), "tonal", "outlined", "text", "elevated".
// Optional leading icon: iconName (Material Symbols).
import QtQuick
import QtQuick.Templates as T
import Vedit.Components
import Vedit.Theme

T.Button {
    id: control

    property string variant: "filled"
    property string iconName: ""

    readonly property bool _filled: variant === "filled"
    readonly property bool _tonal: variant === "tonal"
    readonly property bool _outlined: variant === "outlined"
    readonly property bool _text: variant === "text"
    readonly property bool _elevated: variant === "elevated"

    readonly property color containerColor: !enabled ? (_text || _outlined ? "transparent" : Theme.alpha(Theme.color.onSurface, Theme.state.disabledContainer))
                                          : _filled ? Theme.color.primary
                                          : _tonal ? Theme.color.secondaryContainer
                                          : _elevated ? Theme.color.surfaceContainerLow
                                          : "transparent"
    readonly property color contentColor: !enabled ? Theme.alpha(Theme.color.onSurface, Theme.state.disabledContent)
                                        : _filled ? Theme.color.onPrimary
                                        : _tonal ? Theme.color.onSecondaryContainer
                                        : Theme.color.primary

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding, Theme.space.minimumTarget)
    implicitHeight: Theme.space.control(40)
    leftPadding: _text ? (iconName ? 12 : 12) : (iconName ? 16 : 24)
    rightPadding: _text ? (iconName ? 16 : 12) : 24
    topPadding: 0
    bottomPadding: 0
    spacing: 8
    focusPolicy: Qt.StrongFocus

    Accessible.role: Accessible.Button
    Accessible.name: text

    contentItem: Row {
        spacing: control.spacing
        Icon {
            visible: control.iconName !== ""
            name: control.iconName
            size: 18
            color: control.contentColor
            anchors.verticalCenter: parent.verticalCenter
        }
        Label {
            text: control.text
            role: "labelLarge"
            color: control.contentColor
            anchors.verticalCenter: parent.verticalCenter
        }
    }

    background: Rectangle {
        implicitHeight: Theme.space.control(40)
        radius: Theme.shape.full
        color: control.containerColor
        border.width: control._outlined ? 1 : 0
        border.color: control.enabled ? (control.visualFocus ? Theme.color.primary : Theme.color.outline)
                                      : Theme.alpha(Theme.color.onSurface, Theme.state.disabledContainer)

        Shadow {
            level: control._elevated && control.enabled ? (control.hovered ? 2 : 1) : 0
            radius: parent.radius
        }
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
