// Media pool (SPEC §5.1): imported videos, photos and music. Pointing at a tile skims through it; "+" adds it at the
// playhead; tiles can be dragged onto the timeline; files dropped here are imported.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import Vedit.Components
import Vedit.Theme
import Vedit.UI

Rectangle {
    id: panel

    required property Editor editor

    function importFiles() { fileDialog.open() }

    color: Theme.color.surface

    FileDialog {
        id: fileDialog
        title: qsTr("Import media")
        fileMode: FileDialog.OpenFiles
        nameFilters: [qsTr("Videos, photos and music (%1)").arg("*.mp4 *.mov *.mkv *.webm *.avi *.mts *.m2ts *.mxf *.m4v *.3gp *.jpg *.jpeg *.png *.webp *.gif *.heic *.bmp *.tif *.tiff *.mp3 *.wav *.flac *.aac *.ogg *.opus *.m4a"),
                      qsTr("All files (*)")]
        onAccepted: panel.editor.importFiles(selectedFiles)
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.space.md
        spacing: Theme.space.md

        RowLayout {
            Layout.fillWidth: true
            Label {
                Layout.fillWidth: true
                role: "titleMedium"
                text: qsTr("Media")
            }
            BusyIndicator {
                Layout.preferredWidth: Theme.space.xl
                Layout.preferredHeight: Theme.space.xl
                running: panel.editor.importing
                visible: running
            }
            Button {
                variant: "tonal"
                iconName: "add"
                text: qsTr("Import")
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Import videos, photos and music (Ctrl+I)")
                onClicked: panel.importFiles()
            }
            Button {
                id: recordButton
                objectName: "recordButton"
                variant: "tonal"
                iconName: "fiber_manual_record"
                text: qsTr("Record")
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Record voiceover, screen or webcam")
                onClicked: recordMenu.open()

                Menu {
                    id: recordMenu
                    MenuItem {
                        text: qsTr("Voiceover")
                        icon.name: "mic"
                        onTriggered: panel.editor.startRecord(0)
                    }
                    MenuItem {
                        text: qsTr("Screen")
                        icon.name: "screen_record"
                        onTriggered: panel.editor.startRecord(1)
                    }
                    MenuItem {
                        text: qsTr("Webcam")
                        icon.name: "videocam"
                        onTriggered: panel.editor.startRecord(2)
                    }
                    MenuItem {
                        text: qsTr("Screen & Webcam")
                        icon.name: "picture_in_picture"
                        onTriggered: panel.editor.startRecord(3)
                    }
                }
            }
        }

        GridView {
            id: grid
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: panel.editor.media
            cellWidth: width / Math.max(1, Math.floor(width / Theme.editor.mediaTileWidth))
            cellHeight: Theme.editor.mediaTileHeight + Theme.space.xl + Theme.space.sm
            ScrollBar.vertical: ScrollBar {}
            delegate: MediaTile {
                editor: panel.editor
                width: grid.cellWidth - Theme.space.sm
            }

            // Empty: the whole panel is the import button.
            Rectangle {
                anchors.fill: parent
                visible: grid.count === 0
                radius: Theme.shape.medium
                color: emptyHover.hovered ? Theme.color.surfaceContainerHigh : Theme.color.surfaceContainerLow
                border.width: Theme.editor.selectionBorder
                border.color: Theme.color.outlineVariant
                HoverHandler { id: emptyHover; cursorShape: Qt.PointingHandCursor }
                TapHandler { onTapped: panel.importFiles() }
                ColumnLayout {
                    anchors.centerIn: parent
                    width: parent.width - 2 * Theme.space.lg
                    spacing: Theme.space.md
                    Icon {
                        Layout.alignment: Qt.AlignHCenter
                        name: "upload_file"
                        size: Theme.space.xxxl
                        color: Theme.color.primary
                    }
                    Label {
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                        role: "titleSmall"
                        text: qsTr("Import videos, photos and music")
                    }
                    Label {
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                        role: "bodySmall"
                        color: Theme.color.onSurfaceVariant
                        text: qsTr("Click here or drop files from the file manager")
                    }
                }
            }
        }
    }

    DropArea {
        anchors.fill: parent
        keys: ["text/uri-list"]
        onDropped: (drop) => {
            if (drop.hasUrls) {
                panel.editor.importFiles(drop.urls)
                drop.acceptProposedAction()
            }
        }
        Rectangle {
            anchors.fill: parent
            visible: parent.containsDrag
            color: Theme.alpha(Theme.color.primary, Theme.state.hover)
            border.width: Theme.editor.selectionBorder
            border.color: Theme.color.primary
        }
    }
}
