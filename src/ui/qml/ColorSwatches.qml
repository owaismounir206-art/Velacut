// A choice of colour: the common ones as swatches (one click), any other with the colour dialog.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import Velacut.Components
import Velacut.Theme
import Velacut.UI

Flow {
    id: root

    required property Inspector inspector
    property color current
    property string label
    signal picked(color value)

    spacing: Theme.space.xs
    Accessible.name: label

    Repeater {
        model: root.inspector.swatches
        delegate: Rectangle {
            id: swatch
            required property color modelData
            readonly property bool selected: Qt.colorEqual(modelData, Qt.rgba(root.current.r, root.current.g, root.current.b, 1))
            width: Theme.editor.swatchSize
            height: Theme.editor.swatchSize
            radius: Theme.shape.full
            color: modelData
            border.width: selected ? Theme.editor.selectionBorder : Theme.editor.hairline
            border.color: selected ? Theme.color.primary : Theme.color.outline
            Accessible.role: Accessible.Button
            Accessible.name: modelData.toString()
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: root.picked(swatch.modelData)
            }
        }
    }
    IconButton {
        width: Theme.editor.swatchSize + Theme.space.sm
        height: width
        iconName: "palette"
        label: qsTr("Other colour")
        onClicked: dialog.open()
    }
    ColorDialog {
        id: dialog
        title: root.label
        selectedColor: root.current
        onAccepted: root.picked(selectedColor)
    }
}
