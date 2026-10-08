// Teleprompter for voiceover and webcam recording (SPEC §5.1):
// auto-scrolling script with speed control and horizontal mirror for glass beam splitters.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Velacut.Components
import Velacut.Theme
import Velacut.UI

Rectangle {
    id: prompter

    required property RecordController controller
    property bool editing: controller.teleprompterText === ""

    color: "#D0121212"
    radius: Theme.shape.medium
    border.color: Theme.color.outlineVariant
    border.width: 1
    clip: true

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.space.md
        spacing: Theme.space.sm

        // Top bar: controls
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.space.sm

            Label {
                text: qsTr("Teleprompter")
                role: "titleSmall"
                color: "white"
            }

            Item { Layout.fillWidth: true }

            IconButton {
                iconName: prompter.editing ? "visibility" : "edit"
                label: prompter.editing ? qsTr("View prompter") : qsTr("Edit script")
                onClicked: prompter.editing = !prompter.editing
            }

            IconButton {
                iconName: "flip"
                label: qsTr("Mirror horizontally")
                checkable: true
                checked: prompter.controller.teleprompterMirrored
                onClicked: prompter.controller.teleprompterMirrored = checked
            }

            IconButton {
                iconName: "replay"
                label: qsTr("Rewind to start")
                onClicked: prompter.controller.teleprompterScroll = 0
            }

            IconButton {
                iconName: prompter.controller.teleprompterPlaying ? "pause" : "play_arrow"
                label: prompter.controller.teleprompterPlaying ? qsTr("Pause scroll") : qsTr("Start scroll")
                onClicked: prompter.controller.teleprompterPlaying = !prompter.controller.teleprompterPlaying
            }
        }

        // Speed control slider
        RowLayout {
            Layout.fillWidth: true
            visible: !prompter.editing
            spacing: Theme.space.sm

            Label {
                text: qsTr("Speed")
                role: "labelMedium"
                color: "#CCCCCC"
            }

            Slider {
                Layout.fillWidth: true
                from: 10
                to: 300
                value: prompter.controller.teleprompterSpeed
                onMoved: prompter.controller.teleprompterSpeed = value
            }

            Label {
                text: Math.round(prompter.controller.teleprompterSpeed) + " px/s"
                role: "labelSmall"
                color: "#AAAAAA"
            }
        }

        // Script Editor View
        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: prompter.editing

            TextArea {
                id: scriptInput
                placeholderText: qsTr("Paste or type your script here…")
                text: prompter.controller.teleprompterText
                color: "white"
                wrapMode: Text.WordWrap
                font.pointSize: 16
                background: Rectangle { color: "#202020"; radius: Theme.shape.small }
                onTextChanged: prompter.controller.teleprompterText = text
            }
        }

        // Prompter Scroll View
        Item {
            id: displayContainer
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: !prompter.editing
            clip: true

            transform: Scale {
                xScale: prompter.controller.teleprompterMirrored ? -1 : 1
                origin.x: displayContainer.width / 2
            }

            Flickable {
                id: flick
                anchors.fill: parent
                contentWidth: width
                contentHeight: scriptText.height + displayContainer.height
                contentY: prompter.controller.teleprompterScroll
                interactive: true
                onContentYChanged: {
                    if (moving) {
                        prompter.controller.teleprompterScroll = contentY
                    }
                }

                Label {
                    id: scriptText
                    width: flick.width - Theme.space.xl * 2
                    x: Theme.space.xl
                    y: displayContainer.height * 0.25
                    text: prompter.controller.teleprompterText !== "" ? prompter.controller.teleprompterText : qsTr("No script loaded. Click edit above.")
                    color: "white"
                    font.pointSize: 24
                    font.weight: Font.Bold
                    lineHeight: 1.5
                    wrapMode: Text.WordWrap
                    horizontalAlignment: Text.AlignHCenter
                }
            }

            // Target reading guide line in the upper third
            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                y: parent.height * 0.25 + 16
                height: 2
                color: Theme.color.primary
                opacity: 0.6
            }
        }
    }
}
