// Handles of the selected clip on the preview (SPEC §4, §5.4): drag to move (it snaps to the centre lines), a corner to
// resize, the round handle to rotate; a double click on a text edits it in place (SPEC §5.7). Each gesture is one undo
// step. For a text the corners change the font size, so it stays sharp.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import Vedit.Components
import Vedit.Theme
import Vedit.UI

Item {
    id: root

    required property Editor editor
    readonly property Inspector inspector: editor.inspector
    readonly property TimelinePlayer player: editor.player
    readonly property var box: inspector.canvasBox
    readonly property size canvas: editor.canvasSize
    readonly property bool isText: inspector.kind === Inspector.Text
    // The frame as drawn by the preview: the canvas fitted in this item.
    readonly property real factor: canvas.width > 0 && canvas.height > 0
                                   ? Math.min(width / canvas.width, height / canvas.height) : 1
    readonly property point origin: Qt.point((width - canvas.width * factor) / 2, (height - canvas.height * factor) / 2)
    readonly property int currentFrame: player.skimming ? player.shownPosition : player.position
    readonly property bool shown: box.width !== undefined && !player.playing
                                  && currentFrame >= box.start && currentFrame < box.end
    // "" = the clip's handles, "mask" = the mask's, "pick" = a click picks the colour to remove (chroma key).
    readonly property string mode: inspector.canvasMode
    readonly property var mask: inspector.maskBox
    property bool editing: false
    // Guides while a move snapped to the centre.
    property bool snappedX: false
    property bool snappedY: false

    function toCanvas(item, x, y) {
        const p = item.mapToItem(root, x, y)
        return Qt.point((p.x - origin.x) / factor, (p.y - origin.y) / factor)
    }
    function finishEditing() {
        if (editing) {
            editing = false
            inspector.endGesture()
        }
    }

    // Centre guides.
    Rectangle {
        visible: root.snappedX
        x: root.origin.x + root.canvas.width * root.factor / 2
        y: root.origin.y
        width: Theme.editor.hairline
        height: root.canvas.height * root.factor
        color: Theme.color.tertiary
    }
    Rectangle {
        visible: root.snappedY
        x: root.origin.x
        y: root.origin.y + root.canvas.height * root.factor / 2
        width: root.canvas.width * root.factor
        height: Theme.editor.hairline
        color: Theme.color.tertiary
    }

    // The mask of the clip: drag inside to move it, the corner to resize it (around its centre).
    Item {
        id: maskFrame
        objectName: "maskBox"
        visible: root.shown && root.mode === "mask" && root.mask.width !== undefined
        width: (root.mask.width ?? 0) * root.factor
        height: (root.mask.height ?? 0) * root.factor
        x: root.origin.x + (root.mask.x ?? 0) * root.factor - width / 2
        y: root.origin.y + (root.mask.y ?? 0) * root.factor - height / 2
        rotation: root.mask.rotation ?? 0

        // A circle shows as its ellipse (a circle scaled to the box); the other shapes as their box.
        Rectangle {
            visible: root.mask.shape !== 2
            anchors.fill: parent
            color: "transparent"
            border.width: Theme.editor.selectionBorder
            border.color: Theme.color.tertiary
        }
        Rectangle {
            id: ellipse
            readonly property real side: Math.max(1, Math.max(maskFrame.width, maskFrame.height))
            visible: root.mask.shape === 2
            width: side
            height: side
            radius: side / 2
            color: "transparent"
            border.width: Theme.editor.selectionBorder * side / Math.max(1, Math.min(maskFrame.width, maskFrame.height))
            border.color: Theme.color.tertiary
            transform: Scale { xScale: maskFrame.width / ellipse.side; yScale: maskFrame.height / ellipse.side }
        }
        MouseArea {
            objectName: "maskMove"
            anchors.fill: parent
            cursorShape: Qt.SizeAllCursor
            property point start
            property point startCentre
            onPressed: (mouse) => {
                root.player.pause()
                start = root.toCanvas(this, mouse.x, mouse.y)
                startCentre = Qt.point(root.mask.x, root.mask.y)
            }
            onPositionChanged: (mouse) => {
                if (!pressed)
                    return
                const p = root.toCanvas(this, mouse.x, mouse.y)
                root.inspector.setMaskGeometry(startCentre.x + p.x - start.x, startCentre.y + p.y - start.y,
                                               root.mask.width, root.mask.height)
            }
            onReleased: root.inspector.endGesture()
        }
        Rectangle {
            objectName: "maskCorner"
            width: Theme.editor.handleSize
            height: Theme.editor.handleSize
            radius: width / 2
            x: parent.width - width / 2
            y: parent.height - height / 2
            color: Theme.color.surface
            border.width: Theme.editor.selectionBorder
            border.color: Theme.color.tertiary
            MouseArea {
                anchors.fill: parent
                anchors.margins: -Theme.space.xs
                cursorShape: Qt.SizeFDiagCursor
                onPressed: root.player.pause()
                onPositionChanged: (mouse) => {
                    if (!pressed)
                        return
                    // The pointer in the mask's own (unrotated) axes, from its centre: half the new size.
                    const p = root.toCanvas(this, mouse.x, mouse.y)
                    const angle = -(root.mask.rotation ?? 0) * Math.PI / 180
                    const dx = p.x - root.mask.x
                    const dy = p.y - root.mask.y
                    const localX = dx * Math.cos(angle) - dy * Math.sin(angle)
                    const localY = dx * Math.sin(angle) + dy * Math.cos(angle)
                    root.inspector.setMaskGeometry(root.mask.x, root.mask.y, Math.max(2, 2 * Math.abs(localX)),
                                                   Math.max(2, 2 * Math.abs(localY)))
                }
                onReleased: root.inspector.endGesture()
            }
        }
    }

    // Picking the colour to remove: the next click on the player.
    MouseArea {
        objectName: "pickArea"
        anchors.fill: parent
        visible: root.mode === "pick"
        z: 1
        cursorShape: Qt.CrossCursor
        onClicked: (mouse) => {
            const p = Qt.point((mouse.x - root.origin.x) / root.factor, (mouse.y - root.origin.y) / root.factor)
            root.inspector.pickKeyColor(p.x, p.y)
        }
        Label {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.top
            anchors.topMargin: Theme.space.sm
            padding: Theme.space.sm
            role: "labelLarge"
            color: Theme.color.inverseOnSurface
            text: qsTr("Click the colour to remove (Esc: cancel)")
            background: Rectangle {
                radius: Theme.shape.full
                color: Theme.color.inverseSurface
            }
        }
    }
    Shortcut {
        sequence: "Escape"
        enabled: root.mode === "pick"
        onActivated: root.inspector.canvasMode = ""
    }

    Item {
        id: frame
        objectName: "canvasBox"
        visible: root.shown && root.mode === ""
        width: (root.box.width ?? 0) * root.factor
        height: (root.box.height ?? 0) * root.factor
        x: root.origin.x + (root.box.x ?? 0) * root.factor - width / 2
        y: root.origin.y + (root.box.y ?? 0) * root.factor - height / 2
        rotation: root.box.rotation ?? 0

        Rectangle {
            anchors.fill: parent
            color: "transparent"
            border.width: Theme.editor.selectionBorder
            border.color: Theme.color.primary
        }

        // Move (and double click on a text: edit it).
        MouseArea {
            id: body
            objectName: "canvasMove"
            anchors.fill: parent
            cursorShape: Qt.SizeAllCursor
            property point start
            property real startX: 0
            property real startY: 0
            onPressed: (mouse) => {
                root.player.pause()
                start = root.toCanvas(body, mouse.x, mouse.y)
                startX = root.inspector.values["x"] ?? 0
                startY = root.inspector.values["y"] ?? 0
            }
            onPositionChanged: (mouse) => {
                if (!pressed)
                    return
                const p = root.toCanvas(body, mouse.x, mouse.y)
                let x = startX + (p.x - start.x) / root.canvas.width
                let y = startY + (p.y - start.y) / root.canvas.height
                // The clip's centre on the centre lines of the canvas, within a few screen pixels.
                const thresholdX = Theme.editor.snapThreshold / (root.factor * root.canvas.width)
                const thresholdY = Theme.editor.snapThreshold / (root.factor * root.canvas.height)
                const centreX = x + ((root.box.x ?? 0) / root.canvas.width - 0.5 - (root.inspector.values["x"] ?? 0))
                const centreY = y + ((root.box.y ?? 0) / root.canvas.height - 0.5 - (root.inspector.values["y"] ?? 0))
                root.snappedX = Math.abs(centreX) < thresholdX
                root.snappedY = Math.abs(centreY) < thresholdY
                if (root.snappedX)
                    x -= centreX
                if (root.snappedY)
                    y -= centreY
                root.inspector.set("x", x)
                root.inspector.set("y", y)
            }
            onReleased: {
                root.snappedX = false
                root.snappedY = false
                root.inspector.endGesture()
            }
            onDoubleClicked: {
                if (root.isText) {
                    textEditor.text = root.inspector.values["text.content"] ?? ""
                    root.editing = true
                    textEditor.forceActiveFocus()
                    textEditor.selectAll()
                }
            }
        }

        // Resize from the corners, around the centre.
        Repeater {
            model: [Qt.point(0, 0), Qt.point(1, 0), Qt.point(0, 1), Qt.point(1, 1)]
            delegate: Rectangle {
                id: corner
                required property point modelData
                required property int index
                objectName: "canvasCorner" + index
                width: Theme.editor.handleSize
                height: Theme.editor.handleSize
                radius: width / 2
                x: modelData.x * frame.width - width / 2
                y: modelData.y * frame.height - height / 2
                color: Theme.color.surface
                border.width: Theme.editor.selectionBorder
                border.color: Theme.color.primary

                MouseArea {
                    anchors.fill: parent
                    anchors.margins: -Theme.space.xs
                    cursorShape: (corner.index === 0 || corner.index === 3) ? Qt.SizeFDiagCursor : Qt.SizeBDiagCursor
                    property real startDistance: 1
                    property real startValue: 1
                    function distance(mouse) {
                        const p = root.toCanvas(this, mouse.x, mouse.y)
                        return Math.max(1, Math.hypot(p.x - root.box.x, p.y - root.box.y))
                    }
                    onPressed: (mouse) => {
                        root.player.pause()
                        startDistance = distance(mouse)
                        startValue = root.isText ? (root.inspector.values["text.size"] ?? 0.07) : (root.inspector.values["scale"] ?? 1)
                    }
                    onPositionChanged: (mouse) => {
                        if (!pressed)
                            return
                        const value = startValue * distance(mouse) / startDistance
                        root.inspector.set(root.isText ? "text.size" : "scale", value)
                    }
                    onReleased: root.inspector.endGesture()
                }
            }
        }

        // Rotate: the angle of the pointer around the centre; it sticks to right angles.
        Rectangle {
            objectName: "canvasRotate"
            width: Theme.editor.handleSize * 2
            height: width
            radius: width / 2
            x: (frame.width - width) / 2
            y: -Theme.editor.rotateHandleDistance - height / 2
            color: Theme.color.primary
            Icon {
                anchors.centerIn: parent
                name: "rotate_right"
                size: parent.width - Theme.space.xs
                color: Theme.color.onPrimary
            }
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onPressed: root.player.pause()
                onPositionChanged: (mouse) => {
                    if (!pressed)
                        return
                    const p = root.toCanvas(this, mouse.x, mouse.y)
                    let angle = Math.atan2(p.y - root.box.y, p.x - root.box.x) * 180 / Math.PI + 90
                    angle = ((angle + 180) % 360 + 360) % 360 - 180
                    const right = Math.round(angle / 90) * 90
                    if (Math.abs(angle - right) < 4)
                        angle = right
                    root.inspector.set("rotation", angle)
                }
                onReleased: root.inspector.endGesture()
            }
        }
    }

    // Editing a text in place: over the text, as wide as it (at least a comfortable width).
    TextArea {
        id: textEditor
        objectName: "canvasTextEditor"
        visible: root.editing && root.shown
        width: Math.max(frame.width, Theme.editor.propertiesWidth)
        x: Math.max(0, Math.min(root.width - width, frame.x + (frame.width - width) / 2))
        y: Math.max(0, Math.min(root.height - height, frame.y + frame.height + Theme.space.sm))
        label: qsTr("Text — Esc to finish")
        onTextChanged: if (root.editing && activeFocus && text !== root.inspector.values["text.content"]) root.inspector.set("text.content", text)
        onActiveFocusChanged: if (!activeFocus) root.finishEditing()
        Keys.onEscapePressed: { root.finishEditing(); root.forceActiveFocus() }
    }
    Connections {
        target: root.inspector
        function onChanged() {
            if (!root.isText)
                root.editing = false
        }
    }
}
