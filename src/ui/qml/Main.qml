// Phase 0 window: open a video and play it (SPEC §8, Phase 0). The full editor layout arrives in Phase 1.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material as M
import QtQuick.Dialogs
import QtQuick.Layouts
import Vedit.Components
import Vedit.Theme
import Vedit.UI

ApplicationWindow {
    id: window

    readonly property var player: App.player

    width: 1280
    height: 800
    minimumWidth: 480
    minimumHeight: 360
    visible: true
    title: player.source !== "" ? qsTr("%1 — vedit").arg(player.source.split("/").pop()) : "vedit"
    color: Theme.color.surface

    // Colors of the Qt Material controls this style does not redefine.
    M.Material.theme: Theme.dark ? M.Material.Dark : M.Material.Light
    M.Material.accent: Theme.color.primary
    M.Material.primary: Theme.color.primary
    M.Material.background: Theme.color.surface
    M.Material.foreground: Theme.color.onSurface

    function openFile(url) {
        player.open(App.localPath(url))
    }

    Shortcut { sequences: [StandardKey.Open]; onActivated: fileDialog.open() }
    Shortcut { sequence: "Space"; enabled: window.player.ready; onActivated: window.player.togglePlay() }
    Shortcut { sequence: "Left"; enabled: window.player.ready; onActivated: window.player.step(-1) }
    Shortcut { sequence: "Right"; enabled: window.player.ready; onActivated: window.player.step(1) }
    Shortcut { sequence: "Home"; enabled: window.player.ready; onActivated: window.player.seek(0) }
    Shortcut { sequence: "End"; enabled: window.player.ready; onActivated: window.player.seek(window.player.duration - 1) }

    FileDialog {
        id: fileDialog
        title: qsTr("Open a video")
        nameFilters: [qsTr("Videos and audio (%1)").arg("*.mp4 *.mov *.mkv *.webm *.avi *.mts *.m2ts *.mxf *.mp3 *.wav *.flac *.ogg *.opus *.m4a"),
                      qsTr("All files (*)")]
        onAccepted: window.openFile(selectedFile)
    }

    Connections {
        target: window.player
        function onStateChanged() {
            if (window.player.error !== "")
                snackbar.show(window.player.error, "")
        }
    }

    header: TopAppBar {
        title: window.player.source !== "" ? window.player.source.split("/").pop() : "vedit"
        trailing: [
            Chip {
                variant: "assist"
                iconName: App.softwareRendering ? "memory" : "speed"
                text: App.safeMode ? qsTr("Safe mode") : App.uiBackend
                checkable: false
                onClicked: infoDialog.open()
            },
            IconButton {
                iconName: "info"
                label: qsTr("System information")
                onClicked: infoDialog.open()
            },
            Button {
                variant: "tonal"
                iconName: "folder_open"
                text: qsTr("Open video")
                onClicked: fileDialog.open()
            }
        ]
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.space.lg
        spacing: Theme.space.md

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: Theme.shape.large
            color: Theme.color.surfaceContainerLowest
            clip: true

            VideoPreview {
                anchors.fill: parent
                anchors.margins: 1
                sink: window.player.sink
                visible: window.player.ready
            }

            ColumnLayout {
                anchors.centerIn: parent
                visible: !window.player.ready
                spacing: Theme.space.lg
                Icon {
                    Layout.alignment: Qt.AlignHCenter
                    name: "movie"
                    size: 64
                    color: Theme.color.onSurfaceVariant
                }
                Label {
                    Layout.alignment: Qt.AlignHCenter
                    role: "titleLarge"
                    text: window.player.loading ? qsTr("Opening…") : qsTr("Open a video to start")
                }
                BusyIndicator {
                    Layout.alignment: Qt.AlignHCenter
                    running: window.player.loading
                    visible: running
                }
                Button {
                    Layout.alignment: Qt.AlignHCenter
                    visible: !window.player.loading
                    iconName: "folder_open"
                    text: qsTr("Open video")
                    onClicked: fileDialog.open()
                }
                Label {
                    Layout.alignment: Qt.AlignHCenter
                    visible: !window.player.loading
                    role: "bodyMedium"
                    color: Theme.color.onSurfaceVariant
                    text: qsTr("or drop a file here")
                }
            }

            DropArea {
                anchors.fill: parent
                onDropped: (drop) => {
                    if (drop.hasUrls)
                        window.openFile(drop.urls[0])
                }
            }
        }

        // Transport bar
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.space.sm
            enabled: window.player.ready

            IconButton {
                iconName: "skip_previous"
                label: qsTr("Go to start")
                shortcutText: "Home"
                onClicked: window.player.seek(0)
            }
            IconButton {
                iconName: "chevron_left"
                label: qsTr("Previous frame")
                shortcutText: "←"
                onClicked: window.player.step(-1)
            }
            IconButton {
                variant: "filled"
                iconName: window.player.playing ? "pause" : "play_arrow"
                label: window.player.playing ? qsTr("Pause") : qsTr("Play")
                shortcutText: qsTr("Space")
                onClicked: window.player.togglePlay()
            }
            IconButton {
                iconName: "chevron_right"
                label: qsTr("Next frame")
                shortcutText: "→"
                onClicked: window.player.step(1)
            }
            Label {
                role: "labelLarge"
                font.features: { "tnum": 1 }
                text: window.player.timecode(window.player.position) + " / " + window.player.timecode(Math.max(0, window.player.duration - 1))
            }
            Slider {
                id: seekSlider
                Layout.fillWidth: true
                from: 0
                to: Math.max(1, window.player.duration - 1)
                stepSize: 1
                value: window.player.position
                valueText: window.player.timecode(value)
                Accessible.name: qsTr("Position")
                onMoved: window.player.seek(value)
            }
            Icon {
                name: window.player.volume > 0 ? "volume_up" : "volume_off"
            }
            Slider {
                Layout.preferredWidth: 120
                from: 0
                to: 1
                value: window.player.volume
                valueText: Math.round(value * 100) + "%"
                Accessible.name: qsTr("Volume")
                onMoved: window.player.volume = value
            }
        }
    }

    Snackbar {
        id: snackbar
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Theme.space.xl + 56
    }

    Dialog {
        id: infoDialog
        title: qsTr("System information")
        iconName: "info"
        width: Math.min(window.width - 48, 640)
        standardButtons: Dialog.Close

        ColumnLayout {
            width: parent.width
            spacing: Theme.space.md
            ScrollView {
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(360, info.implicitHeight)
                clip: true
                Label {
                    id: info
                    width: infoDialog.availableWidth
                    role: "bodySmall"
                    color: Theme.color.onSurfaceVariant
                    wrapMode: Text.WrapAnywhere
                    textFormat: Text.PlainText
                    text: App.systemInformation
                }
            }
            Button {
                variant: "tonal"
                iconName: "content_copy"
                text: qsTr("Copy system information")
                onClicked: {
                    App.copySystemInformation()
                    snackbar.show(qsTr("System information copied"), "")
                }
            }
        }
    }
}
