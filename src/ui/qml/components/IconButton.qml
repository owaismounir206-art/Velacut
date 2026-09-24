// Material 3 icon button: variant "standard" (default), "filled", "tonal", "outlined".
// checkable icon buttons show the filled icon and the selected colors when checked.
import QtQuick
import QtQuick.Templates as T
import Vedit.Theme

T.AbstractButton {
    id: control

    property string iconName
    property string variant: "standard"
    // Accessible name and tooltip text (icon buttons have no visible label).
    property string label: ""
    property string shortcutText: ""

    readonly property bool _selected: checkable && checked
    readonly property color containerColor: {
        if (!enabled)
            return variant === "standard" || variant === "outlined" ? "transparent" : Theme.alpha(Theme.color.onSurface, Theme.state.disabledContainer)
        switch (variant) {
        case "filled": return checkable && !checked ? Theme.color.surfaceContainerHighest : Theme.color.primary
        case "tonal": return checkable && !checked ? Theme.color.surfaceContainerHighest : Theme.color.secondaryContainer
        case "outlined": return _selected ? Theme.color.inverseSurface : "transparent"
        default: return "transparent"
        }
    }
    readonly property color contentColor: {
        if (!enabled)
            return Theme.alpha(Theme.color.onSurface, Theme.state.disabledContent)
        switch (variant) {
        case "filled": return checkable && !checked ? Theme.color.primary : Theme.color.onPrimary
        case "tonal": return checkable && !checked ? Theme.color.onSurfaceVariant : Theme.color.onSecondaryContainer
        case "outlined": return _selected ? Theme.color.inverseOnSurface : Theme.color.onSurfaceVariant
        default: return _selected ? Theme.color.primary : Theme.color.onSurfaceVariant
        }
    }

    implicitWidth: Theme.space.control(40)
    implicitHeight: Theme.space.control(40)
    padding: 8
    focusPolicy: Qt.StrongFocus

    Accessible.role: Accessible.Button
    Accessible.name: label
    Accessible.checkable: checkable
    Accessible.checked: checked

    T.ToolTip.visible: hovered && label !== ""
    T.ToolTip.delay: 600
    T.ToolTip.text: shortcutText !== "" ? label + " (" + shortcutText + ")" : label

    contentItem: Icon {
        name: control.iconName
        size: 24
        filled: control._selected || control.variant === "filled" && !control.checkable
        color: control.contentColor
    }

    background: Rectangle {
        radius: Theme.shape.full
        color: control.containerColor
        border.width: control.variant === "outlined" && !control._selected ? 1 : 0
        border.color: control.enabled ? Theme.color.outline : Theme.alpha(Theme.color.onSurface, Theme.state.disabledContainer)
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
