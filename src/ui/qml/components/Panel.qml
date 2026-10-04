// A surface of the editor: a rounded panel standing on the darker backdrop of the window, the way the panels of a
// desktop video editor (library, player, properties, timeline) read as separate cards. Tonal, no shadow (M3: the
// elevation is the surface colour), so it looks the same on every rendering backend.
import QtQuick
import Vedit.Theme

Rectangle {
    id: root

    default property alias content: area.data

    radius: Theme.shape.small
    color: Theme.color.panel

    Item {
        id: area
        anchors.fill: parent
    }
}
