// Material 3 plain tooltip (used by every control's ToolTip attached property).
import QtQuick
import QtQuick.Templates as T
import Velacut.Components
import Velacut.Theme

T.ToolTip {
    id: control

    x: parent ? (parent.width - implicitWidth) / 2 : 0
    y: parent ? parent.height + 4 : 0
    implicitWidth: Math.min(312, contentItem.implicitWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(24, contentItem.implicitHeight + topPadding + bottomPadding)
    leftPadding: 8
    rightPadding: 8
    topPadding: 4
    bottomPadding: 4
    margins: 8
    closePolicy: T.Popup.CloseOnEscape | T.Popup.CloseOnPressOutsideParent | T.Popup.CloseOnReleaseOutsideParent

    enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.motion.short3 } }
    exit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Theme.motion.short2 } }

    contentItem: Label {
        text: control.text
        role: "bodySmall"
        wrapMode: Text.Wrap
        color: Theme.color.inverseOnSurface
    }
    background: Rectangle {
        radius: Theme.shape.extraSmall
        color: Theme.color.inverseSurface
    }
}
