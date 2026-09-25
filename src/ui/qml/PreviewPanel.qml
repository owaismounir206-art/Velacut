// Preview of the timeline with its transport: play/pause (Space, J/K/L), position and duration.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Vedit.Components
import Vedit.Theme
import Vedit.UI

Rectangle {
    id: panel

    required property Editor editor
    readonly property TimelinePlayer player: editor.player

    color: Theme.color.surfaceContainerLowest

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.space.md
        spacing: Theme.space.sm

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            VideoPreview {
                anchors.fill: parent
                sink: panel.player.sink
                backgroundColor: panel.color
            }
            CanvasHandles {
                anchors.fill: parent
                editor: panel.editor
            }
            // Before the first clip: say what to do, in the place where the result will appear.
            Label {
                anchors.centerIn: parent
                width: Math.min(parent.width - 2 * Theme.space.xl, Theme.editor.dialogWidth)
                visible: panel.editor.timeline.duration === 0
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                role: "bodyLarge"
                color: Theme.color.onSurfaceVariant
                text: qsTr("Add a video or a photo to the timeline: the preview appears here.")
            }
        }

        // Position on the left, buttons in the centre, duration on the right, at any width.
        Item {
            Layout.fillWidth: true
            implicitHeight: buttons.implicitHeight

            Label {
                anchors { left: parent.left; verticalCenter: parent.verticalCenter }
                role: "labelLarge"
                font.features: { "tnum": 1 }
                text: panel.player.timecode(panel.player.skimming ? panel.player.shownPosition : panel.player.position)
                color: panel.player.skimming ? Theme.color.onSurfaceVariant : Theme.color.onSurface
                Accessible.name: qsTr("Position")
            }
            Row {
                id: buttons
                anchors.centerIn: parent
                spacing: Theme.space.sm
                IconButton {
                    iconName: "skip_previous"
                    label: qsTr("Go to start")
                    shortcutText: "Home"
                    onClicked: panel.player.seek(0)
                }
                IconButton {
                    variant: "filled"
                    iconName: panel.player.playing ? "pause" : "play_arrow"
                    label: panel.player.playing ? qsTr("Pause") : qsTr("Play")
                    shortcutText: qsTr("Space · J K L")
                    enabled: panel.editor.timeline.duration > 0
                    onClicked: panel.player.togglePlay()
                }
                IconButton {
                    iconName: "skip_next"
                    label: qsTr("Go to end")
                    shortcutText: qsTr("End")
                    onClicked: panel.player.seek(panel.editor.timeline.duration)
                }
            }
            // Format of the canvas under the player (SPEC §4), one click away (SPEC 0bis rule 1).
            FormatButton {
                anchors { right: durationLabel.left; rightMargin: Theme.space.sm; verticalCenter: parent.verticalCenter }
                editor: panel.editor
            }
            Label {
                id: durationLabel
                anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                role: "labelLarge"
                font.features: { "tnum": 1 }
                color: Theme.color.onSurfaceVariant
                text: panel.player.rate !== 0 && panel.player.rate !== 1
                      ? qsTr("%1× · %2").arg(panel.player.rate).arg(panel.player.timecode(panel.editor.timeline.duration))
                      : panel.player.timecode(panel.editor.timeline.duration)
                Accessible.name: qsTr("Duration")
            }
        }
    }
}
