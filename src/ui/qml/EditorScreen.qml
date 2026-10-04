// The editor (SPEC §4): top bar, libraries on the left, preview, contextual toolbar and timeline.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Vedit.Components
import Vedit.Theme
import Vedit.UI

Item {
    id: root

    required property Editor editor
    signal message(string text, bool undoable)

    Connections {
        target: root.editor
        function onMessage(text, undoable) { root.message(text, undoable) }
        function onPropertiesRequested(page) { properties.showPage(page) }
        function onExportRequested() { exportDialog.openDialog() }
        function onImportRequested() { library.importFiles() }
        function onLibraryRequested(name) {
            const index = ["media", "audio", "text", "stickers", "effects", "transitions", "filters", "animations"].indexOf(name)
            if (index >= 0)
                rail.currentIndex = index
        }
        function onExportFinished(path) {
            // A desktop notification when vedit is in background (SPEC §5.15).
            if (!root.Window.window.active)
                App.notify(qsTr("Video exported"), path)
        }
    }

    // Keyboard (docs/SHORTCUTS.md). Single keys act only when no text field has the focus.
    readonly property bool typing: Window.activeFocusItem instanceof TextInput || Window.activeFocusItem instanceof TextEdit
    Shortcut { sequence: "Space"; enabled: !root.typing; onActivated: root.editor.player.togglePlay() }
    Shortcut { sequence: "L"; enabled: !root.typing; onActivated: root.editor.player.shuttleForward() }
    Shortcut { sequence: "J"; enabled: !root.typing; onActivated: root.editor.player.shuttleBackward() }
    Shortcut { sequence: "K"; enabled: !root.typing; onActivated: root.editor.player.pause() }
    Shortcut { sequence: "Left"; enabled: !root.typing; onActivated: root.editor.player.step(-1) }
    Shortcut { sequence: "Right"; enabled: !root.typing; onActivated: root.editor.player.step(1) }
    Shortcut { sequence: "Home"; enabled: !root.typing; onActivated: root.editor.player.seek(0) }
    Shortcut { sequence: "End"; enabled: !root.typing; onActivated: root.editor.player.seek(root.editor.timeline.duration) }
    Shortcut { sequences: ["S", "Ctrl+B"]; enabled: !root.typing; onActivated: root.editor.split() }
    Shortcut { sequence: "Q"; enabled: !root.typing; onActivated: root.editor.rippleTrimLeft() }
    Shortcut { sequence: "W"; enabled: !root.typing; onActivated: root.editor.rippleTrimRight() }
    Shortcut { sequences: [StandardKey.Delete, "Backspace"]; enabled: !root.typing; onActivated: root.editor.deleteSelection() }
    Shortcut { sequence: "Ctrl+D"; onActivated: root.editor.duplicateSelection() }
    Shortcut { sequences: [StandardKey.Undo]; enabled: !root.typing; onActivated: root.editor.undo() }
    Shortcut { sequences: [StandardKey.Redo, "Ctrl+Shift+Z", "Ctrl+Y"]; enabled: !root.typing; onActivated: root.editor.redo() }
    Shortcut { sequence: "Ctrl+E"; onActivated: exportDialog.openDialog() }
    Shortcut { sequence: "Ctrl+I"; onActivated: library.importFiles() }
    Shortcut { sequence: "Escape"; enabled: !root.typing; onActivated: root.editor.clearSelection() }
    Shortcut { sequence: "Ctrl+K"; onActivated: search.open() }
    Shortcut { sequence: "Ctrl+Alt+C"; enabled: !root.typing; onActivated: root.editor.actions.trigger("copyAttributes") }
    Shortcut { sequence: "Ctrl+Alt+V"; enabled: !root.typing; onActivated: root.editor.actions.trigger("pasteAttributes") }
    Shortcut { sequence: "M"; enabled: !root.typing; onActivated: root.editor.addMarker() }

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

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        EditorTopBar {
            Layout.fillWidth: true
            editor: root.editor
            onBackRequested: App.closeEditor()
            onExportRequested: exportDialog.openDialog()
            onSearchRequested: search.open()
        }

        // Upper half: libraries and preview. Lower half: toolbar and timeline, resizable.
        SplitView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            orientation: Qt.Vertical

            handle: Rectangle {
                implicitHeight: Theme.editor.splitterSize
                color: SplitHandle.hovered || SplitHandle.pressed ? Theme.color.surfaceContainerHighest : Theme.color.surfaceContainer
            }

            RowLayout {
                SplitView.fillHeight: true
                SplitView.minimumHeight: Theme.editor.previewMinimumHeight
                spacing: 0

                NavigationRail {
                    id: rail
                    Layout.fillHeight: true
                    model: [{ text: qsTr("Media"), iconName: "video_library" },
                            { text: qsTr("Audio"), iconName: "music_note" },
                            { text: qsTr("Text"), iconName: "title" },
                            { text: qsTr("Stickers"), iconName: "add_reaction" },
                            { text: qsTr("Effects"), iconName: "auto_awesome" },
                            { text: qsTr("Transitions"), iconName: "transition_fade" },
                            { text: qsTr("Filters"), iconName: "filter_vintage" },
                            { text: qsTr("Animations"), iconName: "animation" }]
                    onActivated: (index) => { if (index === 1) App.audioLibrary.load() }
                }
                StackLayout {
                    // Narrower on small windows (tiling window managers give what they have).
                    Layout.preferredWidth: Math.min(Theme.editor.libraryWidth, root.width / 3)
                    Layout.fillWidth: false // layouts fill by default: the preview takes the rest
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
                }
                Divider { vertical: true; Layout.fillHeight: true }
                PreviewPanel {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    Layout.fillHeight: true
                    editor: root.editor
                }
                Divider { vertical: true; Layout.fillHeight: true }
                PropertiesPanel {
                    id: properties
                    objectName: "propertiesPanel"
                    Layout.preferredWidth: Math.min(Theme.editor.propertiesWidth, root.width / 4)
                    Layout.fillWidth: false
                    Layout.fillHeight: true
                    editor: root.editor
                }
            }

            ColumnLayout {
                SplitView.preferredHeight: Theme.editor.timelineDefaultHeight
                SplitView.minimumHeight: Theme.editor.timelineMinimumHeight
                spacing: 0
                ContextToolbar {
                    Layout.fillWidth: true
                    editor: root.editor
                    timeline: timeline
                }
                Divider { Layout.fillWidth: true }
                TimelineView {
                    id: timeline
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    editor: root.editor
                    onImportRequested: library.importFiles()
                }
            }
        }
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
}
