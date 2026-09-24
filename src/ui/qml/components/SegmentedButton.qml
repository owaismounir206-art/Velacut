// Material 3 segmented button for exclusive choices (e.g. 16:9 / 9:16 / 1:1).
// model: list of { text, iconName? }; currentIndex is the selected segment.
import QtQuick
import Vedit.Theme

FocusScope {
    id: root

    property var model: []
    property int currentIndex: 0
    signal activated(int index)

    implicitHeight: Theme.space.control(40)
    implicitWidth: row.implicitWidth
    activeFocusOnTab: true

    Accessible.role: Accessible.RadioButton

    Keys.onLeftPressed: if (currentIndex > 0) { currentIndex--; activated(currentIndex) }
    Keys.onRightPressed: if (currentIndex < model.length - 1) { currentIndex++; activated(currentIndex) }

    Rectangle {
        anchors.fill: parent
        radius: Theme.shape.full
        color: "transparent"
        border.color: Theme.color.outline
        border.width: 1
    }

    Row {
        id: row
        anchors.fill: parent
        Repeater {
            model: root.model
            delegate: Item {
                id: segment
                required property var modelData
                required property int index
                readonly property bool selected: index === root.currentIndex
                readonly property bool first: index === 0
                readonly property bool last: index === root.model.length - 1

                width: Math.max(48, content.implicitWidth + 24)
                height: root.height

                // Selected container: rounded only on the outer ends of the group.
                Rectangle {
                    anchors.fill: parent
                    anchors.margins: 1
                    color: segment.selected ? Theme.color.secondaryContainer : "transparent"
                    radius: (segment.first || segment.last) ? height / 2 : 0
                    Rectangle {
                        // squares the inner side of the first/last segment
                        visible: parent.radius > 0 && root.model.length > 1
                        color: parent.color
                        width: parent.width / 2
                        height: parent.height
                        x: segment.first ? parent.width / 2 : 0
                    }
                }
                Divider {
                    vertical: true
                    visible: !segment.last
                    height: parent.height
                    anchors.right: parent.right
                    color: Theme.color.outline
                }
                Row {
                    id: content
                    anchors.centerIn: parent
                    spacing: 8
                    Icon {
                        visible: segment.selected || (segment.modelData.iconName ?? "") !== ""
                        name: segment.selected ? "check" : (segment.modelData.iconName ?? "")
                        size: 18
                        color: segment.selected ? Theme.color.onSecondaryContainer : Theme.color.onSurface
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    TypeText {
                        text: segment.modelData.text ?? ""
                        role: "labelLarge"
                        color: segment.selected ? Theme.color.onSecondaryContainer : Theme.color.onSurface
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
                StateLayer {
                    radius: (segment.first || segment.last) ? height / 2 : 0
                    color: segment.selected ? Theme.color.onSecondaryContainer : Theme.color.onSurface
                    hovered: mouse.containsMouse
                    pressed: mouse.pressed
                    focused: root.activeFocus && segment.selected
                    pressPoint: Qt.point(mouse.mouseX, mouse.mouseY)
                }
                MouseArea {
                    id: mouse
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: { root.currentIndex = segment.index; root.forceActiveFocus(); root.activated(segment.index) }
                }
                Accessible.role: Accessible.RadioButton
                Accessible.name: segment.modelData.text ?? ""
                Accessible.checked: segment.selected
            }
        }
    }
}
