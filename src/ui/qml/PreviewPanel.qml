// The player (SPEC §4, §5.3): a header, the preview with its handles on the canvas, and the transport: position and
// duration on the left, play/pause (Space, J/K/L) in the middle, scopes, still frame, format and full screen on the
// right.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Vedit.Components
import Vedit.Theme
import Vedit.UI

Item {
    id: panel

    required property Editor editor
    readonly property TimelinePlayer player: editor.player
    property bool showScopes: false
    signal fullScreenRequested()
    signal frameRequested()
    signal templateMediaRequested()

    component Tool: IconButton {
        implicitWidth: Theme.editor.toolButtonSize
        implicitHeight: Theme.editor.toolButtonSize
        iconSize: Theme.editor.toolIconSize
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Header: what this panel is, and the format of the video.
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.editor.panelHeaderHeight
            Layout.leftMargin: Theme.space.md
            Layout.rightMargin: Theme.space.sm
            spacing: Theme.space.sm
            Label {
                Layout.fillWidth: true
                role: "titleSmall"
                text: qsTr("Player")
            }
            Label {
                visible: panel.player.preparingReverse
                role: "labelSmall"
                color: Theme.color.onSurfaceVariant
                text: qsTr("Preparing the reversed clip… %1%").arg(Math.round(panel.player.reverseProgress * 100))
            }
            Label {
                role: "labelSmall"
                font.features: { "tnum": 1 }
                color: Theme.color.onSurfaceVariant
                text: qsTr("%1 × %2 · %3 fps").arg(panel.editor.canvasSize.width).arg(panel.editor.canvasSize.height)
                          .arg(Math.round(panel.editor.frameRate * 100) / 100)
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.leftMargin: Theme.space.sm
            Layout.rightMargin: Theme.space.sm

            VideoPreview {
                id: video
                anchors.fill: parent
                sink: panel.player.sink
                backgroundColor: Theme.color.panel
            }
            CanvasHandles {
                anchors.fill: parent
                editor: panel.editor
            }
            // Before the first clip: say what to do, in the place where the result will appear.
            ColumnLayout {
                anchors.centerIn: parent
                width: Math.min(parent.width - 2 * Theme.space.xl, Theme.editor.dialogWidth)
                visible: panel.editor.timeline.duration === 0
                spacing: Theme.space.sm
                Icon {
                    Layout.alignment: Qt.AlignHCenter
                    name: "smart_display"
                    size: Theme.space.xxxl
                    color: Theme.color.outline
                }
                Label {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    role: "bodyMedium"
                    color: Theme.color.onSurfaceVariant
                    text: qsTr("Add a video or a photo to the timeline: the preview appears here.")
                }
            }

            // A template with shots still empty: what to do next, where the result will appear.
            Rectangle {
                objectName: "templateBanner"
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.top: parent.top
                anchors.topMargin: Theme.space.sm
                visible: panel.editor.placeholderCount > 0
                width: Math.min(parent.width - 2 * Theme.space.md, bannerRow.implicitWidth + 2 * Theme.space.md)
                height: bannerRow.implicitHeight + 2 * Theme.space.sm
                radius: Theme.shape.medium
                color: Theme.color.inverseSurface
                RowLayout {
                    id: bannerRow
                    anchors.fill: parent
                    anchors.leftMargin: Theme.space.md
                    anchors.rightMargin: Theme.space.sm
                    spacing: Theme.space.md
                    Icon {
                        name: "photo_library"
                        size: Theme.editor.toolIconSize
                        color: Theme.color.inversePrimary
                    }
                    Label {
                        Layout.fillWidth: true
                        role: "bodyMedium"
                        elide: Text.ElideRight
                        color: Theme.color.inverseOnSurface
                        text: qsTr("%n shot(s) of the template to fill with your videos and photos", "", panel.editor.placeholderCount)
                    }
                    Button {
                        objectName: "chooseTemplateMedia"
                        implicitHeight: Theme.editor.toolButtonSize
                        variant: "filled"
                        iconName: "add_photo_alternate"
                        text: qsTr("Choose")
                        onClicked: panel.templateMediaRequested()
                    }
                }
            }

            ScopePanel {
                id: scopeOverlay
                objectName: "scopePanel"
                anchors.left: parent.left
                anchors.bottom: parent.bottom
                anchors.right: parent.right
                height: Math.min(parent.height * 0.5, Theme.editor.timelineDefaultHeight * 0.75)
                visible: panel.showScopes
                sink: panel.player.sink
                onCloseRequested: panel.showScopes = false
            }
        }

        // Transport: position / duration on the left, buttons in the centre, tools on the right, at any width.
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.editor.toolbarHeight
            Layout.leftMargin: Theme.space.md
            Layout.rightMargin: Theme.space.sm

            Row {
                id: times
                anchors { left: parent.left; verticalCenter: parent.verticalCenter }
                spacing: Theme.space.xs
                Label {
                    role: "labelLarge"
                    font.features: { "tnum": 1 }
                    text: panel.player.timecode(panel.player.skimming ? panel.player.shownPosition : panel.player.position)
                    color: panel.player.skimming ? Theme.color.onSurfaceVariant : Theme.color.primary
                    Accessible.name: qsTr("Position")
                }
                Label {
                    role: "labelLarge"
                    color: Theme.color.outline
                    text: "/"
                }
                Label {
                    id: durationLabel
                    role: "labelLarge"
                    font.features: { "tnum": 1 }
                    color: Theme.color.onSurfaceVariant
                    text: panel.player.rate !== 0 && panel.player.rate !== 1
                          ? qsTr("%1 · %2×").arg(panel.player.timecode(panel.editor.timeline.duration)).arg(panel.player.rate)
                          : panel.player.timecode(panel.editor.timeline.duration)
                    Accessible.name: qsTr("Duration")
                }
            }
            // Centred, moved aside when the player is too narrow for the times and the tools around it.
            Row {
                id: buttons
                anchors.verticalCenter: parent.verticalCenter
                x: Math.max(times.width + Theme.space.sm, Math.min((parent.width - width) / 2, tools.x - width - Theme.space.sm))
                spacing: Theme.space.sm
                Tool {
                    anchors.verticalCenter: parent.verticalCenter
                    iconName: "skip_previous"
                    label: qsTr("Go to start")
                    shortcutText: "Home"
                    onClicked: panel.player.seek(0)
                }
                IconButton {
                    objectName: "playButton"
                    anchors.verticalCenter: parent.verticalCenter
                    implicitWidth: Theme.editor.playButtonSize
                    implicitHeight: Theme.editor.playButtonSize
                    variant: "filled"
                    iconName: panel.player.playing ? "pause" : "play_arrow"
                    label: panel.player.playing ? qsTr("Pause") : qsTr("Play")
                    shortcutText: qsTr("Space · J K L")
                    enabled: panel.editor.timeline.duration > 0
                    onClicked: panel.player.togglePlay()
                }
                Tool {
                    anchors.verticalCenter: parent.verticalCenter
                    iconName: "skip_next"
                    label: qsTr("Go to end")
                    shortcutText: qsTr("End")
                    onClicked: panel.player.seek(panel.editor.timeline.duration)
                }
            }
            Row {
                id: tools
                anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                spacing: Theme.space.xxs
                Tool {
                    id: scopesBtn
                    objectName: "scopesButton"
                    anchors.verticalCenter: parent.verticalCenter
                    checkable: true
                    checked: panel.showScopes
                    iconName: "monitoring"
                    label: qsTr("Video scopes (histogram, waveform, vectorscope)")
                    onClicked: panel.showScopes = !panel.showScopes
                }
                // The frame on screen as an image (SPEC §5.15): cover, thumbnail, a still to share.
                Tool {
                    objectName: "exportFrameButton"
                    anchors.verticalCenter: parent.verticalCenter
                    iconName: "photo_camera"
                    label: qsTr("Save the current frame as an image")
                    shortcutText: qsTr("Ctrl+Shift+E")
                    enabled: panel.editor.timeline.duration > 0
                    onClicked: panel.frameRequested()
                }
                // Format of the canvas under the player (SPEC §4), one click away (SPEC 0bis rule 1).
                FormatButton {
                    anchors.verticalCenter: parent.verticalCenter
                    editor: panel.editor
                }
                Tool {
                    objectName: "fullScreenButton"
                    anchors.verticalCenter: parent.verticalCenter
                    iconName: "fullscreen"
                    label: qsTr("Full screen")
                    shortcutText: "F11"
                    enabled: panel.editor.timeline.duration > 0
                    onClicked: panel.fullScreenRequested()
                }
            }
        }
    }
}
