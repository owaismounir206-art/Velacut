// Material 3 text field: variant "filled" (default) or "outlined", with floating label, supporting text and error.
import QtQuick
import QtQuick.Templates as T
import Velacut.Components
import Velacut.Theme

T.TextField {
    id: control

    property string variant: "filled"
    property string label: ""
    property string supportingText: ""
    property bool error: false
    property string leadingIconName: ""

    readonly property bool _floating: activeFocus || text.length > 0 || preeditText.length > 0
    readonly property color accent: error ? Theme.color.error : Theme.color.primary

    implicitWidth: 240
    implicitHeight: 56 + (supportingText !== "" ? 20 : 0)
    leftPadding: leadingIconName !== "" ? 52 : 16
    rightPadding: 16
    topPadding: variant === "filled" && label !== "" ? 24 : 16
    bottomPadding: (variant === "filled" && label !== "" ? 8 : 16) + (supportingText !== "" ? 20 : 0)

    font: Theme.type.bodyLarge
    color: enabled ? Theme.color.onSurface : Theme.alpha(Theme.color.onSurface, Theme.state.disabledContent)
    selectionColor: Theme.alpha(accent, 0.4)
    selectedTextColor: Theme.color.onSurface
    placeholderTextColor: Theme.color.onSurfaceVariant
    verticalAlignment: TextInput.AlignVCenter

    Accessible.role: Accessible.EditableText
    Accessible.name: label

    background: Item {
        implicitHeight: 56
        Rectangle {
            id: container
            width: parent.width
            height: 56
            color: control.variant === "filled" ? Theme.color.surfaceContainerHighest : "transparent"
            radius: control.variant === "filled" ? 0 : Theme.shape.extraSmall
            topLeftRadius: Theme.shape.extraSmall
            topRightRadius: Theme.shape.extraSmall
            border.width: control.variant === "outlined" ? (control.activeFocus ? 2 : 1) : 0
            border.color: control.error ? Theme.color.error : control.activeFocus ? control.accent : Theme.color.outline
            FocusFrame { control: control }
            StateLayer {
                radius: 0
                color: Theme.color.onSurface
                hovered: control.hovered && !control.activeFocus
                visible: control.variant === "filled"
            }
            Rectangle {
                // filled: active indicator line
                visible: control.variant === "filled"
                anchors.bottom: parent.bottom
                width: parent.width
                height: control.activeFocus ? 2 : 1
                color: control.error ? Theme.color.error : control.activeFocus ? control.accent : Theme.color.onSurfaceVariant
            }
        }
        Icon {
            visible: control.leadingIconName !== ""
            name: control.leadingIconName
            x: 12
            y: (56 - height) / 2
            color: Theme.color.onSurfaceVariant
        }
        Label {
            // floating label
            visible: control.label !== ""
            text: control.label
            x: control.leftPadding
            role: control._floating ? "bodySmall" : "bodyLarge"
            y: control._floating ? (control.variant === "filled" ? 8 : -height / 2) : (56 - height) / 2
            leftPadding: control.variant === "outlined" && control._floating ? 4 : 0
            rightPadding: leftPadding
            color: control.error ? Theme.color.error : control.activeFocus ? control.accent : Theme.color.onSurfaceVariant
            Rectangle {
                // cuts the outline behind the floating label
                z: -1
                visible: control.variant === "outlined" && control._floating
                anchors.fill: parent
                color: Theme.color.surface
            }
            Behavior on y { NumberAnimation { duration: Theme.motion.short4; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.motion.standard } }
        }
        Label {
            // placeholder: only when there is no label, or once the label floated
            x: control.leftPadding
            y: control.topPadding
            width: parent.width - control.leftPadding - control.rightPadding
            height: 56 - control.topPadding - (control.variant === "filled" && control.label !== "" ? 8 : 16)
            visible: control.text.length === 0 && control.preeditText.length === 0 && (control.label === "" || control.activeFocus)
            text: control.placeholderText
            role: "bodyLarge"
            color: control.placeholderTextColor
            elide: Text.ElideRight
        }
        Label {
            visible: control.supportingText !== ""
            text: control.supportingText
            role: "bodySmall"
            x: 16
            y: 60
            color: control.error ? Theme.color.error : Theme.color.onSurfaceVariant
        }
    }
}
