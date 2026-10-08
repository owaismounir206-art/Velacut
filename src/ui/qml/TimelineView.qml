// The timeline (D-16): ruler, tracks (overlays above the magnetic main track, audio below), clips, playhead.
// Pointing shows the frame under the pointer (skimming); clicking moves the playhead; clips are dragged, trimmed by
// their edges and snap to edges and to the playhead; media and files can be dropped anywhere.
// The view follows the playhead, like CapCut: page-scrolling while playing, centring on a seek, and the timeline
// scrolls under the playhead while it is dragged past an edge of the view.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import Velacut.Components
import Velacut.Theme
import Velacut.UI

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
    // The scrub position in viewport pixels (flick coordinates) while scrubbing. It can be beyond the
    // edges of the view: the timeline then scrolls under it, CapCut-style, for as long as it stays out.
    property real scrubX: 0

    Timer {
        id: scrubDebounceTimer
        interval: 80 // 80 ms debounce: mid-drag only a muted preview lands, never an audio seek
        repeat: false
        onTriggered: {
            if (view.isScrubbing) {
                // Preview only while the drag goes on: scrubSeek is muted at the MLT level and
                // coalesced; the frame-accurate (audio on) commitSeek lands once, on release.
                view.player.scrubSeek(view.visualPlayheadFrame)
            }
        }
    }

    // While the pointer is held past an edge of the view during a scrub drag, the timeline keeps
    // moving under it (like CapCut): the playhead stays pinned at the edge, the content scrolls,
    // faster the further out the pointer is.
    Timer {
        id: scrubAutoScrollTimer
        interval: 16
        repeat: true
        running: view.isScrubbing
        onTriggered: {
            const pastRight = view.scrubX - flick.width
            const pastLeft = -view.scrubX
            if (pastRight > 0) {
                flick.contentX = Math.min(flick.contentWidth - flick.width,
                                          flick.contentX + Math.min(60, Math.max(2, pastRight * 0.2)))
                view.updateScrubbing(view.scrubX)
            } else if (pastLeft > 0) {
                flick.contentX = Math.max(0, flick.contentX - Math.min(60, Math.max(2, pastLeft * 0.2)))
                view.updateScrubbing(view.scrubX)
            }
        }
    }

    function clampFrame(frame) {
        // The player clamps every seek to [0, duration-1]: clamp the visual playhead too, or the
        // cursor and the playhead disagree while dragging beyond the end of the timeline.
        return Math.max(0, Math.min(frame, view.player.duration - 1))
    }

    // The frame of a scrub position in viewport pixels: pinned at the edges of the view, so the
    // playhead stays visible (knob included) while the timeline scrolls under it.
    function scrubFrameAt(x) {
        const pinned = x < 0 ? 0 : x > flick.width ? flick.width - Theme.editor.playheadKnob : x
        return clampFrame(frameAt(pinned + flick.contentX))
    }

    function startScrubbing(x) {
        view.scrubX = x
        const target = scrubFrameAt(x)
        view.isScrubbing = true
        view.visualPlayheadFrame = target
        // Before pause(): it remembers whether the timeline was playing, to resume it on commit.
        view.player.setScrubMuted(true)
        view.player.pause()
        view.player.scrubSeek(target)
        scrubDebounceTimer.restart()
    }

    function updateScrubbing(x) {
        view.scrubX = x
        if (!view.isScrubbing) {
            view.isScrubbing = true
            view.player.setScrubMuted(true)
            view.player.pause()
        }
        const target = scrubFrameAt(x)
        // Same frame as before (a slow drag, or the edge auto-scroll): no MLT seek, no timer.
        if (target === view.visualPlayheadFrame)
            return
        view.visualPlayheadFrame = target
        view.player.scrubSeek(target)
        scrubDebounceTimer.restart()
    }

    function finishScrubbing() {
        if (view.isScrubbing) {
            scrubDebounceTimer.stop()
            // The commit is where the audio comes back (and playback resumes if it was playing).
            view.player.commitSeek(view.visualPlayheadFrame)
            view.isScrubbing = false
        }
    }

    color: "transparent" // the timeline panel's surface
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

    // Keep the playhead in view: page-scrolling while playing, and the view moves to it on a seek
    // (keyboard, preview, markers), like CapCut. During a scrub drag this does nothing: the drag owns
    // the playhead and the edge auto-scroll (scrubAutoScrollTimer).
    Connections {
        target: view.player
        function onPositionChanged() {
            if (!view.isScrubbing)
                view.visualPlayheadFrame = view.player.position
            if (view.isScrubbing)
                return
            const x = view.player.position * view.zoom
            if (view.player.playing) {
                if (x > flick.contentX + flick.width * 0.9 || x < flick.contentX)
                    flick.contentX = Math.max(0, x - flick.width * 0.1)
            } else if (x < flick.contentX || x > flick.contentX + flick.width) {
                // A seek from elsewhere: the timeline scrolls, the playhead centred in the view.
                flick.contentX = Math.max(0, Math.min(flick.contentWidth - flick.width, x - flick.width / 2))
            }
        }
        // A split, a delete or an undo during a scrub drag can shorten the timeline: the visual
        // playhead would point past its end until the next pointer move. Keep it inside.
        function onDurationChanged() {
            if (view.isScrubbing)
                view.visualPlayheadFrame = view.clampFrame(view.visualPlayheadFrame)
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

        // Labelled ticks at every step, small ones in between (a quarter or a fifth of the step).
        readonly property int minorDivisions: {
            if (step < fps)
                return step === 15 ? 3 : step >= 10 ? 5 : step
            const seconds = step / fps
            return seconds <= 2 ? 4 : seconds % 5 === 0 ? 5 : seconds % 3 === 0 ? 3 : 2
        }
        Repeater {
            model: Math.ceil(ruler.width / (ruler.step * view.zoom)) + 2
            delegate: Item {
                id: tick
                required property int index
                readonly property int frame: (Math.floor(flick.contentX / (ruler.step * view.zoom)) + index) * ruler.step
                x: frame * view.zoom - flick.contentX
                height: ruler.height
                Rectangle {
                    width: Theme.editor.hairline
                    height: Theme.editor.rulerMajorTick
                    anchors.bottom: parent.bottom
                    color: Theme.color.outline
                }
                Repeater {
                    model: ruler.minorDivisions - 1
                    delegate: Rectangle {
                        required property int index
                        x: (index + 1) * ruler.step * view.zoom / ruler.minorDivisions
                        width: Theme.editor.hairline
                        y: ruler.height - height
                        height: Theme.editor.rulerMinorTick
                        color: Theme.color.outlineVariant
                    }
                }
                Label {
                    x: Theme.space.xs
                    y: Theme.space.xs
                    role: "labelSmall"
                    font.features: { "tnum": 1 }
                    color: Theme.color.onSurfaceVariant
                    text: ruler.label(tick.frame)
                }
            }
        }
        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: Theme.editor.hairline
            color: Theme.color.outlineVariant
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
        // In and Out points: brackets on the ruler.
        Rectangle {
            visible: view.editor.inPoint >= 0
            x: view.editor.inPoint * view.zoom - flick.contentX
            width: Theme.editor.playheadWidth
            height: ruler.height
            color: Theme.color.primary
            Rectangle {
                width: Theme.space.sm
                height: Theme.editor.playheadWidth * 2
                color: Theme.color.primary
            }
        }
        Rectangle {
            visible: view.editor.outPoint >= 0
            x: view.editor.outPoint * view.zoom - flick.contentX - width
            width: Theme.editor.playheadWidth
            height: ruler.height
            color: Theme.color.primary
            Rectangle {
                anchors.right: parent.right
                width: Theme.space.sm
                height: Theme.editor.playheadWidth * 2
                color: Theme.color.primary
            }
        }

        // Click or drag on the ruler: move the playhead (scrubbing).
        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.SizeHorCursor
            onPressed: (mouse) => view.startScrubbing(mouse.x)
            onPositionChanged: (mouse) => { if (pressed) view.updateScrubbing(mouse.x) }
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

    // ---- track headers (SPEC §5.2, §5.9): lock, hide, mute always one click away, the level of the track, a click on
    // the free part opens its volume; the main track starts with the cover of the video (SPEC §4) ---------------------
    component HeaderButton: IconButton {
        implicitWidth: Theme.editor.toolButtonSize - Theme.space.sm
        implicitHeight: implicitWidth
        iconSize: Theme.editor.smallIconSize
        checkable: true
    }
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
                readonly property bool main: modelData.kind === "main"
                readonly property bool audio: modelData.kind === "audio"
                objectName: "trackHeader_" + index
                y: view.rowTop(index) - flick.contentY
                width: headers.width
                height: view.rowHeight(index)

                // The free part of the header: the track's volume and settings.
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    ToolTip.visible: containsMouse
                    ToolTip.delay: 600
                    ToolTip.text: header.audio ? qsTr("Audio track: volume and settings")
                                : header.main ? qsTr("Main track: volume and settings") : qsTr("Overlay track: volume and settings")
                    hoverEnabled: true
                    onClicked: {
                        trackMixer.track = header.modelData
                        trackMixer.y = Math.min(header.y + headers.y, view.height - trackMixer.height)
                        trackMixer.open()
                    }
                }

                Row {
                    id: switches
                    anchors.left: parent.left
                    anchors.leftMargin: Theme.space.xs
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 0
                    HeaderButton {
                        objectName: "trackLockButton_" + header.index
                        iconName: header.modelData.locked ? "lock" : "lock_open"
                        checked: header.modelData.locked
                        label: header.modelData.locked ? qsTr("Unlock the track") : qsTr("Lock the track")
                        onClicked: view.editor.setTrackLocked(header.modelData.trackId, !header.modelData.locked)
                    }
                    HeaderButton {
                        visible: !header.audio
                        objectName: "trackHideButton_" + header.index
                        iconName: header.modelData.hidden ? "visibility_off" : "visibility"
                        checked: header.modelData.hidden
                        label: header.modelData.hidden ? qsTr("Show the track") : qsTr("Hide the track")
                        onClicked: view.editor.setTrackHidden(header.modelData.trackId, !header.modelData.hidden)
                    }
                    HeaderButton {
                        visible: header.audio || header.main
                        objectName: "trackMuteButton_" + header.index
                        iconName: header.modelData.muted ? "volume_off" : "volume_up"
                        checked: header.modelData.muted
                        label: header.modelData.muted ? qsTr("Turn the sound on") : qsTr("Mute the track")
                        onClicked: view.editor.setTrackMuted(header.modelData.trackId, !header.modelData.muted)
                    }
                }

                // The cover (SPEC §4, §5.13ter): the chosen picture, else an invitation to choose one.
                Rectangle {
                    id: coverTile
                    objectName: "coverButton"
                    visible: header.main
                    anchors.right: meter.left
                    anchors.rightMargin: Theme.space.xs
                    anchors.verticalCenter: parent.verticalCenter
                    width: Theme.editor.coverWidth
                    height: parent.height - Theme.space.sm
                    radius: Theme.shape.extraSmall
                    color: Theme.color.surfaceContainerHighest
                    clip: true
                    Accessible.role: Accessible.Button
                    Accessible.name: qsTr("Cover")
                    Image {
                        anchors.fill: parent
                        source: view.editor.coverUrl
                        fillMode: Image.PreserveAspectCrop
                        asynchronous: true
                        cache: false
                        visible: status === Image.Ready
                    }
                    Rectangle {
                        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                        height: coverLabel.implicitHeight + Theme.space.xxs
                        color: Theme.alpha(Theme.color.scrim, 0.6)
                        Label {
                            id: coverLabel
                            anchors.centerIn: parent
                            role: "labelSmall"
                            color: Theme.readableOn(Theme.color.scrim)
                            text: qsTr("Cover")
                        }
                    }
                    Icon {
                        anchors.horizontalCenter: parent.horizontalCenter
                        y: (parent.height - coverLabel.implicitHeight - height) / 2
                        visible: view.editor.coverUrl.toString() === ""
                        name: "add_photo_alternate"
                        size: Theme.editor.toolIconSize
                        color: Theme.color.onSurfaceVariant
                    }
                    StateLayer {
                        radius: parent.radius
                        color: Theme.color.onSurface
                        hovered: coverMouse.containsMouse
                        pressed: coverMouse.pressed
                        pressPoint: Qt.point(coverMouse.mouseX, coverMouse.mouseY)
                    }
                    MouseArea {
                        id: coverMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        ToolTip.visible: containsMouse
                        ToolTip.delay: 600
                        ToolTip.text: qsTr("Cover of the video: shown on the draft and in the exported file")
                        onClicked: coverMenu.popup()
                    }
                }

                LevelMeter {
                    id: meter
                    anchors.right: parent.right
                    anchors.rightMargin: Theme.space.sm
                    anchors.verticalCenter: parent.verticalCenter
                    height: parent.height - Theme.space.md
                    player: view.player
                    key: header.modelData.trackId
                }
            }
        }
        Rectangle {
            anchors.right: parent.right
            width: Theme.editor.hairline
            height: parent.height
            color: Theme.color.outlineVariant
        }
    }

    // What to do with the cover: a frame of the video, a picture, saving it as a file.
    Menu {
        id: coverMenu
        objectName: "coverMenu"
        MenuItem {
            objectName: "coverFromFrame"
            iconName: "photo_camera"
            text: qsTr("Use the frame on screen")
            enabled: view.model.duration > 0
            onTriggered: view.editor.setCoverFromCurrentFrame()
        }
        MenuItem {
            iconName: "image"
            text: qsTr("Choose a picture…")
            onTriggered: coverDialog.open()
        }
        MenuSeparator {}
        MenuItem {
            iconName: "download"
            text: qsTr("Save the cover (PNG)")
            enabled: view.editor.coverUrl.toString() !== ""
            onTriggered: {
                const path = view.editor.exportCover(App.videosFolder(), false)
                App.message(path !== "" ? qsTr("Cover saved as %1").arg(path) : qsTr("The cover could not be saved."))
            }
        }
        MenuItem {
            iconName: "smart_display"
            text: qsTr("Save for YouTube (1280×720 JPG)")
            enabled: view.editor.coverUrl.toString() !== ""
            onTriggered: {
                const path = view.editor.exportCover(App.videosFolder(), true)
                App.message(path !== "" ? qsTr("Cover saved as %1").arg(path) : qsTr("The cover could not be saved."))
            }
        }
        MenuItem {
            iconName: "delete"
            text: qsTr("Remove the cover")
            enabled: view.editor.coverUrl.toString() !== ""
            onTriggered: view.editor.clearCover()
        }
    }
    FileDialog {
        id: coverDialog
        title: qsTr("Choose a picture for the cover")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("Pictures (%1)").arg("*.png *.jpg *.jpeg *.webp *.bmp"), qsTr("All files (*)")]
        onAccepted: view.editor.setCoverFromImage(selectedFile)
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
                    view.startScrubbing(mouse.x - flick.contentX)
                }
                onPositionChanged: (mouse) => {
                    if (pressed)
                        view.updateScrubbing(mouse.x - flick.contentX)
                }
                onReleased: view.finishScrubbing()
                onCanceled: view.finishScrubbing()
            }
            HoverHandler {
                id: skimmer
                // Never skim while a scrub drag owns the playhead: double seeks (skim + scrub per
                // pointer move) built an async backlog in the MLT consumer and desynced the preview.
                enabled: !view.isScrubbing && view.editor.skimmingEnabled
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
                    radius: Theme.shape.extraSmall
                    color: Theme.color.surfaceContainerLow
                    opacity: modelData.hidden ? Theme.state.disabledContent : 1
                }
            }

            // Empty project: the main track says what to do, across the view.
            Rectangle {
                visible: view.model.duration === 0
                x: flick.contentX + Theme.space.sm
                y: view.rowTop(view.model.mainRow)
                width: flick.width - 2 * Theme.space.sm
                height: view.rowHeight(view.model.mainRow)
                radius: Theme.shape.extraSmall
                color: emptyHover.hovered ? Theme.color.surfaceContainerHigh : Theme.color.surfaceContainerLow
                border.width: Theme.editor.hairline
                border.color: Theme.color.outlineVariant
                HoverHandler { id: emptyHover; cursorShape: Qt.PointingHandCursor }
                TapHandler { onTapped: view.importRequested() }
                RowLayout {
                    anchors.centerIn: parent
                    spacing: Theme.space.md
                    Rectangle {
                        implicitWidth: Theme.editor.toolButtonSize
                        implicitHeight: implicitWidth
                        radius: Theme.shape.full
                        color: Theme.color.primaryContainer
                        Icon {
                            anchors.centerIn: parent
                            name: "add"
                            size: Theme.editor.toolIconSize
                            color: Theme.color.onPrimaryContainer
                        }
                    }
                    Label {
                        role: "bodyMedium"
                        color: Theme.color.onSurfaceVariant
                        text: qsTr("Drop videos and photos here, or click to import")
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
                    // On a template slot, or on any clip with Alt held: the media replaces the clip (SPEC §5.2, §5.13).
                    const target = view.model.clipAt(view.frameAt(event.x), row)
                    const altHeld = !!event.source && event.source.altHeld === true // a media item dragged with Alt
                    const replacing = target !== "" && (view.model.placeholderOf(target) !== "" || altHeld)
                    if (replacing && event.hasUrls && event.urls.length > 0) {
                        view.editor.replaceClipWithFile(target, event.urls[0])
                        event.acceptProposedAction()
                        return
                    }
                    if (replacing && event.source && event.source.mediaId !== undefined) {
                        view.editor.replaceClip(target, event.source.mediaId)
                        event.accept()
                        return
                    }
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

        // Floating timecode while scrubbing.
        Rectangle {
            id: timecodeBadge
            visible: view.isScrubbing
            anchors.bottom: knob.top
            anchors.bottomMargin: Theme.space.xs
            anchors.horizontalCenter: parent.horizontalCenter
            width: timecodeLabel.implicitWidth + Theme.space.md
            height: Theme.editor.badgeHeight
            radius: Theme.shape.extraSmall
            color: Theme.color.inverseSurface
            Label {
                id: timecodeLabel
                anchors.centerIn: parent
                role: "labelSmall"
                font.features: { "tnum": 1 }
                color: Theme.color.inverseOnSurface
                text: view.player.timecode(view.visualPlayheadFrame)
            }
        }

        // The line, with a thin dark edge so it reads on any picture.
        Rectangle {
            x: -width / 2
            width: Theme.editor.playheadWidth + 2 * Theme.editor.hairline
            height: parent.height
            color: Theme.alpha(Theme.color.scrim, 0.35)
        }
        Rectangle {
            x: -width / 2
            width: Theme.editor.playheadWidth
            height: parent.height
            color: Theme.color.primary
        }

        // The head, on the ruler: a rounded tab pointing down at the frame.
        Item {
            id: knob
            x: -width / 2
            y: Theme.editor.rulerHeight - height
            width: Theme.editor.playheadKnob
            height: Theme.editor.rulerHeight - Theme.space.sm
            Rectangle {
                width: parent.width
                height: parent.height - parent.width / 2
                radius: Theme.shape.extraSmall
                color: Theme.color.primary
            }
            Rectangle {
                // the point: a square turned by 45°, its lower half below the tab
                anchors.horizontalCenter: parent.horizontalCenter
                y: parent.height - parent.width / 2 - height / 2
                width: parent.width / Math.SQRT2
                height: width
                rotation: 45
                color: Theme.color.primary
            }
        }

        MouseArea {
            x: -Theme.editor.playheadKnob
            y: 0
            width: Theme.editor.playheadKnob * 2
            height: Theme.editor.rulerHeight
            cursorShape: Qt.SizeHorCursor
            onPressed: (mouse) => {
                view.startScrubbing(playheadItem.x - Theme.editor.trackHeaderWidth + mouse.x - Theme.editor.playheadKnob)
            }
            onPositionChanged: (mouse) => {
                if (pressed) {
                    const sceneX = mapToItem(ruler, mouse.x, 0).x
                    view.updateScrubbing(sceneX)
                }
            }
            onReleased: view.finishScrubbing()
            onCanceled: view.finishScrubbing()
        }
    }
}
