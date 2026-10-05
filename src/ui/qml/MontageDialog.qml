// "Automatic montage" (SPEC §5.13bis): videos and photos, a song if you like, a style and a length; the best moments are
// cut on the beat with the style's look, and the result is an ordinary project ("Shuffle" in the editor for another one).
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import Vedit.Components
import Vedit.Theme
import Vedit.UI

Dialog {
    id: dialog

    title: qsTr("Automatic montage")
    iconName: "movie_edit"
    modal: true

    property var files: []
    property url music: ""
    property string musicName: ""
    property bool musicFromFile: false
    property string style: "vlog"
    property int seconds: 30

    function clearChoices() {
        files = []
        music = ""
        musicName = ""
        musicFromFile = false
        style = "vlog"
        seconds = 30
    }
    onOpened: App.audioLibrary.load()

    footer: DialogButtonBox {
        Button {
            variant: "text"
            text: qsTr("Cancel")
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
        }
        Button {
            objectName: "createMontage"
            variant: "filled"
            iconName: "auto_awesome"
            text: qsTr("Create")
            enabled: dialog.files.length > 0
            DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
        }
    }
    onAccepted: {
        // The home screen (and this dialog) goes away when the project opens: the choices are taken first.
        const chosen = { files: files, music: music, style: style, seconds: seconds }
        clearChoices()
        App.newMontage(chosen.files, chosen.music, chosen.style, chosen.seconds)
    }
    onRejected: clearChoices()

    ColumnLayout {
        width: parent.width
        spacing: Theme.space.lg

        // 1. The shots.
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.space.md
            Button {
                objectName: "chooseMontageFiles"
                variant: dialog.files.length > 0 ? "tonal" : "filled"
                iconName: "video_library"
                text: dialog.files.length > 0 ? qsTr("Change the videos and photos") : qsTr("Choose videos and photos")
                onClicked: fileDialog.open()
            }
            Label {
                Layout.fillWidth: true
                role: "bodyMedium"
                color: Theme.color.onSurfaceVariant
                wrapMode: Text.WordWrap
                text: dialog.files.length > 0 ? qsTr("%n file(s): the best moments of each are used", "", dialog.files.length)
                                              : qsTr("As many as you like: vedit picks the best moments.")
            }
        }

        // 2. The music.
        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.space.sm
            Label { role: "titleSmall"; text: qsTr("Music") }
            Flow {
                Layout.fillWidth: true
                spacing: Theme.space.xs
                Chip {
                    text: qsTr("None")
                    checkable: false
                    checked: dialog.musicName === ""
                    onClicked: { dialog.music = ""; dialog.musicName = ""; dialog.musicFromFile = false }
                }
                Repeater {
                    model: App.audioLibrary
                    delegate: Chip {
                        required property string path
                        required property string name
                        required property int index
                        visible: index < 6
                        iconName: "music_note"
                        text: name
                        checkable: false
                        checked: dialog.music.toString() === App.fileUrl(path).toString()
                        onClicked: { dialog.music = App.fileUrl(path); dialog.musicName = name; dialog.musicFromFile = false }
                    }
                }
                Chip {
                    iconName: "folder_open"
                    text: dialog.musicFromFile ? dialog.musicName : qsTr("Other…")
                    checkable: false
                    checked: dialog.musicFromFile
                    onClicked: musicDialog.open()
                }
            }
            Label {
                Layout.fillWidth: true
                role: "bodySmall"
                color: Theme.color.onSurfaceVariant
                wrapMode: Text.WordWrap
                text: dialog.musicName !== "" ? qsTr("The shots change on its beat.") : qsTr("Without music the shots change at a steady pace.")
            }
        }

        // 3. The style.
        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.space.sm
            Label { role: "titleSmall"; text: qsTr("Style") }
            GridLayout {
                Layout.fillWidth: true
                columns: 2
                rowSpacing: Theme.space.sm
                columnSpacing: Theme.space.sm
                Repeater {
                    model: [{ id: "vlog", icon: "videocam", text: qsTr("Vlog"), detail: qsTr("Natural colours, quick cuts") },
                            { id: "travel", icon: "flight", text: qsTr("Travel"), detail: qsTr("Vivid colours, whip pans") },
                            { id: "sport", icon: "sports_soccer", text: qsTr("Sport"), detail: qsTr("Very quick cuts, zooms") },
                            { id: "cinematic", icon: "movie_filter", text: qsTr("Cinematic"), detail: qsTr("Long shots, dissolves, teal and orange") },
                            { id: "party", icon: "celebration", text: qsTr("Party"), detail: qsTr("Flashes on every beat, neon") },
                            { id: "meme", icon: "mood", text: qsTr("Meme"), detail: qsTr("Punchy cuts, comic title") },
                            { id: "product", icon: "shopping_bag", text: qsTr("Product"), detail: qsTr("Clean look, slides, call to action") }]
                    delegate: Rectangle {
                        id: card
                        required property var modelData
                        objectName: "montageStyle_" + modelData.id
                        readonly property bool chosen: dialog.style === modelData.id
                        Layout.fillWidth: true
                        implicitHeight: cardRow.implicitHeight + 2 * Theme.space.md
                        radius: Theme.shape.medium
                        color: chosen ? Theme.color.secondaryContainer : Theme.color.surfaceContainerHighest
                        border.width: chosen ? Theme.editor.selectionBorder : 0
                        border.color: Theme.color.primary
                        RowLayout {
                            id: cardRow
                            anchors.fill: parent
                            anchors.margins: Theme.space.md
                            spacing: Theme.space.md
                            Icon {
                                name: card.modelData.icon
                                color: card.chosen ? Theme.color.onSecondaryContainer : Theme.color.onSurfaceVariant
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 0
                                Label { Layout.fillWidth: true; role: "labelLarge"; text: card.modelData.text }
                                Label {
                                    Layout.fillWidth: true
                                    role: "bodySmall"
                                    color: Theme.color.onSurfaceVariant
                                    elide: Text.ElideRight
                                    text: card.modelData.detail
                                }
                            }
                        }
                        StateLayer {
                            radius: parent.radius
                            color: Theme.color.onSurface
                            hovered: cardMouse.containsMouse
                            pressed: cardMouse.pressed
                            pressPoint: Qt.point(cardMouse.mouseX, cardMouse.mouseY)
                        }
                        MouseArea {
                            id: cardMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: dialog.style = card.modelData.id
                        }
                        Accessible.role: Accessible.RadioButton
                        Accessible.name: modelData.text
                        Accessible.checked: chosen
                    }
                }
            }
        }

        // 4. The length.
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.space.sm
            Label { Layout.fillWidth: true; role: "titleSmall"; text: qsTr("Length") }
            Repeater {
                model: [{ value: 15, text: qsTr("15 s") }, { value: 30, text: qsTr("30 s") }, { value: 60, text: qsTr("60 s") },
                        { value: 0, text: qsTr("All") }]
                delegate: Chip {
                    required property var modelData
                    objectName: "montageLength_" + modelData.value
                    text: modelData.text
                    checkable: false
                    checked: dialog.seconds === modelData.value
                    onClicked: dialog.seconds = modelData.value
                }
            }
        }
    }

    FileDialog {
        id: fileDialog
        title: qsTr("Choose videos and photos")
        fileMode: FileDialog.OpenFiles
        nameFilters: [qsTr("Videos and photos (%1)").arg("*.mp4 *.mov *.mkv *.webm *.avi *.m4v *.jpg *.jpeg *.png *.webp *.heic"),
                      qsTr("All files (*)")]
        onAccepted: {
            const chosen = Array.from(selectedFiles)
            chosen.sort((a, b) => a.toString().localeCompare(b.toString(), undefined, { numeric: true }))
            dialog.files = chosen
        }
    }
    FileDialog {
        id: musicDialog
        title: qsTr("Choose the music")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("Music (%1)").arg("*.mp3 *.wav *.flac *.aac *.ogg *.opus *.m4a"), qsTr("All files (*)")]
        onAccepted: {
            dialog.music = selectedFile
            dialog.musicName = App.localPath(selectedFile).split("/").pop()
            dialog.musicFromFile = true
        }
    }
}
