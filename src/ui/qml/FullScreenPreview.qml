// Full-screen preview (SPEC §5.3): the video alone on black, with a transport that shows while the pointer moves and
// hides after a moment. F11, Esc or the button leave it.
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
    readonly property TimelinePlayer player: editor.player
    signal closeRequested()

    color: Theme.color.scrim // black in every scheme: the video's letterbox is never tinted (SPEC §4)
    focus: visible

    // The controls are shown while the pointer moves, and while paused.
    property bool controlsShown: true
    Timer {
        id: hideTimer
        interval: Theme.editor.autoHideDelay
        onTriggered: root.controlsShown = !root.player.playing
    }
    function poke() {
        controlsShown = true
        hideTimer.restart()
    }
    onVisibleChanged: if (visible) poke()
    Connections {
        target: root.player
        function onStateChanged() { root.poke() }
    }

    VideoPreview {
        anchors.fill: parent
        sink: root.player.sink
        backgroundColor: root.color
    }
    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: root.controlsShown ? Qt.ArrowCursor : Qt.BlankCursor
        onPositionChanged: root.poke()
        onClicked: root.player.togglePlay()
        onDoubleClicked: root.closeRequested()
    }

    Rectangle {
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        height: transport.implicitHeight + 2 * Theme.space.lg
        opacity: root.controlsShown ? 1 : 0
        visible: opacity > 0
        Behavior on opacity { NumberAnimation { duration: Theme.motion.medium2 } }
        gradient: Gradient {
            GradientStop { position: 0; color: "transparent" }
            GradientStop { position: 1; color: Theme.alpha(Theme.color.scrim, 0.8) }
        }
        HoverHandler { onHoveredChanged: if (hovered) root.poke() }

        ColumnLayout {
            id: transport
            anchors { left: parent.left; right: parent.right; bottom: parent.bottom; margins: Theme.space.lg }
            spacing: Theme.space.sm

            Slider {
                Layout.fillWidth: true
                from: 0
                to: Math.max(1, root.editor.timeline.duration - 1)
                value: root.player.position
                Accessible.name: qsTr("Position")
                onMoved: root.player.seek(Math.round(value))
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.space.md
                IconButton {
                    variant: "filled"
                    iconName: root.player.playing ? "pause" : "play_arrow"
                    label: root.player.playing ? qsTr("Pause") : qsTr("Play")
                    shortcutText: qsTr("Space")
                    onClicked: root.player.togglePlay()
                }
                Label {
                    role: "labelLarge"
                    font.features: { "tnum": 1 }
                    color: Theme.readableOn(Theme.color.scrim)
                    text: qsTr("%1 / %2").arg(root.player.timecode(root.player.position))
                                         .arg(root.player.timecode(root.editor.timeline.duration))
                }
                Item { Layout.fillWidth: true }
                IconButton {
                    objectName: "exitFullScreenButton"
                    variant: "tonal"
                    iconName: "fullscreen_exit"
                    label: qsTr("Leave full screen")
                    shortcutText: qsTr("Esc")
                    onClicked: root.closeRequested()
                }
            }
        }
    }
}
