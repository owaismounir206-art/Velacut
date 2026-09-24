// Material 3 search bar (library panels, universal search Ctrl+K).
import QtQuick
import QtQuick.Templates as T
import Vedit.Theme

T.TextField {
    id: control

    implicitWidth: 360
    implicitHeight: Theme.space.control(56)
    leftPadding: 52
    rightPadding: text.length > 0 ? 52 : 16
    font: Theme.type.bodyLarge
    color: Theme.color.onSurface
    placeholderTextColor: Theme.color.onSurfaceVariant
    verticalAlignment: TextInput.AlignVCenter
    selectionColor: Theme.alpha(Theme.color.primary, 0.4)

    Accessible.role: Accessible.EditableText

    background: Rectangle {
        radius: Theme.shape.full
        color: Theme.color.surfaceContainerHigh
        TypeText {
            // placeholder (templates do not draw it)
            x: control.leftPadding
            width: parent.width - control.leftPadding - control.rightPadding
            anchors.verticalCenter: parent.verticalCenter
            visible: control.text.length === 0 && control.preeditText.length === 0
            text: control.placeholderText
            role: "bodyLarge"
            color: control.placeholderTextColor
            elide: Text.ElideRight
        }
        Icon {
            name: "search"
            x: 16
            anchors.verticalCenter: parent.verticalCenter
            color: Theme.color.onSurface
        }
        IconButton {
            visible: control.text.length > 0
            iconName: "close"
            label: qsTr("Clear")
            anchors.right: parent.right
            anchors.rightMargin: 4
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
