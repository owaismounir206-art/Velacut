// Material 3 card: variant "filled" (default), "elevated", "outlined". Clickable when `interactive`.
import QtQuick
import Velacut.Theme

Item {
    id: root

    property string variant: "filled"
    property bool interactive: false
    default property alias content: contentArea.data
    property alias hovered: hover.hovered
    // The surface of the card (the variant's by default).
    property color containerColor: variant === "elevated" ? Theme.color.surfaceContainerLow
                                 : variant === "outlined" ? Theme.color.surface
                                 : Theme.color.surfaceContainerHighest
    signal clicked()

    implicitWidth: 160
    implicitHeight: 120

    Accessible.role: interactive ? Accessible.Button : Accessible.Pane

    // Apple-style hover feedback: a lift (no scale — a grid must not move under the pointer),
    // with the shadow rising a level. Transform only, nothing with "reduce motion".
    transform: Translate {
        y: root.interactive && hover.hovered ? Theme.motion.hoverLift : 0
        Behavior on y {
            enabled: !Theme.motion.reduced
            NumberAnimation { duration: Theme.motion.short3; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.motion.standardDecelerate }
        }
    }

    Rectangle {
        id: surface
        anchors.fill: parent
        radius: Theme.shape.medium
        color: root.containerColor
        border.width: root.variant === "outlined" ? 1 : 0
        border.color: Theme.color.outlineVariant
        Shadow {
            level: root.variant === "elevated" ? (root.interactive && hover.hovered ? 2 : 1) : 0
            radius: parent.radius
        }
    }
    Item {
        id: contentArea
        anchors.fill: parent
        clip: true
    }
    StateLayer {
        radius: surface.radius
        color: Theme.color.onSurface
        visible: root.interactive
        hovered: hover.hovered
        pressed: tap.pressed
        pressPoint: tap.point.position
    }
    HoverHandler { id: hover; enabled: root.interactive }
    TapHandler { id: tap; enabled: root.interactive; onTapped: root.clicked() }
}
