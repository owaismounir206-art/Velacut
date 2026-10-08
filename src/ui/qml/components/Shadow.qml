// Elevation shadow for levels 1–5 drawn with a stack of translucent rounded rectangles (no
// shaders): the stack grows outward with a fading alpha, so the edge is soft (Apple-style diffuse
// shadows), and the deeper layers hang a little further down, like shadows lit from above. M3
// renders elevation mainly with tonal surface colors; this soft shadow is only for components
// that require one, and it is hidden in software rendering mode (SPEC §4). One or two shadowed
// items per view: cheap where it shows at all (never in software mode).
import QtQuick
import Velacut.Theme

Item {
    id: root
    property int level: 1
    property real radius: 0

    anchors.fill: parent
    visible: level > 0 && !Theme.softwareRendering
    z: -1

    // Soft edge: 8 layers, each 1.25 dp wider than the shape, alpha 0.10 -> ~0.01 outward. The
    // innermost layer (index 0) is exactly the shape: hidden behind the surface itself. The
    // layer constants are this drawing primitive's own (like the alphas before it): not a color,
    // size or radius of the components that use it.
    Repeater {
        model: 8
        Rectangle {
            required property int index

            // The layer's outward step, and its hang (deeper layers hang further down).
            readonly property real step: 1.25 * index
            readonly property real hang: 0.2 * (root.level + index)

            x: -step
            y: hang - step
            width: root.width + 2 * step
            height: root.height + 2 * step
            radius: root.radius + step
            color: Theme.alpha(Theme.color.shadow, 0.10 - index * 0.0125)
        }
    }
}
