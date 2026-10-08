import QtQuick
import QtQuick.Templates as T
import Velacut.Components
import Velacut.Theme

T.Menu {
    id: control

    implicitWidth: Math.max(112, Math.min(280, contentWidth + leftPadding + rightPadding))
    implicitHeight: contentHeight + topPadding + bottomPadding
    topPadding: 8
    bottomPadding: 8
    margins: 0
    overlap: 1

    enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.motion.short4 } }
    exit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Theme.motion.short2 } }

    delegate: MenuItem { }

    contentItem: ListView {
        implicitHeight: contentHeight
        model: control.contentModel
        interactive: Window.window ? contentHeight + control.topPadding + control.bottomPadding > Window.window.height : false
        clip: true
        currentIndex: control.currentIndex
    }

    background: Rectangle {
        implicitWidth: 112
        radius: Theme.shape.extraSmall
        // Frosted material, "lite" (see Dialog.qml): 92% surface, the hairline is the glass edge.
        color: Theme.alpha(Theme.color.surfaceContainer, 0.92)
        Rectangle {
            anchors { top: parent.top; left: parent.left; right: parent.right; leftMargin: parent.radius; rightMargin: parent.radius }
            height: Theme.editor.hairline
            color: Theme.alpha(Theme.color.outlineVariant, 0.14)
        }
        Shadow { level: 2; radius: parent.radius }
    }
}
