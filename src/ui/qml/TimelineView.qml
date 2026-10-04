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
    // The visible part of the tracks, in timeline pixels.
    readonly property real viewportLeft: flick.contentX
    readonly property real viewportRight: flick.contentX + flick.width
    readonly property int snapFrames: Math.max(1, Math.round(Theme.editor.snapThreshold / zoom))
    // Frame where a drag snapped (a guide line is drawn there), -1 when none.
    property int snapGuide: -1

    // Decoupled 60/120 FPS playhead tracking & reactive scrubbing
    property bool isScrubbing: false
    property int visualPlayheadFrame: player.position

    Timer {
        id: scrubDebounceTimer
        interval: 60 // 50-80ms debounce for frame-accurate commit
        repeat: false
        onTriggered: {
            if (view.isScrubbing) {
                view.player.commitSeek(view.visualPlayheadFrame)
            }
        }
    }

    function clampFrame(frame) {
        // The player clamps every seek to [0, duration-1]: clamp the visual playhead too, or the
        // cursor and the playhead disagree while dragging beyond the end of the timeline.
        return Math.max(0, Math.min(frame, view.player.duration - 1))
    }

    function startScrubbing(targetFrame) {
        const target = clampFrame(targetFrame)
        view.player.pause()
        view.isScrubbing = true
        view.visualPlayheadFrame = target
        view.player.scrubSeek(target)
        scrubDebounceTimer.restart()
    }

    function updateScrubbing(targetFrame) {
        const target = clampFrame(targetFrame)
        if (!view.isScrubbing) {
            view.isScrubbing = true
            view.player.pause()
        }
        view.visualPlayheadFrame = target
        view.player.scrubSeek(target)
        scrubDebounceTimer.restart()
    }

    function finishScrubbing() {
        if (view.isScrubbing) {
            scrubDebounceTimer.stop()
            view.player.commitSeek(view.visualPlayheadFrame)
            view.isScrubbing = false
        }
    }

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
    // A marker's colour: a role of the theme ("primary", "tertiary"…) or "#RRGGBBAA" (docs/FILE_FORMAT.md §5.8).
    function markerColor(value) {
        if (value.length === 9 && value[0] === "#")
            return "#" + value.substr(7, 2) + value.substr(1, 6)
        return Theme.color[value] ?? Theme.color.primary
    }

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

    // Keep the playhead in view while playing and sync visual playhead when not scrubbing.
    Connections {
        target: view.player
        function onPositionChanged() {
            if (!view.isScrubbing)
                view.visualPlayheadFrame = view.player.position
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
        // In/Out range highlight on ruler
        Rectangle {
            visible: view.editor.hasInOut
            readonly property real inX: view.editor.inPoint >= 0 ? view.editor.inPoint * view.zoom - flick.contentX : 0
            readonly property real outX: view.editor.outPoint >= 0 ? view.editor.outPoint * view.zoom - flick.contentX : ruler.width
            x: Math.max(0, Math.min(inX, outX))
            width: Math.max(0, Math.abs(outX - inX))
            height: ruler.height
            color: Theme.alpha(Theme.color.primary, 0.22)
            border.width: Theme.editor.hairline
            border.color: Theme.color.primary
        }
        // In point bracket marker
        Rectangle {
            visible: view.editor.inPoint >= 0
            x: view.editor.inPoint * view.zoom - flick.contentX
            width: 2
            height: ruler.height
            color: Theme.color.primary
            Rectangle {
                width: 6
                height: 4
                color: Theme.color.primary
            }
        }
        // Out point bracket marker
        Rectangle {
            visible: view.editor.outPoint >= 0
            x: view.editor.outPoint * view.zoom - flick.contentX - 2
            width: 2
            height: ruler.height
            color: Theme.color.primary
            Rectangle {
                anchors.right: parent.right
                width: 6
                height: 4
                color: Theme.color.primary
            }
        }

        // Click or drag on the ruler: move the playhead (scrubbing).
        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.SizeHorCursor
            onPressed: (mouse) => view.startScrubbing(view.frameAt(mouse.x + flick.contentX))
            onPositionChanged: (mouse) => { if (pressed) view.updateScrubbing(view.frameAt(mouse.x + flick.contentX)) }
            onReleased: view.finishScrubbing()
            onCanceled: view.finishScrubbing()
        }
        // Markers of the video (M adds one at the playhead): click = go there, right click = remove.
        Repeater {
            model: view.model.markers
            delegate: Rectangle {
                id: flag
                required property var modelData
                objectName: "marker_" + modelData.id
                x: modelData.frame * view.zoom - flick.contentX - width / 2
                y: ruler.height - height
                width: Theme.editor.playheadKnob
                height: Theme.editor.playheadKnob
                radius: Theme.shape.extraSmall
                rotation: 45
                color: view.markerColor(modelData.color)
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    hoverEnabled: true
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    ToolTip.visible: containsMouse && flag.modelData.name !== ""
                    ToolTip.text: flag.modelData.name
                    onClicked: (mouse) => {
                        if (mouse.button === Qt.RightButton)
                            view.editor.removeSequenceMarker(flag.modelData.id)
                        else
                            view.player.seek(flag.modelData.frame)
                    }
                }
            }
        }
    }

    // ---- track headers: kind, level meter, a click opens the track's volume (mixer, SPEC §5.9) ------------------------
    Item {
        id: headers
        y: Theme.editor.rulerHeight
        width: Theme.editor.trackHeaderWidth
        height: parent.height - y
        clip: true
        Repeater {
            model: view.tracks
            delegate: Item {
                id: header
                required property var modelData
                required property int index
                objectName: "trackHeader_" + index
                y: view.rowTop(index) - flick.contentY
                width: headers.width
                height: view.rowHeight(index)

                Rectangle {
                    anchors.fill: parent
                    color: header.modelData.locked ? Theme.alpha(Theme.color.errorContainer, 0.25)
                         : header.modelData.kind === "main" ? Theme.color.surfaceContainerHigh : Theme.color.surfaceContainerLow
                    border.width: Theme.editor.hairline
                    border.color: header.modelData.locked ? Theme.color.error : Theme.color.outlineVariant
                }

                Icon {
                    anchors.centerIn: parent
                    name: header.modelData.locked ? "lock"
                        : header.modelData.muted ? "volume_off"
                        : header.modelData.hidden ? "visibility_off"
                        : header.modelData.kind === "audio" ? "music_note"
                        : header.modelData.kind === "main" ? "movie" : "picture_in_picture"
                    color: header.modelData.locked ? Theme.color.error
                         : header.modelData.muted ? Theme.color.error
                         : header.modelData.kind === "main" ? Theme.color.primary : Theme.color.onSurfaceVariant
                    Accessible.name: header.modelData.kind === "audio" ? qsTr("Audio track")
                                   : header.modelData.kind === "main" ? qsTr("Main track") : qsTr("Overlay track")
                }

                LevelMeter {
                    anchors.right: parent.right
                    anchors.rightMargin: Theme.space.xxs
                    anchors.verticalCenter: parent.verticalCenter
                    height: parent.height - Theme.space.sm
                    player: view.player
                    key: header.modelData.trackId
                }

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    ToolTip.visible: containsMouse
                    ToolTip.text: qsTr("Track settings")
                    hoverEnabled: true
                    onClicked: {
                        trackMixer.track = header.modelData
                        trackMixer.y = Math.min(header.y + headers.y, view.height - trackMixer.height)
                        trackMixer.open()
                    }
                }
            }
        }
    }

    // The volume and settings of one track.
    Popup {
        id: trackMixer
        objectName: "trackMixer"
        property var track: ({})
        x: Theme.editor.trackHeaderWidth
        padding: Theme.space.md
        width: Theme.editor.propertiesWidth
        background: Rectangle {
            radius: Theme.shape.medium
            color: Theme.color.surfaceContainerHigh
        }
        ColumnLayout {
            anchors.fill: parent
            spacing: Theme.space.sm
            RowLayout {
                Layout.fillWidth: true
                Label {
                    Layout.fillWidth: true
                    role: "titleSmall"
                    text: trackMixer.track.kind === "audio" ? qsTr("Audio track")
                        : trackMixer.track.kind === "main" ? qsTr("Main track") : qsTr("Overlay track")
                }
                Label {
                    role: "labelLarge"
                    font.features: { "tnum": 1 }
                    color: Theme.color.onSurfaceVariant
                    text: qsTr("%1 dB").arg((volume.value > 0 ? "+" : "") + volume.value.toLocaleString(Qt.locale(), "f", 1))
                }
            }
            Slider {
                id: volume
                objectName: "trackVolume"
                Layout.fillWidth: true
                from: -60
                to: 12
                stepSize: 0.5
                value: trackMixer.track.gainDb ?? 0
                valueText: (value > 0 ? "+" : "") + value.toLocaleString(Qt.locale(), "f", 1)
                Accessible.name: qsTr("Track volume")
                onMoved: view.editor.setTrackVolume(trackMixer.track.trackId, value)
                onPressedChanged: if (!pressed) view.editor.endTrackGesture()
            }
            RowLayout {
                Layout.fillWidth: true
                Label { Layout.fillWidth: true; role: "bodyMedium"; text: qsTr("Mute") }
                Switch {
                    objectName: "trackMute"
                    checked: trackMixer.track.muted ?? false
                    Accessible.name: qsTr("Mute")
                    onToggled: view.editor.setTrackMuted(trackMixer.track.trackId, checked)
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Label { Layout.fillWidth: true; role: "bodyMedium"; text: qsTr("Lock track") }
                Switch {
                    objectName: "trackLock"
                    checked: trackMixer.track.locked ?? false
                    Accessible.name: qsTr("Lock track")
                    onToggled: view.editor.setTrackLocked(trackMixer.track.trackId, checked)
                }
            }
            RowLayout {
                visible: !trackMixer.track.audio
                Layout.fillWidth: true
                Label { Layout.fillWidth: true; role: "bodyMedium"; text: qsTr("Hide track") }
                Switch {
                    objectName: "trackHide"
                    checked: trackMixer.track.hidden ?? false
                    Accessible.name: qsTr("Hide track")
                    onToggled: view.editor.setTrackHidden(trackMixer.track.trackId, checked)
                }
            }
            RowLayout {
                visible: trackMixer.track.kind === "main"
                Layout.fillWidth: true
                Button {
                    Layout.fillWidth: true
                    variant: "tonal"
                    iconName: "image"
                    text: qsTr("Set project cover from current frame")
                    onClicked: {
                        view.editor.setCoverFromCurrentFrame()
                        trackMixer.close()
                    }
                }
            }
        }
        // The track's values follow undo and the other edits while open.
        Connections {
            target: view
            function onTracksChanged() {
                for (const track of view.tracks) {
                    if (track.trackId === trackMixer.track.trackId)
                        trackMixer.track = track
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
                    view.startScrubbing(view.frameAt(mouse.x))
                }
                onPositionChanged: (mouse) => {
                    if (pressed)
                        view.updateScrubbing(view.frameAt(mouse.x))
                }
                onReleased: view.finishScrubbing()
                onCanceled: view.finishScrubbing()
            }
            HoverHandler {
                id: skimmer
                // Never skim while a scrub drag owns the playhead: double seeks (skim + scrub per
                // pointer move) built an async backlog in the MLT consumer and desynced the preview.
                enabled: !view.isScrubbing
                onPointChanged: if (hovered && !view.isScrubbing) view.player.skim(view.frameAt(point.position.x))
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
                x: view.snapGuide * view.zoom - 1
                width: 2
                height: canvas.height
                color: Theme.color.tertiary
                z: 8
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
        id: playheadItem
        x: Theme.editor.trackHeaderWidth + (view.isScrubbing ? view.visualPlayheadFrame : view.player.position) * view.zoom - flick.contentX
        visible: x >= Theme.editor.trackHeaderWidth && x <= view.width
        height: view.height
        z: 15

        // Floating timecode badge while scrubbing
        Rectangle {
            id: timecodeBadge
            visible: view.isScrubbing
            anchors.bottom: knob.top
            anchors.bottomMargin: 4
            anchors.horizontalCenter: parent.horizontalCenter
            width: timecodeLabel.implicitWidth + 12
            height: 20
            radius: Theme.shape.extraSmall
            color: Theme.color.inverseSurface
            Label {
                id: timecodeLabel
                anchors.centerIn: parent
                role: "labelSmall"
                font.features: { "tnum": 1 }
                font.bold: true
                color: Theme.color.inverseOnSurface
                text: view.player.timecode(view.visualPlayheadFrame)
            }
        }

        // Contrast halo for visibility on any background
        Rectangle {
            x: -width / 2
            width: Theme.editor.playheadWidth + 2
            height: parent.height
            color: Theme.alpha(Theme.color.scrim, 0.45)
        }

        // Main playhead vertical line
        Rectangle {
            x: -width / 2
            width: Theme.editor.playheadWidth
            height: parent.height
            color: Theme.color.primary
        }

        // Playhead head / knob
        Item {
            id: knob
            x: -width / 2
            y: Theme.editor.rulerHeight - height
            width: Theme.editor.playheadKnob + 2
            height: Theme.editor.rulerHeight - 2
            Rectangle {
                anchors.fill: parent
                radius: Theme.shape.extraSmall
                color: Theme.color.primary
                border.width: 1
                border.color: Theme.color.onPrimary
            }
            Rectangle {
                anchors.centerIn: parent
                width: 4
                height: 4
                radius: 2
                color: Theme.color.onPrimary
            }
        }

        MouseArea {
            x: -Theme.editor.playheadKnob
            y: 0
            width: Theme.editor.playheadKnob * 2
            height: Theme.editor.rulerHeight
            cursorShape: Qt.SizeHorCursor
            onPressed: (mouse) => {
                view.startScrubbing(view.frameAt(playheadItem.x - Theme.editor.trackHeaderWidth + flick.contentX + mouse.x - Theme.editor.playheadKnob))
            }
            onPositionChanged: (mouse) => {
                if (pressed) {
                    const sceneX = mapToItem(ruler, mouse.x, 0).x
                    view.updateScrubbing(view.frameAt(sceneX + flick.contentX))
                }
            }
            onReleased: view.finishScrubbing()
            onCanceled: view.finishScrubbing()
        }
    }
}
