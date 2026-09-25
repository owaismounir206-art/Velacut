// Material 3 secondary tabs: model = list of { text }, currentIndex (set by the owner); the active tab is underlined.
// The tabs share the width equally (a side panel has room for a few short labels).
import QtQuick
import Vedit.Theme

FocusScope {
    id: root

    property var model: []
    property int currentIndex: 0
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
    Row {
        anchors.fill: parent
        Repeater {
            model: root.model
            delegate: Item {
                id: tab
                required property var modelData
                required property int index
                readonly property bool selected: index === root.currentIndex

                objectName: "tab_" + index
                width: root.width / Math.max(1, root.model.length)
                height: root.height
                Accessible.role: Accessible.PageTab
                Accessible.name: modelData.text ?? ""
                Accessible.selected: selected

                TypeText {
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
