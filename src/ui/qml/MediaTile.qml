// One item of the media pool: its picture (skimming while pointed at), duration, "+" and drag to the timeline.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Vedit.Components
import Vedit.Theme
import Vedit.UI

Item {
    id: tile

    required property Editor editor
    required property string mediaId
    required property string name
    required property string kind
    required property string durationText
    required property bool used

    height: Theme.editor.mediaTileHeight + Theme.space.xl
    Accessible.role: Accessible.ListItem
    Accessible.name: name

    Rectangle {
        id: picture
        width: parent.width
        height: Theme.editor.mediaTileHeight
        radius: Theme.shape.small
        color: tile.kind === "audio" ? Theme.color.tertiaryContainer : Theme.color.surfaceContainerHighest
        clip: true

        MediaThumbnail {
            anchors.fill: parent
            visible: tile.kind !== "audio"
            editor: tile.editor
            mediaId: tile.mediaId
            // Skimming: the frame under the pointer.
            position: hover.hovered ? hover.point.position.x / width : 0
        }
        Icon {
            anchors.centerIn: parent
            visible: tile.kind === "audio"
            name: "music_note"
            size: Theme.space.xxl
            color: Theme.color.onTertiaryContainer
        }
        Rectangle {
            anchors { right: parent.right; bottom: parent.bottom; margins: Theme.space.xs }
            visible: tile.durationText !== "" && !hover.hovered
            width: durationLabel.implicitWidth + 2 * Theme.space.xs
            height: durationLabel.implicitHeight
            radius: Theme.shape.extraSmall
            color: Theme.color.inverseSurface
            Label {
                id: durationLabel
                anchors.centerIn: parent
                role: "labelSmall"
                font.features: { "tnum": 1 }
                color: Theme.color.inverseOnSurface
                text: tile.durationText
            }
        }
        Icon {
            anchors { left: parent.left; top: parent.top; margins: Theme.space.xs }
            visible: tile.used
            name: "check_circle"
            filled: true
            size: Theme.space.lg + Theme.space.xs
            color: Theme.color.primary
            Accessible.name: qsTr("On the timeline")
        }
        // "+": the fastest way to use a media item (SPEC 0bis rule 4).
        IconButton {
            anchors { right: parent.right; bottom: parent.bottom; margins: Theme.space.xs }
            visible: hover.hovered
            variant: "filled"
            iconName: "add"
            label: qsTr("Add to the timeline at the playhead")
            onClicked: tile.editor.addMedia(tile.mediaId)
        }

        HoverHandler { id: hover }
        DragHandler {
            id: dragHandler
            target: null
            dragThreshold: Theme.editor.dragStartDistance
            onActiveChanged: {
                if (active) {
                    // Above everything in the window (not in Overlay.overlay: it is invisible without a popup, and an
                    // invisible item delivers no drag events).
                    ghost.parent = tile.Window.window.contentItem
                    ghost.visible = true
                    ghost.Drag.active = true
                } else {
                    ghost.Drag.drop()
                    ghost.Drag.active = false
                    ghost.visible = false
                    ghost.parent = picture
                }
            }
            onCentroidChanged: {
                if (active) {
                    ghost.altHeld = (centroid.modifiers & Qt.AltModifier) !== 0
                    const point = picture.mapToItem(ghost.parent, centroid.position.x, centroid.position.y)
                    ghost.x = point.x - ghost.width / 2
                    ghost.y = point.y - ghost.height / 2
                }
            }
        }
    }

    Label {
        anchors { left: parent.left; right: parent.right; top: picture.bottom; topMargin: Theme.space.xs }
        role: "bodySmall"
        elide: Text.ElideMiddle
        text: tile.name
    }

    // What follows the pointer while dragging onto the timeline.
    Rectangle {
        id: ghost
        readonly property string mediaId: tile.mediaId
        // Alt held while dragging: the media replaces the clip it is dropped on (SPEC §5.2).
        property bool altHeld: false
        visible: false
        z: Theme.elevation.level5
        width: picture.width / 2
        height: picture.height / 2
        radius: Theme.shape.small
        color: Theme.color.primaryContainer
        border.width: Theme.editor.selectionBorder
        border.color: Theme.color.primary
        Drag.keys: ["vedit/media"]
        Drag.hotSpot.x: width / 2
        Drag.hotSpot.y: height / 2
        Icon {
            anchors.centerIn: parent
            name: tile.kind === "audio" ? "music_note" : tile.kind === "image" ? "image" : "movie"
            color: Theme.color.onPrimaryContainer
        }
    }
}
