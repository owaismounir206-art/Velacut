// Local music library (usability test 2: music under the video in 2 actions: "Audio", then "+").
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
    readonly property AudioLibraryModel library: App.audioLibrary

    color: Theme.color.surface

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.space.md
        spacing: Theme.space.sm

        RowLayout {
            Layout.fillWidth: true
            Label {
                Layout.fillWidth: true
                role: "titleMedium"
                text: qsTr("Music")
            }
            BusyIndicator {
                Layout.preferredWidth: Theme.space.xl
                Layout.preferredHeight: Theme.space.xl
                running: panel.library.loading
                visible: running
            }
            IconButton {
                iconName: "refresh"
                label: qsTr("Look for new music")
                onClicked: panel.library.reload()
            }
        }
        Label {
            Layout.fillWidth: true
            role: "bodySmall"
            color: Theme.color.onSurfaceVariant
            elide: Text.ElideMiddle
            text: qsTr("From %1").arg(panel.library.folder)
        }

        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: panel.library
            ScrollBar.vertical: ScrollBar {}
            delegate: ItemDelegate {
                id: row
                required property int index
                required property string name
                required property string durationText
                required property bool ready
                width: ListView.view.width
                contentItem: RowLayout {
                    spacing: Theme.space.md
                    Icon {
                        name: "music_note"
                        color: Theme.color.onSurfaceVariant
                    }
                    Label {
                        Layout.fillWidth: true
                        role: "bodyMedium"
                        elide: Text.ElideRight
                        text: row.name
                    }
                    Label {
                        role: "labelMedium"
                        font.features: { "tnum": 1 }
                        color: Theme.color.onSurfaceVariant
                        text: row.durationText
                    }
                    IconButton {
                        variant: "tonal"
                        iconName: "add"
                        enabled: row.ready
                        label: qsTr("Add under the video at the playhead")
                        onClicked: panel.editor.addFromLibrary(panel.library, row.index)
                    }
                }
            }
            Label {
                anchors.centerIn: parent
                width: parent.width - 2 * Theme.space.lg
                visible: list.count === 0 && !panel.library.loading
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                role: "bodyMedium"
                color: Theme.color.onSurfaceVariant
                text: qsTr("No music in this folder. Songs you put there appear here.")
            }
        }
    }
}
