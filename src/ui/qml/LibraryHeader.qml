// The header of a library panel (Media, Audio, Text…): its name on the left, its few actions on the right, compact.
import QtQuick
import QtQuick.Controls
import Velacut.Theme

Item {
    id: header

    property string title
    property bool busy: false
    default property alias actions: actionRow.data

    implicitHeight: Theme.editor.panelHeaderHeight

    Label {
        anchors.left: parent.left
        anchors.right: actionRow.left
        anchors.rightMargin: Theme.space.sm
        anchors.verticalCenter: parent.verticalCenter
        role: "titleSmall"
        elide: Text.ElideRight
        text: header.title
    }
    Row {
        id: actionRow
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        spacing: Theme.space.xs
        BusyIndicator {
            anchors.verticalCenter: parent.verticalCenter
            width: Theme.editor.toolIconSize
            height: Theme.editor.toolIconSize
            running: header.busy
            visible: running
        }
    }
}
