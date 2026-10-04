// Material 3 navigation rail (left library tabs). model: list of { text, iconName }. `compact` (dense desktop panels)
// packs the destinations closer; when they do not fit in the height they scroll.
import QtQuick
import Vedit.Theme

FocusScope {
    id: root

    property var model: []
    property int currentIndex: 0
    property bool compact: false
    property color color: Theme.color.surface
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
        color: root.color
    }

    Flickable {
        anchors.fill: parent
        contentHeight: column.implicitHeight
        interactive: contentHeight > height
        boundsBehavior: Flickable.StopAtBounds
        clip: interactive

        Column {
            id: column
            width: parent.width
            topPadding: root.compact ? Theme.space.sm : Theme.space.md
            bottomPadding: topPadding
            spacing: root.compact ? Theme.space.xxs : Theme.space.md

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
                    compact: root.compact
                    selected: index === root.currentIndex
                    focusIndicator: root.activeFocus && selected
                    onClicked: { root.currentIndex = index; root.forceActiveFocus(); root.activated(index) }
                }
            }
        }
    }
}
