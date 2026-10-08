// Material 3 slider with value indicator while dragging.
import QtQuick
import QtQuick.Templates as T
import Velacut.Components
import Velacut.Theme

T.Slider {
    id: control

    // Text shown in the value indicator (defaults to the rounded value).
    property string valueText: Math.round(value * 100) / 100

    implicitWidth: horizontal ? 200 : 44
    implicitHeight: horizontal ? 44 : 200
    padding: 10
    focusPolicy: Qt.StrongFocus

    Accessible.role: Accessible.Slider

    readonly property color activeColor: enabled ? Theme.color.primary : Theme.alpha(Theme.color.onSurface, Theme.state.disabledContent)

    background: Item {
        x: control.leftPadding
        y: control.topPadding
        width: control.availableWidth
        height: control.availableHeight

        Rectangle {
            // inactive track
            x: control.horizontal ? 0 : (parent.width - width) / 2
            y: control.horizontal ? (parent.height - height) / 2 : 0
            width: control.horizontal ? parent.width : 4
            height: control.horizontal ? 4 : parent.height
            radius: 2
            color: control.enabled ? Theme.color.surfaceContainerHighest : Theme.alpha(Theme.color.onSurface, Theme.state.disabledContainer)
        }
        Rectangle {
            // active track
            x: control.horizontal ? 0 : (parent.width - width) / 2
            y: control.horizontal ? (parent.height - height) / 2 : control.visualPosition * parent.height
            width: control.horizontal ? control.visualPosition * parent.width : 4
            height: control.horizontal ? 4 : (1 - control.visualPosition) * parent.height
            radius: 2
            color: control.activeColor
        }
    }

    handle: Item {
        x: control.leftPadding + (control.horizontal ? control.visualPosition * (control.availableWidth - width) : (control.availableWidth - width) / 2)
        y: control.topPadding + (control.horizontal ? (control.availableHeight - height) / 2 : control.visualPosition * (control.availableHeight - height))
        implicitWidth: 20
        implicitHeight: 20

        Rectangle {
            anchors.centerIn: parent
            width: 40
            height: 40
            radius: 20
            color: control.activeColor
            opacity: control.pressed ? Theme.state.pressed : control.hovered || control.visualFocus ? Theme.state.hover : 0
            Behavior on opacity { NumberAnimation { duration: Theme.motion.short2 } }
        }
        Rectangle {
            anchors.fill: parent
            radius: width / 2
            color: control.activeColor
        }
        // Value indicator
        Rectangle {
            visible: control.pressed
            width: Math.max(28, indicatorText.implicitWidth + 16)
            height: 28
            radius: Theme.shape.full
            color: Theme.color.inverseSurface
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.top
            anchors.bottomMargin: 8
            Label {
                id: indicatorText
                anchors.centerIn: parent
                text: control.valueText
                role: "labelMedium"
                color: Theme.color.inverseOnSurface
            }
        }
    }
}
