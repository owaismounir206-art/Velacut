// Material 3 small top app bar: leading slot, title (editable project name elsewhere), trailing actions.
import QtQuick
import QtQuick.Layouts
import Vedit.Theme

Rectangle {
    id: root

    property string title
    property alias leading: leadingSlot.data
    property alias trailing: trailingSlot.data
    // Tonal elevation when content scrolls under the bar.
    property bool scrolled: false

    implicitHeight: 64
    color: scrolled ? Theme.color.surfaceContainer : Theme.color.surface
    Behavior on color { ColorAnimation { duration: Theme.motion.short4 } }

    Accessible.role: Accessible.ToolBar

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 4
        anchors.rightMargin: 12
        spacing: 4
        Row {
            id: leadingSlot
            Layout.alignment: Qt.AlignVCenter
        }
        TypeText {
            Layout.fillWidth: true
            Layout.leftMargin: 12
            text: root.title
            role: "titleLarge"
            elide: Text.ElideRight
        }
        Row {
            id: trailingSlot
            spacing: 8
            Layout.alignment: Qt.AlignVCenter
        }
    }
}
