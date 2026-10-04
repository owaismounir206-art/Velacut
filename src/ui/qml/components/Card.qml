// Material 3 card: variant "filled" (default), "elevated", "outlined". Clickable when `interactive`.
import QtQuick
import Vedit.Theme

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
