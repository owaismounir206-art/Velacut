// Material 3 search bar (library panels, universal search Ctrl+K). `compact`: the dense form of the editor's panels.
import QtQuick
import QtQuick.Templates as T
import Vedit.Theme

T.TextField {
    id: control

    property bool compact: false
    readonly property real _iconSize: compact ? Theme.editor.toolIconSize : 24

    implicitWidth: 360
    implicitHeight: compact ? Theme.editor.toolButtonSize + Theme.space.xs : Theme.space.control(56)
    leftPadding: compact ? Theme.space.md + _iconSize + Theme.space.sm : 52
    rightPadding: text.length > 0 ? (compact ? Theme.editor.toolButtonSize + Theme.space.xs : 52) : Theme.space.lg
    font: compact ? Theme.type.bodyMedium : Theme.type.bodyLarge
    color: Theme.color.onSurface
    placeholderTextColor: Theme.color.onSurfaceVariant
    verticalAlignment: TextInput.AlignVCenter
    selectionColor: Theme.alpha(Theme.color.primary, 0.4)

    Accessible.role: Accessible.EditableText

    background: Rectangle {
        radius: Theme.shape.full
        color: Theme.color.surfaceContainerHigh
        border.width: control.activeFocus ? Theme.editor.hairline : 0
        border.color: Theme.color.primary
        TypeText {
            // placeholder (templates do not draw it)
            x: control.leftPadding
            width: parent.width - control.leftPadding - control.rightPadding
            anchors.verticalCenter: parent.verticalCenter
            visible: control.text.length === 0 && control.preeditText.length === 0
            text: control.placeholderText
            role: control.compact ? "bodyMedium" : "bodyLarge"
            color: control.placeholderTextColor
            elide: Text.ElideRight
        }
        Icon {
            name: "search"
            x: control.compact ? Theme.space.md : Theme.space.lg
            size: control._iconSize
            anchors.verticalCenter: parent.verticalCenter
            color: Theme.color.onSurfaceVariant
        }
        IconButton {
            visible: control.text.length > 0
            implicitWidth: control.compact ? Theme.editor.toolButtonSize : Theme.space.control(40)
            implicitHeight: implicitWidth
            iconSize: control._iconSize
            iconName: "close"
            label: qsTr("Clear")
            anchors.right: parent.right
            anchors.rightMargin: Theme.space.xs
            anchors.verticalCenter: parent.verticalCenter
            onClicked: control.clear()
        }
        StateLayer {
            radius: parent.radius
            color: Theme.color.onSurface
            hovered: control.hovered && !control.activeFocus
        }
    }
}
