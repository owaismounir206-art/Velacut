// Material 3 snackbar: "Clip eliminata — Annulla". show(text, actionText, callback).
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import Velacut.Theme

Item {
    id: root

    property int timeout: 5000
    signal actionTriggered()

    function show(message, actionText, onAction) {
        messageLabel.text = message
        action.text = actionText ?? ""
        root._callback = onAction ?? null
        box.opacity = 1
        timer.restart()
    }
    function dismiss() {
        timer.stop()
        box.opacity = 0
    }
    property var _callback: null

    implicitHeight: box.height
    implicitWidth: box.width
    visible: box.opacity > 0

    Rectangle {
        id: box
        width: Math.min(560, Math.max(344, row.implicitWidth + 32))
        height: 48
        radius: Theme.shape.extraSmall
        color: Theme.color.inverseSurface
        opacity: 0
        Behavior on opacity { NumberAnimation { duration: Theme.motion.short4 } }
        Shadow { level: 3; radius: parent.radius }

        Accessible.role: Accessible.AlertMessage
        Accessible.name: messageLabel.text

        RowLayout {
            id: row
            anchors.fill: parent
            anchors.leftMargin: 16
            anchors.rightMargin: 8
            spacing: 8
            TypeText {
                id: messageLabel
                Layout.fillWidth: true
                role: "bodyMedium"
                color: Theme.color.inverseOnSurface
                elide: Text.ElideRight
            }
            T.AbstractButton {
                id: action
                visible: text !== ""
                implicitHeight: 40
                implicitWidth: actionText.implicitWidth + 24
                contentItem: TypeText {
                    id: actionText
                    text: action.text
                    role: "labelLarge"
                    color: Theme.color.inversePrimary
                    horizontalAlignment: Text.AlignHCenter
                }
                background: StateLayer {
                    radius: Theme.shape.full
                    color: Theme.color.inversePrimary
                    hovered: action.hovered
                    pressed: action.pressed
                    focused: action.visualFocus
                    pressPoint: Qt.point(action.pressX, action.pressY)
                }
                Accessible.role: Accessible.Button
                Accessible.name: text
                onClicked: {
                    if (root._callback)
                        root._callback()
                    root.actionTriggered()
                    root.dismiss()
                }
            }
        }
    }
    Timer {
        id: timer
        interval: root.timeout
        onTriggered: root.dismiss()
    }
}
