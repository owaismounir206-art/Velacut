// Recording dialog & overlay (SPEC §5.1, §5.10):
// voiceover directly into timeline with countdown, audio level monitor, webcam and screen capture, teleprompter.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Velacut.Components
import Velacut.Theme
import Velacut.UI

Rectangle {
    id: root

    required property Editor editor
    readonly property RecordController recorder: editor.recorder

    visible: recorder.active
    color: "#E6000000"
    anchors.fill: parent
    z: 100

    function formatDuration(seconds) {
        const total = Math.floor(seconds)
        const mins = Math.floor(total / 60)
        const secs = total % 60
        const tenths = Math.floor((seconds - total) * 10)
        return (mins < 10 ? "0" : "") + mins + ":" + (secs < 10 ? "0" : "") + secs + "." + tenths
    }

    ColumnLayout {
        anchors.centerIn: parent
        width: Math.min(parent.width - Theme.space.xl * 2, 720)
        spacing: Theme.space.lg

        // Header
        RowLayout {
            Layout.fillWidth: true

            Label {
                role: "headlineSmall"
                color: "white"
                text: {
                    switch (root.recorder.mode) {
                    case 0: return qsTr("Record voiceover")
                    case 1: return qsTr("Record screen")
                    case 2: return qsTr("Record webcam")
                    case 3: return qsTr("Record screen & webcam")
                    default: return qsTr("Record")
                    }
                }
            }

            Item { Layout.fillWidth: true }

            IconButton {
                iconName: "close"
                label: qsTr("Cancel")
                onClicked: root.recorder.close()
            }
        }

        // Mode selector
        SegmentedButton {
            id: modeSelector
            Layout.fillWidth: true
            enabled: !root.recorder.recording && root.recorder.status !== 1
            model: [
                { text: qsTr("Voiceover") },
                { text: qsTr("Screen") },
                { text: qsTr("Webcam") },
                { text: qsTr("Screen & Cam") }
            ]
            currentIndex: root.recorder.mode
            onActivated: (index) => root.recorder.setMode(index)
        }

        // Teleprompter (collapsible)
        Teleprompter {
            Layout.fillWidth: true
            Layout.preferredHeight: 280
            visible: root.recorder.teleprompterVisible
            controller: root.recorder
        }

        // Controls bar: Teleprompter toggle, Audio Level, Timer, Big Action Button
        Card {
            Layout.fillWidth: true
            variant: "filled"

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: Theme.space.lg
                spacing: Theme.space.md

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.space.md

                    // Teleprompter toggle
                    Button {
                        variant: root.recorder.teleprompterVisible ? "filled" : "outlined"
                        iconName: "notes"
                        text: qsTr("Teleprompter")
                        onClicked: root.recorder.teleprompterVisible = !root.recorder.teleprompterVisible
                    }

                    // Audio level indicator
                    RowLayout {
                        spacing: Theme.space.xs
                        Icon {
                            name: "mic"
                            size: 20
                            color: root.recorder.audioLevel > 0.05 ? Theme.color.primary : Theme.color.onSurfaceVariant
                        }
                        Rectangle {
                            Layout.preferredWidth: 100
                            Layout.preferredHeight: 8
                            radius: 4
                            color: Theme.color.surfaceContainerHighest

                            Rectangle {
                                anchors.left: parent.left
                                anchors.top: parent.top
                                anchors.bottom: parent.bottom
                                width: parent.width * root.recorder.audioLevel
                                radius: 4
                                color: root.recorder.audioLevel > 0.9 ? Theme.color.error : Theme.color.primary
                            }
                        }
                    }

                    Item { Layout.fillWidth: true }

                    // Duration display
                    Label {
                        role: "headlineMedium"
                        font.features: { "tnum": 1 }
                        color: root.recorder.recording ? Theme.color.error : "white"
                        text: root.formatDuration(root.recorder.durationSeconds)
                    }
                }

                // Main action buttons
                RowLayout {
                    Layout.alignment: Qt.AlignHCenter
                    spacing: Theme.space.xl

                    // Cancel button
                    IconButton {
                        visible: root.recorder.recording || root.recorder.status === 1
                        iconName: "close"
                        label: qsTr("Discard recording")
                        onClicked: root.recorder.cancelRecording()
                    }

                    // Pause / Resume button
                    IconButton {
                        visible: root.recorder.recording
                        iconName: root.recorder.paused ? "play_arrow" : "pause"
                        label: root.recorder.paused ? qsTr("Resume") : qsTr("Pause")
                        onClicked: {
                            if (root.recorder.paused) {
                                root.recorder.resumeRecording()
                            } else {
                                root.recorder.pauseRecording()
                            }
                        }
                    }

                    // Central Record / Stop Button
                    Rectangle {
                        id: recordBtn
                        objectName: "recordActionButton"
                        width: 72
                        height: 72
                        radius: 36
                        color: root.recorder.recording ? Theme.color.error : "#E53935"

                        TapHandler {
                            onTapped: {
                                if (root.recorder.recording) {
                                    root.recorder.stopRecording()
                                } else if (root.recorder.status === 0) {
                                    root.recorder.startCountdown()
                                }
                            }
                        }

                        // Icon inside button: circle when idle, square when recording
                        Rectangle {
                            anchors.centerIn: parent
                            width: root.recorder.recording ? 24 : 32
                            height: width
                            radius: root.recorder.recording ? 4 : width / 2
                            color: "white"
                        }
                    }
                }
            }
        }
    }

    // Countdown 3-2-1 Overlay
    Rectangle {
        anchors.fill: parent
        visible: root.recorder.status === 1 // CountingDown
        color: "#B0000000"

        Label {
            anchors.centerIn: parent
            text: root.recorder.countdown > 0 ? root.recorder.countdown.toString() : ""
            font.pointSize: 120
            font.weight: Font.Black
            color: Theme.color.primary
        }
    }
}
