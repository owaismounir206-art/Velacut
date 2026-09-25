// One clip on the timeline: its frames (or waveform), name and selection; drag to move (to another track, or to the
// free space above/below for a new track), drag an edge to trim. The result is committed on release, as one command.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import Vedit.Components
import Vedit.Theme
import Vedit.UI

Item {
    id: clip

    required property Item view
    required property string clipId
    required property int trackRow
    required property int start
    required property int duration
    required property string name
    required property string kind
    required property string mediaId
    required property int sourceIn
    required property bool selected
    required property bool locked
    required property int mediaLength
    required property var markers // [{id, frame (from the clip's start), name, color}]

    // Gesture in progress: "", "move", "trimStart", "trimEnd".
    property string mode: ""
    property int moveFrame: start
    property int moveRow: trackRow
    property int editStart: start
    property int editEnd: start + duration
    readonly property int shownStart: mode === "move" ? moveFrame : mode === "" ? start : editStart
    readonly property int shownDuration: mode === "trimStart" || mode === "trimEnd" ? editEnd - editStart : duration
    readonly property int shownRow: mode === "move" ? moveRow : trackRow

    objectName: "clip-" + clipId
    x: shownStart * view.zoom
    y: view.rowTop(shownRow) + (shownRow < 0 || shownRow >= view.rows
                                ? (view.rowHeight(shownRow) - view.rowHeight(trackRow)) / 2 : 0)
    width: Math.max(Theme.editor.hairline, shownDuration * view.zoom)
    height: view.rowHeight(trackRow)
    z: mode !== "" ? 10 : selected ? 2 : 1
    opacity: mode === "move" ? 1 - Theme.state.dragged : 1

    Accessible.role: Accessible.Button
    Accessible.name: qsTr("Clip %1").arg(name)
    Accessible.selected: selected

    Rectangle {
        id: body
        anchors.fill: parent
        radius: Theme.shape.small
        color: clip.kind === "audio" ? Theme.color.tertiaryContainer
             : clip.kind === "text" ? Theme.color.primaryContainer : Theme.color.secondaryContainer
        clip: true

        MediaThumbnail {
            anchors.fill: parent
            visible: clip.kind === "video" || clip.kind === "image"
            editor: clip.view.editor
            mediaId: clip.mediaId
            tiled: true
            sourceIn: clip.sourceIn + (clip.mode === "trimStart" ? clip.editStart - clip.start : 0)
            sourceDuration: clip.shownDuration
        }
        WaveformView {
            anchors.fill: parent
            anchors.margins: Theme.space.xs
            visible: clip.kind === "audio"
            editor: clip.view.editor
            mediaId: clip.mediaId
            sourceIn: clip.sourceIn + (clip.mode === "trimStart" ? clip.editStart - clip.start : 0)
            sourceDuration: clip.shownDuration
            color: Theme.color.onTertiaryContainer
        }
        Rectangle {
            x: Theme.space.xs
            y: Theme.space.xs
            visible: clip.width > Theme.space.xxxl
            width: Math.min(nameLabel.implicitWidth + 2 * Theme.space.xs, parent.width - 2 * Theme.space.xs)
            height: nameLabel.implicitHeight
            radius: Theme.shape.extraSmall
            color: Theme.color.inverseSurface
            Label {
                id: nameLabel
                anchors.fill: parent
                anchors.leftMargin: Theme.space.xs
                anchors.rightMargin: Theme.space.xs
                role: "labelSmall"
                elide: Text.ElideRight
                color: Theme.color.inverseOnSurface
                text: clip.name
            }
        }
        // Keyframes of the selected clip: diamonds at the bottom; a click moves the playhead there.
        Repeater {
            model: clip.selected && clip.view.editor.inspector.clipId === clip.clipId ? clip.view.editor.inspector.keyframes : []
            delegate: Icon {
                id: diamond
                required property int modelData
                objectName: "clipKeyframe_" + modelData
                x: modelData * clip.view.zoom - width / 2
                y: parent.height - height
                name: "diamond"
                filled: true
                size: Theme.editor.handleSize
                color: Theme.color.primary
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: clip.view.player.seek(clip.start + diamond.modelData)
                }
            }
        }
        // Markers of the clip: a tick at the top; a click moves the playhead there.
        Repeater {
            model: clip.markers
            delegate: Rectangle {
                id: tick
                required property var modelData
                objectName: "clipMarker_" + modelData.id
                x: modelData.frame * clip.view.zoom - width / 2
                width: Theme.editor.playheadWidth
                height: parent.height / 3
                color: clip.view.markerColor(modelData.color)
                MouseArea {
                    anchors.fill: parent
                    anchors.margins: -Theme.space.xs
                    cursorShape: Qt.PointingHandCursor
                    ToolTip.visible: containsMouse && tick.modelData.name !== ""
                    ToolTip.text: tick.modelData.name
                    hoverEnabled: true
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    onClicked: (mouse) => {
                        if (mouse.button === Qt.RightButton)
                            clip.view.editor.removeClipMarker(clip.clipId, tick.modelData.id)
                        else
                            clip.view.player.seek(clip.start + tick.modelData.frame)
                    }
                }
            }
        }
        Rectangle {
            anchors.fill: parent
            radius: parent.radius
            color: "transparent"
            border.width: clip.selected ? Theme.editor.selectionBorder : Theme.editor.hairline
            border.color: clip.selected ? Theme.color.primary : Theme.color.outlineVariant
        }
    }

    // Right click: the same actions as the contextual toolbar (SPEC 0bis rule 3).
    TapHandler {
        acceptedButtons: Qt.RightButton
        enabled: !clip.locked
        onTapped: {
            if (!clip.selected)
                clip.view.editor.select(clip.clipId, false)
            contextMenu.popup()
        }
    }
    Menu {
        id: contextMenu
        objectName: "clipMenu"
        Repeater {
            model: clip.view.editor.actions.toolbar
            delegate: MenuItem {
                required property var modelData
                iconName: modelData.icon
                text: modelData.text
                shortcutText: modelData.shortcut
                enabled: modelData.enabled
                onTriggered: clip.view.editor.actions.trigger(modelData.id)
            }
        }
        MenuSeparator {}
        // Grouping (Phase 3): shown when it applies.
        Repeater {
            model: [{ id: "createCompound", icon: "stacks", text: qsTr("Group into a compound clip") },
                    { id: "expandCompound", icon: "stacks", text: qsTr("Ungroup the compound clip") }]
            delegate: MenuItem {
                required property var modelData
                objectName: modelData.id + "Item"
                // Re-evaluated with the toolbar (same signals); `clip` is null while a removed clip is destroyed.
                readonly property bool applies: !!clip && !!clip.view.editor.actions.toolbar
                                                && clip.view.editor.actions.isEnabled(modelData.id)
                visible: applies
                height: applies ? implicitHeight : 0
                iconName: modelData.icon
                text: modelData.text
                onTriggered: clip.view.editor.actions.trigger(modelData.id)
            }
        }
        Repeater {
            model: ["copyAttributes", "pasteAttributes"]
            delegate: MenuItem {
                required property string modelData
                objectName: modelData + "Item"
                iconName: modelData === "copyAttributes" ? "format_paint" : "content_paste"
                text: modelData === "copyAttributes" ? qsTr("Copy attributes") : qsTr("Paste attributes")
                shortcutText: modelData === "copyAttributes" ? qsTr("Ctrl+Alt+C") : qsTr("Ctrl+Alt+V")
                enabled: modelData === "copyAttributes" || clip.view.editor.inspector.canPaste
                onTriggered: clip.view.editor.actions.trigger(modelData)
            }
        }
    }

    // Body: click selects (Ctrl/Shift adds), drag moves.
    MouseArea {
        id: mover
        anchors.fill: parent
        anchors.leftMargin: Theme.editor.trimHandleWidth
        anchors.rightMargin: Theme.editor.trimHandleWidth
        enabled: !clip.locked
        cursorShape: clip.mode === "move" ? Qt.ClosedHandCursor : Qt.OpenHandCursor
        property real pressX
        property real pressY
        onPressed: (mouse) => {
            const point = mapToItem(clip.parent, mouse.x, mouse.y)
            pressX = point.x
            pressY = point.y
            clip.view.player.pause()
        }
        onPositionChanged: (mouse) => {
            const point = mapToItem(clip.parent, mouse.x, mouse.y)
            if (clip.mode === "" && Math.abs(point.x - pressX) + Math.abs(point.y - pressY) > Theme.editor.dragStartDistance) {
                clip.mode = "move"
                if (!clip.selected)
                    clip.view.editor.select(clip.clipId, false)
            }
            if (clip.mode !== "move")
                return
            const raw = Math.max(0, clip.start + Math.round((point.x - pressX) / clip.view.zoom))
            clip.moveFrame = clip.view.editor.snapRange(raw, clip.duration, [clip.clipId], clip.view.snapFrames)
            // The guide goes on the edge that snapped: the start if snapping the start alone gives this position.
            clip.view.snapGuide = clip.moveFrame === raw ? -1
                                : clip.view.editor.snap(raw, [clip.clipId], clip.view.snapFrames) === clip.moveFrame
                                  ? clip.moveFrame : clip.moveFrame + clip.duration
            clip.moveRow = clip.view.rowAt(point.y)
        }
        onReleased: (mouse) => {
            if (clip.mode === "move") {
                const frame = clip.moveFrame
                const row = clip.moveRow
                clip.mode = ""
                clip.view.snapGuide = -1
                if (frame !== clip.start || row !== clip.trackRow)
                    clip.view.editor.moveClip(clip.clipId, frame, row)
            } else {
                clip.view.editor.select(clip.clipId, (mouse.modifiers & (Qt.ControlModifier | Qt.ShiftModifier)) !== 0)
            }
        }
        onCanceled: { clip.mode = ""; clip.view.snapGuide = -1 }
    }

    // Edges: drag to trim; the preview shows the frame at the new cut.
    component TrimHandle: MouseArea {
        id: handle
        required property bool startEdge
        width: Theme.editor.trimHandleWidth
        height: parent.height
        enabled: !clip.locked
        hoverEnabled: true
        cursorShape: Qt.SizeHorCursor
        property real pressX
        onPressed: (mouse) => {
            pressX = mapToItem(clip.parent, mouse.x, 0).x
            clip.editStart = clip.start
            clip.editEnd = clip.start + clip.duration
            clip.mode = startEdge ? "trimStart" : "trimEnd"
            clip.view.player.pause()
            if (!clip.selected)
                clip.view.editor.select(clip.clipId, false)
        }
        onPositionChanged: (mouse) => {
            if (!pressed)
                return
            const delta = Math.round((mapToItem(clip.parent, mouse.x, 0).x - pressX) / clip.view.zoom)
            if (startEdge) {
                // Not before the start of the material, and at least one frame long.
                const lowest = clip.mediaLength >= 0 ? Math.max(0, clip.start - clip.sourceIn) : 0
                const raw = Math.max(lowest, Math.min(clip.start + delta, clip.editEnd - 1))
                const snapped = clip.view.editor.snap(raw, [clip.clipId], clip.view.snapFrames)
                clip.editStart = Math.max(lowest, Math.min(snapped, clip.editEnd - 1))
                clip.view.snapGuide = snapped !== raw ? snapped : -1
                clip.view.player.skim(clip.editStart)
            } else {
                const highest = clip.mediaLength >= 0 ? clip.start + clip.mediaLength - clip.sourceIn : Number.MAX_SAFE_INTEGER
                const raw = Math.min(highest, Math.max(clip.start + clip.duration + delta, clip.editStart + 1))
                const snapped = clip.view.editor.snap(raw, [clip.clipId], clip.view.snapFrames)
                clip.editEnd = Math.min(highest, Math.max(snapped, clip.editStart + 1))
                clip.view.snapGuide = snapped !== raw ? snapped : -1
                clip.view.player.skim(clip.editEnd - 1)
            }
        }
        onReleased: {
            const frame = startEdge ? clip.editStart : clip.editEnd
            const changed = startEdge ? frame !== clip.start : frame !== clip.start + clip.duration
            clip.mode = ""
            clip.view.snapGuide = -1
            clip.view.player.endSkim()
            if (changed)
                clip.view.editor.trimClip(clip.clipId, startEdge, frame)
        }
        onCanceled: { clip.mode = ""; clip.view.snapGuide = -1; clip.view.player.endSkim() }

        Rectangle {
            anchors.fill: parent
            anchors.margins: Theme.space.xxs
            radius: Theme.shape.extraSmall
            visible: handle.containsMouse || handle.pressed || clip.selected
            color: handle.pressed ? Theme.color.primary : Theme.color.primaryContainer
        }
    }
    TrimHandle { objectName: "trimStart"; startEdge: true; anchors.left: parent.left }
    TrimHandle { objectName: "trimEnd"; startEdge: false; anchors.right: parent.right }
}
