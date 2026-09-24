import QtQuick
import QtQuick.Templates as T
import Vedit.Components
import Vedit.Theme

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
        color: Theme.color.surfaceContainer
        Shadow { level: 2; radius: parent.radius }
    }
}
