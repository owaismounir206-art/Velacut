// Material 3 linear progress indicator (determinate or indeterminate).
import QtQuick
import QtQuick.Templates as T
import Vedit.Components
import Vedit.Theme

T.ProgressBar {
    id: control

    implicitWidth: 200
    implicitHeight: 4

    Accessible.role: Accessible.ProgressBar

    background: Rectangle {
        radius: 2
        color: Theme.color.surfaceContainerHighest
    }
    contentItem: Item {
        clip: true
        Rectangle {
            visible: !control.indeterminate
            width: control.visualPosition * parent.width
            height: parent.height
            radius: 2
            color: Theme.color.primary
        }
        Rectangle {
            id: bar
            visible: control.indeterminate
            height: parent.height
            width: parent.width * 0.4
            radius: 2
            color: Theme.color.primary
            // Essential motion: kept even when animations are reduced.
            NumberAnimation on x {
                running: control.indeterminate && control.visible
                from: -bar.width
                to: control.width
                duration: Theme.motion.essentialLong * 3
                loops: Animation.Infinite
                easing.type: Easing.InOutQuad
            }
        }
    }
}
