// Material 3 navigation rail (left library tabs). model: list of { text, iconName }.
import QtQuick
import Vedit.Theme

FocusScope {
    id: root

    property var model: []
    property int currentIndex: 0
    // Optional item above the destinations (e.g. a FAB or a menu button).
    property alias header: headerSlot.data
    signal activated(int index)

    implicitWidth: 80
    implicitHeight: column.implicitHeight
    activeFocusOnTab: true

    Keys.onUpPressed: if (currentIndex > 0) { currentIndex--; activated(currentIndex) }
    Keys.onDownPressed: if (currentIndex < model.length - 1) { currentIndex++; activated(currentIndex) }

    Accessible.role: Accessible.PageTabList

    Rectangle {
        anchors.fill: parent
        color: Theme.color.surface
    }

    Column {
        id: column
        width: parent.width
        topPadding: 12
        spacing: 12

        Item {
            id: headerSlot
            width: parent.width
            height: childrenRect.height
        }

        Repeater {
            model: root.model
            delegate: NavigationRailItem {
                required property var modelData
                required property int index
                width: root.width
                text: modelData.text
                iconName: modelData.iconName
                selected: index === root.currentIndex
                focusIndicator: root.activeFocus && selected
                onClicked: { root.currentIndex = index; root.forceActiveFocus(); root.activated(index) }
            }
        }
    }
}
