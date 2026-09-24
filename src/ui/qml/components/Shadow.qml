// Elevation shadow for levels 1–5 drawn with a few translucent rounded rectangles (no shaders). M3 renders
// elevation mainly with tonal surface colors; this soft shadow is only for components that require one,
// and it is hidden in software rendering mode (SPEC §4).
import QtQuick
import Vedit.Theme

Item {
    id: root
    property int level: 1
    property real radius: 0

    anchors.fill: parent
    visible: level > 0 && !Theme.softwareRendering
    z: -1

    Repeater {
        model: Math.min(root.level, 3)
        Rectangle {
            required property int index
            x: 0
            y: (index + 1) * (root.level >= 3 ? 1.5 : 1)
            width: root.width
            height: root.height
            radius: root.radius
            color: Theme.alpha(Theme.color.shadow, 0.10 - index * 0.025)
        }
    }
}
