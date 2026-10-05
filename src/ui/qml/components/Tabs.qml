// Material 3 secondary tabs: model = list of { text }, currentIndex (set by the owner); the active tab is underlined.
// The tabs share the width equally, each at least as wide as its label: when they do not fit they scroll sideways
// (M3 scrollable tabs) and the active one is kept in view; the side with more tabs fades out. Labels are never cut.
import QtQuick
import Vedit.Theme

FocusScope {
    id: root

    property var model: []
    property int currentIndex: 0
    // The colour under the tabs, for the fades at the scrolling sides.
    property color fadeColor: Theme.color.panel
    signal activated(int index)

    implicitHeight: Theme.space.control(Theme.space.xxxl)
    activeFocusOnTab: true
    Accessible.role: Accessible.PageTabList

    // The owner decides the page shown: it binds currentIndex and changes it on activated() (a click never breaks that
    // binding, so the page can also change from elsewhere, e.g. a toolbar button).
    function select(index) {
        if (index >= 0 && index < model.length)
            activated(index)
    }
    Keys.onLeftPressed: select(currentIndex - 1)
    Keys.onRightPressed: select(currentIndex + 1)

    Divider {
        anchors.bottom: parent.bottom
        width: parent.width
    }
    Flickable {
        id: flick
        anchors.fill: parent
        contentWidth: row.width
        interactive: contentWidth > width
        flickableDirection: Flickable.HorizontalFlick
        boundsBehavior: Flickable.StopAtBounds
        clip: true
        WheelHandler {
            enabled: flick.interactive
            onWheel: (event) => {
                const delta = event.angleDelta.y !== 0 ? event.angleDelta.y : event.angleDelta.x
                flick.contentX = Math.max(0, Math.min(flick.contentWidth - flick.width, flick.contentX - delta))
            }
        }
    Row {
        id: row
        height: parent.height
        Repeater {
            id: tabs
            model: root.model
            delegate: Item {
                id: tab
                required property var modelData
                required property int index
                readonly property bool selected: index === root.currentIndex

                objectName: "tab_" + index
                width: Math.max(root.width / Math.max(1, root.model.length), label.implicitWidth + 2 * Theme.space.sm + Theme.space.xs)
                height: root.height
                Accessible.role: Accessible.PageTab
                Accessible.name: modelData.text ?? ""
                Accessible.selected: selected

                TypeText {
                    id: label
                    anchors.fill: parent
                    anchors.leftMargin: Theme.space.xs
                    anchors.rightMargin: Theme.space.xs
                    horizontalAlignment: Text.AlignHCenter
                    elide: Text.ElideRight
                    text: tab.modelData.text ?? ""
                    role: "titleSmall"
                    color: tab.selected ? Theme.color.onSurface : Theme.color.onSurfaceVariant
                }
                Rectangle {
                    visible: tab.selected
                    anchors.bottom: parent.bottom
                    width: parent.width
                    height: Theme.editor.tabIndicator
                    color: Theme.color.primary
                }
                StateLayer {
                    color: Theme.color.onSurface
                    hovered: mouse.containsMouse
                    pressed: mouse.pressed
                    focused: root.activeFocus && tab.selected
                    pressPoint: Qt.point(mouse.mouseX, mouse.mouseY)
                }
                MouseArea {
                    id: mouse
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: { root.forceActiveFocus(); root.select(tab.index) }
                }
            }
        }
    }
    }

    // More tabs on a side: the tabs fade out there (it takes no clicks: a tab half hidden is still clicked, and then
    // scrolled into view).
    component Scroller: Rectangle {
        id: scroller
        property bool atStart: true
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Theme.editor.tabIndicator
        width: Theme.space.xxl
        visible: flick.interactive && (atStart ? flick.contentX > 1 : flick.contentX + flick.width < flick.contentWidth - 1)
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0.0; color: scroller.atStart ? root.fadeColor : Theme.alpha(root.fadeColor, 0) }
            GradientStop { position: 1.0; color: scroller.atStart ? Theme.alpha(root.fadeColor, 0) : root.fadeColor }
        }
    }
    Scroller { anchors.left: parent.left; atStart: true }
    Scroller { anchors.right: parent.right; atStart: false }

    // The active tab stays in view.
    function reveal() {
        const tab = tabs.itemAt(currentIndex)
        if (!tab || !flick.interactive)
            return
        if (tab.x < flick.contentX)
            flick.contentX = tab.x
        else if (tab.x + tab.width > flick.contentX + flick.width)
            flick.contentX = tab.x + tab.width - flick.width
    }
    onCurrentIndexChanged: Qt.callLater(reveal)
    onWidthChanged: Qt.callLater(reveal)
}
