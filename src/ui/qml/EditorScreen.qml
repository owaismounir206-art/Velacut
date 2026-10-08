// The editor (SPEC §4): top bar, libraries on the left, preview, contextual toolbar and timeline.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import Velacut.Components
import Velacut.Theme
import Velacut.UI

Item {
    id: root

    required property Editor editor
    signal message(string text, bool undoable)
    signal infoRequested()
    signal preferencesRequested()
    // Preferences → AI models (from "Auto captions" when a component or model is missing).
    signal aiModelsRequested()

    Connections {
        target: root.editor
        function onMessage(text, undoable) { root.message(text, undoable) }
        function onPropertiesRequested(page) { properties.showPage(page) }
        function onExportRequested() { exportDialog.openDialog() }
        function onReplaceRequested(clipId) {
            replaceDialog.clipId = clipId
            replaceDialog.open()
        }
        function onImportRequested() { library.importFiles() }
        function onLibraryRequested(name) {
            const index = ["media", "audio", "text", "captions", "transcript", "stickers", "effects", "transitions", "filters", "animations", "brand"].indexOf(name)
            if (index >= 0)
                rail.currentIndex = index
        }
        function onExportFinished(path) {
            // A desktop notification when velacut is in background (SPEC §5.15).
            if (!root.Window.window.active)
                App.notify(qsTr("Video exported"), path)
        }
    }

    // The frame on screen as an image (SPEC §5.15), from the menu, the player or Ctrl+Shift+E.
    function saveFrame() {
        if (root.editor.timeline.duration === 0)
            return
        const path = root.editor.exportCurrentFrame(root.editor.name, App.videosFolder())
        App.message(path !== "" ? qsTr("Frame saved as %1").arg(path) : qsTr("The frame could not be saved."))
    }

    // Full-screen preview (SPEC §5.3): the window goes full screen with the video alone, and comes back as it was.
    property bool fullScreen: false
    property int windowVisibilityBefore: Window.Windowed
    function setFullScreen(on) {
        const window = root.Window.window
        if (on === fullScreen || !window)
            return
        if (on) {
            windowVisibilityBefore = window.visibility
            window.visibility = Window.FullScreen
        } else {
            window.visibility = windowVisibilityBefore === Window.FullScreen ? Window.Windowed : windowVisibilityBefore
        }
        fullScreen = on
    }

    // Keyboard (docs/SHORTCUTS.md). Single keys act only when no text field has the focus.
    readonly property bool typing: Window.activeFocusItem instanceof TextInput || Window.activeFocusItem instanceof TextEdit
    Shortcut { sequence: "Space"; enabled: !root.typing; onActivated: root.editor.player.togglePlay() }
    Shortcut { sequence: "L"; enabled: !root.typing; onActivated: root.editor.player.shuttleForward() }
    Shortcut { sequence: "J"; enabled: !root.typing; onActivated: root.editor.player.shuttleBackward() }
    Shortcut { sequence: "K"; enabled: !root.typing; onActivated: root.editor.player.pause() }
    Shortcut { sequence: "Left"; enabled: !root.typing; onActivated: root.editor.player.step(-1) }
    Shortcut { sequence: "Right"; enabled: !root.typing; onActivated: root.editor.player.step(1) }
    Shortcut { sequence: "Shift+Left"; enabled: !root.typing; onActivated: root.editor.player.step(-10) }
    Shortcut { sequence: "Shift+Right"; enabled: !root.typing; onActivated: root.editor.player.step(10) }
    Shortcut { sequence: "Up"; enabled: !root.typing; onActivated: root.editor.previousCut() }
    Shortcut { sequence: "Down"; enabled: !root.typing; onActivated: root.editor.nextCut() }
    Shortcut { sequence: "Home"; enabled: !root.typing; onActivated: root.editor.player.seek(0) }
    Shortcut { sequence: "End"; enabled: !root.typing; onActivated: root.editor.player.seek(root.editor.timeline.duration) }
    Shortcut { sequences: ["S", "Ctrl+B"]; enabled: !root.typing; onActivated: root.editor.split() }
    Shortcut { sequence: "Q"; enabled: !root.typing; onActivated: root.editor.rippleTrimLeft() }
    Shortcut { sequence: "W"; enabled: !root.typing; onActivated: root.editor.rippleTrimRight() }
    Shortcut { sequence: "["; enabled: !root.typing; onActivated: root.editor.trimSelectedToPlayhead(true) }
    Shortcut { sequence: "]"; enabled: !root.typing; onActivated: root.editor.trimSelectedToPlayhead(false) }
    Shortcut { sequences: [StandardKey.Delete, "Backspace"]; enabled: !root.typing; onActivated: root.editor.deleteSelection() }
    Shortcut { sequences: ["Shift+Delete", "Shift+Backspace"]; enabled: !root.typing; onActivated: root.editor.rippleDeleteSelection() }
    Shortcut { sequence: "Ctrl+D"; onActivated: root.editor.duplicateSelection() }
    Shortcut { sequences: [StandardKey.SelectAll]; enabled: !root.typing; onActivated: root.editor.selectAll() }
    Shortcut { sequence: "Ctrl+T"; onActivated: root.editor.addText() }
    Shortcut { sequences: [StandardKey.New]; onActivated: App.newProject() }
    Shortcut { sequence: "Ctrl+Shift+E"; onActivated: root.saveFrame() }
    Shortcut { sequences: [StandardKey.Undo]; enabled: !root.typing; onActivated: root.editor.undo() }
    Shortcut { sequences: [StandardKey.Redo, "Ctrl+Shift+Z", "Ctrl+Y"]; enabled: !root.typing; onActivated: root.editor.redo() }
    Shortcut { sequence: "Ctrl+E"; onActivated: exportDialog.openDialog() }
    Shortcut { sequence: "Ctrl+I"; onActivated: library.importFiles() }
    Shortcut { sequence: "Escape"; enabled: !root.typing && !root.fullScreen; onActivated: root.editor.clearSelection() }
    Shortcut { sequence: "Escape"; enabled: root.fullScreen; onActivated: root.setFullScreen(false) }
    Shortcut { sequence: "F11"; enabled: root.fullScreen || root.editor.timeline.duration > 0; onActivated: root.setFullScreen(!root.fullScreen) }
    Shortcut { sequence: "Ctrl+K"; onActivated: search.open() }
    Shortcut { sequence: "Ctrl+Alt+C"; enabled: !root.typing; onActivated: root.editor.actions.trigger("copyAttributes") }
    Shortcut { sequence: "Ctrl+Alt+V"; enabled: !root.typing; onActivated: root.editor.actions.trigger("pasteAttributes") }
    Shortcut { sequence: "M"; enabled: !root.typing; onActivated: root.editor.addMarker() }
    Shortcut { sequence: "N"; enabled: !root.typing; onActivated: root.editor.toggleMagneticMain() }
    Shortcut { sequence: "\\"; enabled: !root.typing; onActivated: root.editor.toggleSnapping() }
    Shortcut { sequence: "I"; enabled: !root.typing; onActivated: root.editor.setInPoint() }
    Shortcut { sequence: "O"; enabled: !root.typing; onActivated: root.editor.setOutPoint() }
    Shortcut { sequence: "Alt+X"; enabled: !root.typing; onActivated: root.editor.clearInOut() }
    Shortcut { sequences: ["+", "=", "Ctrl+="]; enabled: !root.typing; onActivated: timeline.zoomBy(Theme.editor.zoomStep) }
    Shortcut { sequences: ["-", "Ctrl+-", "Ctrl+_"]; enabled: !root.typing; onActivated: timeline.zoomBy(1 / Theme.editor.zoomStep) }
    Shortcut { sequence: "Ctrl+0"; enabled: !root.typing; onActivated: timeline.zoomToFit() }
    Shortcut { sequence: "Ctrl+1"; enabled: !root.typing; onActivated: timeline.setZoom(1.0) }

    // Multicam camera switching (keys 1-9, SPEC §5.10 & §8)
    Shortcut { sequence: "1"; enabled: !root.typing; onActivated: root.editor.multicamAngleKey(1) }
    Shortcut { sequence: "2"; enabled: !root.typing; onActivated: root.editor.multicamAngleKey(2) }
    Shortcut { sequence: "3"; enabled: !root.typing; onActivated: root.editor.multicamAngleKey(3) }
    Shortcut { sequence: "4"; enabled: !root.typing; onActivated: root.editor.multicamAngleKey(4) }
    Shortcut { sequence: "5"; enabled: !root.typing; onActivated: root.editor.multicamAngleKey(5) }
    Shortcut { sequence: "6"; enabled: !root.typing; onActivated: root.editor.multicamAngleKey(6) }
    Shortcut { sequence: "7"; enabled: !root.typing; onActivated: root.editor.multicamAngleKey(7) }
    Shortcut { sequence: "8"; enabled: !root.typing; onActivated: root.editor.multicamAngleKey(8) }
    Shortcut { sequence: "9"; enabled: !root.typing; onActivated: root.editor.multicamAngleKey(9) }

    // The editor as a desktop video editor lays it out: the panels are cards on a darker backdrop, separated by narrow
    // gaps that are also the handles to resize them (sizes remembered with the project, SPEC §4).
    Rectangle {
        anchors.fill: parent
        color: Theme.color.backdrop
    }

    // A gap between two panels that resizes them: invisible until pointed at, then a thin accent line.
    component Gap: Rectangle {
        required property bool vertical
        implicitWidth: vertical ? Theme.space.xs : 0
        implicitHeight: vertical ? 0 : Theme.space.xs
        color: "transparent"
        Rectangle {
            anchors.centerIn: parent
            width: parent.vertical ? Theme.editor.selectionBorder : parent.width / 4
            height: parent.vertical ? parent.height / 4 : Theme.editor.selectionBorder
            radius: Theme.shape.full
            color: Theme.color.primary
            opacity: parent.SplitHandle.pressed ? 1 : parent.SplitHandle.hovered ? 0.6 : 0
            Behavior on opacity { NumberAnimation { duration: Theme.motion.short3 } }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        visible: !root.fullScreen
        anchors.leftMargin: Theme.space.xs
        anchors.rightMargin: Theme.space.xs
        anchors.bottomMargin: Theme.space.xs
        spacing: 0

        EditorTopBar {
            Layout.fillWidth: true
            editor: root.editor
            onBackRequested: App.closeEditor()
            onExportRequested: exportDialog.openDialog()
            onSearchRequested: search.open()
            onImportRequested: library.importFiles()
            onInfoRequested: root.infoRequested()
            onShortcutsRequested: shortcuts.open()
            onPreferencesRequested: root.preferencesRequested()
            onFrameRequested: root.saveFrame()
        }

        // Upper half: libraries, preview and properties. Lower half: toolbar and timeline.
        SplitView {
            id: vertical
            Layout.fillWidth: true
            Layout.fillHeight: true
            orientation: Qt.Vertical
            handle: Gap { vertical: false }

            SplitView {
                id: upper
                SplitView.fillHeight: true
                SplitView.minimumHeight: Theme.editor.previewMinimumHeight
                orientation: Qt.Horizontal
                handle: Gap { vertical: true }

                Panel {
                    id: libraryPanel
                    objectName: "libraryPanel"
                    SplitView.preferredWidth: root.editor.panelSize("library", Math.min(Theme.editor.railWidth + Theme.editor.libraryWidth,
                                                                                        root.width / 3))
                    SplitView.minimumWidth: Theme.editor.railWidth + Theme.editor.panelMinimumWidth
                    onWidthChanged: if (width > 0) Qt.callLater(root.editor.setPanelSize, "library", width)

                    RowLayout {
                        anchors.fill: parent
                        spacing: 0

                        NavigationRail {
                            id: rail
                            Layout.fillHeight: true
                            Layout.preferredWidth: Theme.editor.railWidth
                            compact: true
                            color: "transparent"
                            model: [{ text: qsTr("Media"), iconName: "video_library" },
                                    { text: qsTr("Audio"), iconName: "music_note" },
                                    { text: qsTr("Text"), iconName: "title" },
                                    { text: qsTr("Captions"), iconName: "subtitles" },
                                    { text: qsTr("Transcript"), iconName: "description" },
                                    { text: qsTr("Stickers"), iconName: "add_reaction" },
                                    { text: qsTr("Effects"), iconName: "auto_awesome" },
                                    { text: qsTr("Transitions"), iconName: "transition_fade" },
                                    { text: qsTr("Filters"), iconName: "filter_vintage" },
                                    { text: qsTr("Animations"), iconName: "animation" },
                                    { text: qsTr("Brand"), iconName: "verified" }]
                            onActivated: (index) => { if (index === 1) App.audioLibrary.load() }
                        }
                        Divider { vertical: true; Layout.fillHeight: true }
                        StackLayout {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            currentIndex: rail.currentIndex
                            MediaPanel {
                                id: library
                                editor: root.editor
                            }
                            AudioPanel {
                                editor: root.editor
                            }
                            AssetPanel {
                                editor: root.editor
                                kind: AssetLibraryModel.TextStyles
                                title: qsTr("Text")
                            }
                            CaptionsPanel {
                                editor: root.editor
                                onSetUpRequested: root.aiModelsRequested()
                            }
                            TranscriptPanel {
                                editor: root.editor
                                onSetUpRequested: root.aiModelsRequested()
                            }
                            AssetPanel {
                                editor: root.editor
                                kind: AssetLibraryModel.Stickers
                                title: qsTr("Stickers")
                            }
                            AssetPanel {
                                editor: root.editor
                                kind: AssetLibraryModel.VideoEffects
                                title: qsTr("Effects")
                            }
                            AssetPanel {
                                editor: root.editor
                                kind: AssetLibraryModel.Transitions
                                title: qsTr("Transitions")
                            }
                            AssetPanel {
                                editor: root.editor
                                kind: AssetLibraryModel.Filters
                                title: qsTr("Filters")
                            }
                            AssetPanel {
                                editor: root.editor
                                kind: AssetLibraryModel.Animations
                                title: qsTr("Animations")
                            }
                            BrandKitPanel {
                                editor: root.editor
                            }
                        }
                    }
                }

                Panel {
                    SplitView.fillWidth: true
                    SplitView.minimumWidth: Theme.editor.panelMinimumWidth
                    PreviewPanel {
                        anchors.fill: parent
                        editor: root.editor
                        onFullScreenRequested: root.setFullScreen(true)
                        onFrameRequested: root.saveFrame()
                        onTemplateMediaRequested: templateMediaDialog.open()
                    }
                }

                Panel {
                    SplitView.preferredWidth: root.editor.panelSize("properties", Math.min(Theme.editor.propertiesWidth, root.width / 4))
                    SplitView.minimumWidth: Theme.editor.panelMinimumWidth
                    onWidthChanged: if (width > 0) Qt.callLater(root.editor.setPanelSize, "properties", width)
                    PropertiesPanel {
                        id: properties
                        objectName: "propertiesPanel"
                        anchors.fill: parent
                        editor: root.editor
                    }
                }
            }

            Panel {
                SplitView.preferredHeight: root.editor.panelSize("timeline", Theme.editor.timelineDefaultHeight)
                SplitView.minimumHeight: Theme.editor.timelineMinimumHeight
                onHeightChanged: if (height > 0) Qt.callLater(root.editor.setPanelSize, "timeline", height)

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 0
                    ContextToolbar {
                        Layout.fillWidth: true
                        editor: root.editor
                        timeline: timeline
                    }
                    Divider { Layout.fillWidth: true }
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        spacing: 0
                        TimelineView {
                            id: timeline
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            editor: root.editor
                            onImportRequested: library.importFiles()
                        }
                        // Level of the whole mix (master, SPEC §5.9), along the tracks.
                        LevelMeter {
                            objectName: "masterMeter"
                            Layout.fillHeight: true
                            Layout.topMargin: Theme.editor.rulerHeight
                            Layout.bottomMargin: Theme.space.sm
                            Layout.leftMargin: Theme.space.xs
                            Layout.rightMargin: Theme.space.sm
                            player: root.editor.player
                            key: "master"
                        }
                    }
                }
            }
        }
    }

    FullScreenPreview {
        anchors.fill: parent
        visible: root.fullScreen
        editor: root.editor
        onCloseRequested: root.setFullScreen(false)
    }

    SearchDialog {
        id: search
        editor: root.editor
    }

    ExportDialog {
        id: exportDialog
        editor: root.editor
        anchors.centerIn: parent
        width: Math.min(root.width - 2 * Theme.space.xl, Theme.editor.dialogWidth)
    }

    RecordDialog {
        id: recordDialog
        editor: root.editor
    }

    // "Replace": one video or photo for the clip (keeps its place, length and look).
    FileDialog {
        id: replaceDialog
        property string clipId
        title: root.editor.timeline.placeholderOf(clipId) !== ""
               ? qsTr("Choose a video or a photo for “%1”").arg(root.editor.timeline.placeholderOf(clipId))
               : qsTr("Choose a video or a photo to put in its place")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("Videos and photos (%1)").arg("*.mp4 *.mov *.mkv *.webm *.avi *.mts *.m2ts *.m4v *.jpg *.jpeg *.png *.webp *.gif *.heic"),
                      qsTr("All files (*)")]
        onAccepted: root.editor.replaceClipWithFile(clipId, selectedFile)
    }
    // A template: all its shots at once, in order (SPEC §5.13).
    FileDialog {
        id: templateMediaDialog
        objectName: "templateMediaDialog"
        title: qsTr("Choose %n video(s) or photo(s) for the template", "", root.editor.placeholderCount)
        fileMode: FileDialog.OpenFiles
        nameFilters: [qsTr("Videos, photos and music (%1)").arg("*.mp4 *.mov *.mkv *.webm *.avi *.mts *.m2ts *.m4v *.jpg *.jpeg *.png *.webp *.gif *.heic *.mp3 *.wav *.flac *.aac *.ogg *.opus *.m4a"),
                      qsTr("All files (*)")]
        onAccepted: root.editor.fillPlaceholders(selectedFiles)
    }
    Component.onCompleted: {
        if (root.editor.askForTemplateMedia) {
            root.editor.askForTemplateMedia = false
            templateMediaDialog.open()
        }
    }

    ShortcutsDialog {
        id: shortcuts
        anchors.centerIn: parent
        width: Math.min(root.width - 2 * Theme.space.xl, Theme.editor.dialogWidth)
        height: Math.min(root.height - 2 * Theme.space.xl, implicitHeight)
    }
}
