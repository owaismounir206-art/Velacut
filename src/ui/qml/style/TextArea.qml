// Material 3 filled text field for several lines (e.g. the content of a text clip): label above the text.
import QtQuick
import QtQuick.Templates as T
import Vedit.Components
import Vedit.Theme

T.TextArea {
    id: control

    property string label: ""

    implicitWidth: Math.max(contentWidth + leftPadding + rightPadding, Theme.editor.propertiesWidth)
    implicitHeight: Math.max(contentHeight + topPadding + bottomPadding, Theme.editor.textAreaHeight)
    leftPadding: Theme.space.lg
    rightPadding: Theme.space.lg
    topPadding: label !== "" ? Theme.space.xl : Theme.space.md
    bottomPadding: Theme.space.md

    font: Theme.type.bodyLarge
    wrapMode: TextEdit.Wrap
    color: enabled ? Theme.color.onSurface : Theme.alpha(Theme.color.onSurface, Theme.state.disabledContent)
    selectionColor: Theme.alpha(Theme.color.primary, 0.4)
    selectedTextColor: Theme.color.onSurface
    placeholderTextColor: Theme.color.onSurfaceVariant

    Accessible.role: Accessible.EditableText
    Accessible.name: label

    background: Rectangle {
        color: Theme.color.surfaceContainerHighest
        topLeftRadius: Theme.shape.extraSmall
        topRightRadius: Theme.shape.extraSmall
        StateLayer {
            color: Theme.color.onSurface
            hovered: control.hovered && !control.activeFocus
        }
        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: control.activeFocus ? Theme.editor.selectionBorder : Theme.editor.hairline
            color: control.activeFocus ? Theme.color.primary : Theme.color.onSurfaceVariant
        }
        TypeText {
            visible: control.label !== ""
            x: control.leftPadding
            y: Theme.space.xs
            text: control.label
            role: "bodySmall"
            color: control.activeFocus ? Theme.color.primary : Theme.color.onSurfaceVariant
        }
        TypeText {
            x: control.leftPadding
            y: control.topPadding
            visible: control.length === 0 && control.preeditText.length === 0
            text: control.placeholderText
            role: "bodyLarge"
            color: control.placeholderTextColor
        }
    }
}
