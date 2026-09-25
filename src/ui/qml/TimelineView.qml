// The timeline (D-16): ruler, tracks (overlays above the magnetic main track, audio below), clips, playhead.
// Pointing shows the frame under the pointer (skimming); clicking moves the playhead; clips are dragged, trimmed by
// their edges and snap to edges and to the playhead; media and files can be dropped anywhere.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Vedit.Components
import Vedit.Theme
import Vedit.UI

Rectangle {
    id: view

    required property Editor editor
    signal importRequested()

    readonly property TimelineModel model: editor.timeline
    readonly property TimelinePlayer player: editor.player
    readonly property var tracks: model.tracks
    readonly property int rows: tracks.length
    property real zoom: Theme.editor.zoomDefault // pixels per frame
    readonly property int snapFrames: Math.max(1, Math.round(Theme.editor.snapThreshold / zoom))
    // Frame where a drag snapped (a guide line is drawn there), -1 when none.
    property int snapGuide: -1

    color: Theme.color.surfaceContainerLow
    clip: true

    // ---- geometry ----------------------------------------------------------------------------------------------
    function rowHeight(row) {
        if (row < 0 || row >= rows)
            return Theme.editor.newTrackZone
        const kind = tracks[row].kind
        return kind === "main" ? Theme.editor.mainTrackHeight
             : kind === "audio" ? Theme.editor.audioTrackHeight : Theme.editor.overlayTrackHeight
    }
    function rowTop(row) {
        let y = Theme.editor.newTrackZone
        for (let i = 0; i < Math.min(row, rows); ++i)
            y += rowHeight(i) + Theme.editor.trackGap
        return row < 0 ? 0 : y
    }
    // -1: the free space above the tracks (a new track), rows: the space below them.
    function rowAt(y) {
        if (y < Theme.editor.newTrackZone)
            return -1
        for (let i = 0; i < rows; ++i) {
            if (y < rowTop(i) + rowHeight(i) + Theme.editor.trackGap / 2)
                return i
        }
        return rows
    }
    function frameAt(x) { return Math.max(0, Math.round(x / zoom)) }

    // ---- zoom -----------------------------------------------------------------------------------------------------
    function setZoom(value, anchorX) {
        const clamped = Math.min(Theme.editor.zoomMaximum, Math.max(Theme.editor.zoomMinimum, value))
        const pointer = anchorX === undefined ? player.position * zoom - flick.contentX : anchorX
        const frame = (flick.contentX + pointer) / zoom
        zoom = clamped
        flick.contentX = Math.max(0, frame * zoom - pointer)
    }
    function zoomBy(factor, anchorX) { setZoom(zoom * factor, anchorX) }
    function zoomToFit() {
        if (model.duration > 0) {
            setZoom(flick.width * (1 - Theme.editor.tailRatio / 4) / model.duration)
            flick.contentX = 0
        }
    }

    // Keep the playhead in view while playing.
    Connections {
        target: view.player
        function onPositionChanged() {
            if (!view.player.playing)
                return
            const x = view.player.position * view.zoom
            if (x > flick.contentX + flick.width * 0.9 || x < flick.contentX)
                flick.contentX = Math.max(0, x - flick.width * 0.1)
        }
    }

    // ---- ruler ----------------------------------------------------------------------------------------------------
    Item {
        id: ruler
        x: Theme.editor.trackHeaderWidth
        width: parent.width - x
        height: Theme.editor.rulerHeight
        clip: true

        readonly property int fps: Math.max(1, Math.round(view.editor.frameRate))
        // The smallest step (in frames) whose labels do not overlap.
        readonly property int step: {
            const steps = [1, 2, 5, 10, 15, fps, 2 * fps, 5 * fps, 10 * fps, 15 * fps, 30 * fps, 60 * fps,
                           120 * fps, 300 * fps, 600 * fps, 1800 * fps, 3600 * fps]
            for (const candidate of steps) {
                if (candidate * view.zoom >= Theme.editor.rulerLabelSpacing)
                    return candidate
            }
            return steps[steps.length - 1]
        }
        function label(frame) {
            if (step % fps !== 0)
                return view.player.timecode(frame)
            const seconds = Math.round(frame / fps)
            return Math.floor(seconds / 60) + ":" + String(seconds % 60).padStart(2, "0")
        }

        Repeater {
            model: Math.ceil(ruler.width / (ruler.step * view.zoom)) + 2
            delegate: Item {
                required property int index
                readonly property int frame: (Math.floor(flick.contentX / (ruler.step * view.zoom)) + index) * ruler.step
                x: frame * view.zoom - flick.contentX
                height: ruler.height
                Rectangle {
                    width: Theme.editor.hairline
                    height: parent.height / 3
                    anchors.bottom: parent.bottom
                    color: Theme.color.outline
                }
                Label {
                    x: Theme.space.xs
                    anchors.verticalCenter: parent.verticalCenter
                    role: "labelSmall"
                    font.features: { "tnum": 1 }
                    color: Theme.color.onSurfaceVariant
                    text: ruler.label(parent.frame)
                }
            }
        }
        // Click or drag on the ruler: move the playhead (scrubbing).
        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.SizeHorCursor
            onPressed: (mouse) => { view.player.pause(); view.player.seek(view.frameAt(mouse.x + flick.contentX)) }
            onPositionChanged: (mouse) => { if (pressed) view.player.seek(view.frameAt(mouse.x + flick.contentX)) }
        }
    }

    // ---- track headers ----------------------------------------------------------------------------------------------
    Item {
        id: headers
        y: Theme.editor.rulerHeight
        width: Theme.editor.trackHeaderWidth
        height: parent.height - y
        clip: true
        Repeater {
            model: view.tracks
            delegate: Item {
                required property var modelData
                required property int index
                y: view.rowTop(index) - flick.contentY
                width: headers.width
                height: view.rowHeight(index)
                Icon {
                    anchors.centerIn: parent
                    name: parent.modelData.kind === "audio" ? "music_note"
                        : parent.modelData.kind === "main" ? "movie" : "picture_in_picture"
                    color: parent.modelData.kind === "main" ? Theme.color.primary : Theme.color.onSurfaceVariant
                    Accessible.name: parent.modelData.kind === "audio" ? qsTr("Audio track")
                                   : parent.modelData.kind === "main" ? qsTr("Main track") : qsTr("Overlay track")
                }
            }
        }
    }

    // ---- tracks and clips -----------------------------------------------------------------------------------------
    Flickable {
        id: flick
        x: Theme.editor.trackHeaderWidth
        y: Theme.editor.rulerHeight
        width: parent.width - x
        height: parent.height - y
        contentWidth: Math.max(width, view.model.duration * view.zoom + width * Theme.editor.tailRatio)
        contentHeight: Math.max(height, view.rowTop(view.rows) + Theme.editor.newTrackZone)
        boundsBehavior: Flickable.StopAtBounds
        interactive: false // dragging is for clips; scrolling with the wheel and the scroll bars
        ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded }
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

        // Only the clips in view (plus a margin) have delegates.
        function updateVisibleRange() {
            view.model.setVisibleRange(Math.floor(contentX / view.zoom), Math.ceil((contentX + width) / view.zoom))
        }
        onContentXChanged: Qt.callLater(updateVisibleRange)
        onWidthChanged: Qt.callLater(updateVisibleRange)
        Connections {
            target: view
            function onZoomChanged() { Qt.callLater(flick.updateVisibleRange) }
        }

        WheelHandler {
            // Ctrl+wheel: zoom around the pointer; wheel: scroll in time (and Shift: between tracks).
            acceptedModifiers: Qt.ControlModifier
            onWheel: (event) => view.zoomBy(event.angleDelta.y > 0 ? Theme.editor.zoomStep : 1 / Theme.editor.zoomStep,
                                           point.position.x - flick.contentX)
        }
        WheelHandler {
            acceptedModifiers: Qt.NoModifier
            onWheel: (event) => {
                const delta = event.angleDelta.x !== 0 ? event.angleDelta.x : event.angleDelta.y
                flick.contentX = Math.max(0, Math.min(flick.contentWidth - flick.width, flick.contentX - delta))
            }
        }
        WheelHandler {
            acceptedModifiers: Qt.ShiftModifier
            onWheel: (event) => {
                flick.contentY = Math.max(0, Math.min(flick.contentHeight - flick.height, flick.contentY - event.angleDelta.y))
            }
        }

        Item {
            id: canvas
            objectName: "timelineCanvas"
            width: flick.contentWidth
            height: flick.contentHeight

            // Background: clicking empty space moves the playhead and clears the selection; pointing skims.
            MouseArea {
                anchors.fill: parent
                hoverEnabled: true
                onPressed: (mouse) => {
                    view.editor.clearSelection()
                    view.player.pause()
                    view.player.seek(view.frameAt(mouse.x))
                }
                onPositionChanged: (mouse) => {
                    if (pressed)
                        view.player.seek(view.frameAt(mouse.x))
                }
            }
            HoverHandler {
                id: skimmer
                onPointChanged: if (hovered) view.player.skim(view.frameAt(point.position.x))
                onHoveredChanged: if (!hovered) view.player.endSkim()
            }

            Repeater {
                model: view.tracks
                delegate: Rectangle {
                    required property var modelData
                    required property int index
                    y: view.rowTop(index)
                    width: canvas.width
                    height: view.rowHeight(index)
                    radius: Theme.shape.small
                    color: modelData.kind === "main" ? Theme.color.surfaceContainerHigh : Theme.color.surfaceContainer
                }
            }

            // Empty project: the main track says what to do.
            Rectangle {
                visible: view.model.duration === 0
                x: Theme.space.sm
                y: view.rowTop(view.model.mainRow)
                width: Math.min(flick.width, Theme.editor.dialogWidth) - 2 * Theme.space.sm
                height: view.rowHeight(view.model.mainRow)
                radius: Theme.shape.small
                color: "transparent"
                border.width: Theme.editor.selectionBorder
                border.color: Theme.color.outlineVariant
                RowLayout {
                    anchors.centerIn: parent
                    spacing: Theme.space.md
                    Icon { name: "add_photo_alternate"; color: Theme.color.onSurfaceVariant }
                    Label {
                        role: "bodyMedium"
                        color: Theme.color.onSurfaceVariant
                        text: qsTr("Drop videos and photos here")
                    }
                    Button {
                        variant: "text"
                        text: qsTr("Import")
                        onClicked: view.importRequested()
                    }
                }
            }

            Repeater {
                model: view.model
                delegate: TimelineClip {
                    view: view
                }
            }

            // Cuts between touching clips (SPEC §5.11bis): a "+" to add a transition, or the transition, as wide as the
            // time it covers; selected, its edges change the duration.
            Repeater {
                model: view.model.cuts
                delegate: Item {
                    id: cut
                    required property var modelData
                    readonly property bool hasTransition: modelData.transitionId !== ""
                    readonly property bool selected: hasTransition ? view.editor.selectedTransition === modelData.transitionId
                                                                   : view.editor.selectedCut === modelData.fromClip
                    property int dragDuration: -1 // frames, while an edge is dragged
                    readonly property int duration: dragDuration >= 0 ? dragDuration : modelData.duration

                    objectName: "cut-" + modelData.fromClip
                    z: 5 // above the clips (even a selected one), below a clip being dragged
                    width: hasTransition ? Math.max(Theme.editor.transitionMark, duration * view.zoom) : Theme.editor.transitionMark
                    height: Theme.editor.transitionMark
                    x: modelData.frame * view.zoom - width / 2
                    y: view.rowTop(modelData.trackRow) + (view.rowHeight(modelData.trackRow) - height) / 2
                    Accessible.role: Accessible.Button
                    Accessible.name: hasTransition ? qsTr("Transition %1").arg(modelData.name) : qsTr("Add a transition")

                    Rectangle {
                        anchors.fill: parent
                        radius: Theme.shape.full
                        color: cut.selected ? Theme.color.primary
                             : cut.hasTransition ? Theme.color.secondaryContainer : Theme.color.surfaceContainerHighest
                        border.width: Theme.editor.hairline
                        border.color: Theme.color.outline
                        opacity: cut.hasTransition || cut.selected || markMouse.containsMouse ? 1 : Theme.state.disabledContent
                    }
                    Icon {
                        anchors.centerIn: parent
                        name: cut.hasTransition ? "transition_fade" : "add"
                        size: Theme.editor.transitionMark - Theme.space.sm
                        color: cut.selected ? Theme.color.onPrimary
                             : cut.hasTransition ? Theme.color.onSecondaryContainer : Theme.color.onSurfaceVariant
                    }
                    MouseArea {
                        id: markMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        ToolTip.visible: containsMouse
                        ToolTip.text: cut.hasTransition ? cut.modelData.name : qsTr("Add a transition")
                        onClicked: {
                            if (cut.hasTransition) {
                                view.editor.selectTransition(cut.modelData.transitionId)
                                view.editor.libraryRequested("transitions")
                            } else {
                                view.editor.selectCut(cut.modelData.fromClip)
                            }
                        }
                    }
                    // Edges of the selected transition: the window stays centred on the cut, so both sides move.
                    Repeater {
                        model: cut.hasTransition && cut.selected ? [-1, 1] : []
                        delegate: MouseArea {
                            required property int modelData
                            property real startX: 0
                            property int startDuration: 0
                            objectName: modelData < 0 ? "transitionStart" : "transitionEnd"
                            width: Theme.editor.trimHandleWidth
                            height: cut.height
                            x: modelData < 0 ? -width / 2 : cut.width - width / 2
                            cursorShape: Qt.SizeHorCursor
                            onPressed: (mouse) => {
                                startX = mapToItem(canvas, mouse.x, 0).x
                                startDuration = cut.modelData.duration
                            }
                            onPositionChanged: (mouse) => {
                                if (!pressed)
                                    return
                                const delta = (mapToItem(canvas, mouse.x, 0).x - startX) * modelData * 2 / view.zoom
                                cut.dragDuration = Math.max(1, Math.round(startDuration + delta))
                                view.editor.inspector.set("transition.duration", cut.dragDuration / view.editor.frameRate)
                            }
                            onReleased: {
                                view.editor.inspector.endGesture()
                                cut.dragDuration = -1
                            }
                        }
                    }
                }
            }

            // Where a dropped media item or file will go.
            Rectangle {
                id: dropMarker
                property int frame: 0
                property int row: 0
                visible: drop.containsDrag
                x: frame * view.zoom
                y: view.rowTop(row)
                width: Theme.editor.playheadWidth
                height: view.rowHeight(row)
                color: Theme.color.primary
            }

            // Snap guide: the edge a dragged clip snapped to.
            Rectangle {
                visible: view.snapGuide >= 0
                x: view.snapGuide * view.zoom
                width: Theme.editor.hairline
                height: canvas.height
                color: Theme.color.tertiary
            }

            // Skimming line (the frame shown in the preview while pointing).
            Rectangle {
                visible: view.player.skimming
                x: view.player.shownPosition * view.zoom
                width: Theme.editor.hairline
                height: canvas.height
                color: Theme.color.onSurfaceVariant
            }

            DropArea {
                id: drop
                anchors.fill: parent
                onEntered: (event) => event.accept()
                onPositionChanged: (event) => {
                    dropMarker.frame = view.editor.snap(view.frameAt(event.x), [], view.snapFrames)
                    dropMarker.row = view.rowAt(event.y)
                }
                onDropped: (event) => {
                    const frame = view.editor.snap(view.frameAt(event.x), [], view.snapFrames)
                    const row = view.rowAt(event.y)
                    if (event.hasUrls) {
                        view.editor.importAndInsert(event.urls, frame, row)
                        event.acceptProposedAction()
                    } else if (event.source && event.source.mediaId !== undefined) {
                        view.editor.insertMedia(event.source.mediaId, frame, row)
                        event.accept()
                    }
                }
            }
        }
    }

    // ---- playhead (over the ruler and the tracks) ---------------------------------------------------------------------
    Item {
        x: Theme.editor.trackHeaderWidth + view.player.position * view.zoom - flick.contentX
        visible: x >= Theme.editor.trackHeaderWidth && x <= view.width
        height: view.height
        Rectangle {
            x: -width / 2
            width: Theme.editor.playheadWidth
            height: parent.height
            color: Theme.color.onSurface
        }
        Rectangle {
            x: -width / 2
            y: Theme.editor.rulerHeight - height
            width: Theme.editor.playheadKnob
            height: Theme.editor.playheadKnob
            radius: width / 2
            color: Theme.color.onSurface
        }
    }
}
