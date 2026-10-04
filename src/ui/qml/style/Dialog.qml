// Material 3 basic dialog. Blocking confirmations are reserved for deleting a project and overwriting a
// file (SPEC 0bis rule 10): everything else uses an undoable action and a snackbar.
import QtQuick
import QtQuick.Templates as T
import Vedit.Components
import Vedit.Theme

T.Dialog {
    id: control

    property string iconName: ""

    anchors.centerIn: T.Overlay.overlay
    implicitWidth: Math.max(280, Math.min(560, implicitContentWidth + leftPadding + rightPadding))
    implicitHeight: implicitHeaderHeight + implicitContentHeight + implicitFooterHeight + topPadding + bottomPadding
    padding: 24
    topPadding: 16
    modal: true

    enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.motion.medium2 } }
    exit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Theme.motion.short4 } }

    header: Column {
        topPadding: 24
        leftPadding: 24
        rightPadding: 24
        spacing: 16
        Icon {
            visible: control.iconName !== ""
            name: control.iconName
            color: Theme.color.secondary
            anchors.horizontalCenter: parent.horizontalCenter
        }
        Label {
            visible: control.title !== ""
            width: parent.width - 48
            text: control.title
            role: "headlineSmall"
            wrapMode: Text.Wrap
            horizontalAlignment: control.iconName !== "" ? Text.AlignHCenter : Text.AlignLeft
        }
    }

    // The actions (standardButtons or buttons given by the dialog), hidden when there are none.
    footer: DialogButtonBox {
        visible: count > 0
    }

    background: Rectangle {
        radius: Theme.shape.extraLarge
        color: Theme.color.surfaceContainerHigh
    }

    T.Overlay.modal: Rectangle {
        color: Theme.alpha(Theme.color.scrim, 0.32)
    }
}
