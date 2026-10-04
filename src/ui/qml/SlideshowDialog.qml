// "Slideshow from photos" (SPEC §5.13bis): choose the photos, a song (from the music folder or a file, or none) and a
// style; the project is made at once (camera moves, transitions, look, music fitted to the length, changes on the beat)
// and stays editable like any other.
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

    title: qsTr("Slideshow from photos")
    iconName: "slideshow"
    modal: true

    property var photos: []
    property url music: ""
    property string musicName: ""
    property bool musicFromFile: false // chosen with "Other…" rather than from the music folder
    property int style: 0
    property bool onBeat: true

    function clearChoices() {
        photos = []
        music = ""
        musicName = ""
        musicFromFile = false
        style = 0
        onBeat = true
    }
    onOpened: App.audioLibrary.load()

    footer: DialogButtonBox {
        Button {
            variant: "text"
            text: qsTr("Cancel")
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
        }
        Button {
            objectName: "createSlideshow"
            variant: "filled"
            iconName: "auto_awesome"
            text: qsTr("Create")
            enabled: dialog.photos.length > 0
            DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
        }
    }
    onAccepted: {
        // The home screen (and this dialog) goes away when the project opens: the choices are taken first.
        const chosen = { photos: photos, music: music, style: style, onBeat: onBeat && musicName !== "" }
        clearChoices()
        App.newSlideshow(chosen.photos, chosen.music, chosen.style, chosen.onBeat)
    }
    onRejected: clearChoices()

    ColumnLayout {
        width: parent.width
        spacing: Theme.space.lg

        // 1. The photos.
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.space.md
            Button {
                objectName: "choosePhotos"
                variant: dialog.photos.length > 0 ? "tonal" : "filled"
                iconName: "add_photo_alternate"
                text: dialog.photos.length > 0 ? qsTr("Change the photos") : qsTr("Choose the photos")
                onClicked: photoDialog.open()
            }
            Label {
                Layout.fillWidth: true
                role: "bodyMedium"
                color: Theme.color.onSurfaceVariant
                text: dialog.photos.length > 0 ? qsTr("%n photo(s), in this order", "", dialog.photos.length)
                                               : qsTr("They follow the order of their names.")
            }
        }
        Flow {
            Layout.fillWidth: true
            visible: dialog.photos.length > 0
            spacing: Theme.space.xs
            Repeater {
                model: dialog.photos.slice(0, 14)
                delegate: Rectangle {
                    required property url modelData
                    width: Theme.editor.assetTileHeight * 0.75
                    height: width
                    radius: Theme.shape.small
                    color: Theme.color.surfaceContainerHighest
                    clip: true
                    Image {
                        anchors.fill: parent
                        source: parent.modelData
                        sourceSize: Qt.size(width * 2, height * 2)
                        fillMode: Image.PreserveAspectCrop
                        asynchronous: true
                    }
                }
            }
            Label {
                visible: dialog.photos.length > 14
                role: "labelLarge"
                color: Theme.color.onSurfaceVariant
                text: qsTr("+%1").arg(dialog.photos.length - 14)
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
                visible: dialog.musicName !== ""
                role: "bodySmall"
                color: Theme.color.onSurfaceVariant
                elide: Text.ElideMiddle
                text: qsTr("Chosen: %1 — cut to the length of the slideshow, with a fade at the end").arg(dialog.musicName)
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
                    model: [{ icon: "blur_on", text: qsTr("Soft"), detail: qsTr("Slow dissolves, 3 s per photo") },
                            { icon: "bolt", text: qsTr("Dynamic"), detail: qsTr("Quick pushes, vivid colours, 2 s") },
                            { icon: "photo_album", text: qsTr("Memories"), detail: qsTr("Film look, long dissolves") },
                            { icon: "movie_filter", text: qsTr("Cinematic"), detail: qsTr("Teal and orange, fades to black") }]
                    delegate: Rectangle {
                        id: styleCard
                        required property var modelData
                        required property int index
                        objectName: "slideshowStyle_" + index
                        Layout.fillWidth: true
                        implicitHeight: styleRow.implicitHeight + 2 * Theme.space.md
                        radius: Theme.shape.medium
                        color: dialog.style === index ? Theme.color.secondaryContainer : Theme.color.surfaceContainerHighest
                        border.width: dialog.style === index ? Theme.editor.selectionBorder : 0
                        border.color: Theme.color.primary
                        RowLayout {
                            id: styleRow
                            anchors.fill: parent
                            anchors.margins: Theme.space.md
                            spacing: Theme.space.md
                            Icon {
                                name: styleCard.modelData.icon
                                color: dialog.style === styleCard.index ? Theme.color.onSecondaryContainer : Theme.color.onSurfaceVariant
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 0
                                Label {
                                    Layout.fillWidth: true
                                    role: "labelLarge"
                                    text: styleCard.modelData.text
                                }
                                Label {
                                    Layout.fillWidth: true
                                    role: "bodySmall"
                                    color: Theme.color.onSurfaceVariant
                                    elide: Text.ElideRight
                                    text: styleCard.modelData.detail
                                }
                            }
                        }
                        StateLayer {
                            radius: parent.radius
                            color: Theme.color.onSurface
                            hovered: styleMouse.containsMouse
                            pressed: styleMouse.pressed
                            pressPoint: Qt.point(styleMouse.mouseX, styleMouse.mouseY)
                        }
                        MouseArea {
                            id: styleMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: dialog.style = styleCard.index
                        }
                        Accessible.role: Accessible.RadioButton
                        Accessible.name: modelData.text
                        Accessible.checked: dialog.style === index
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            enabled: dialog.musicName !== ""
            Label {
                Layout.fillWidth: true
                role: "bodyMedium"
                wrapMode: Text.WordWrap
                text: qsTr("Change photo on the beat of the music")
            }
            Switch {
                checked: dialog.onBeat
                Accessible.name: qsTr("Change photo on the beat of the music")
                onToggled: dialog.onBeat = checked
            }
        }
    }

    FileDialog {
        id: photoDialog
        title: qsTr("Choose the photos")
        fileMode: FileDialog.OpenFiles
        nameFilters: [qsTr("Photos (%1)").arg("*.jpg *.jpeg *.png *.webp *.heic *.bmp *.tif *.tiff *.gif"), qsTr("All files (*)")]
        onAccepted: {
            // In the order of their names: the order of a camera's files, usually the order they were taken.
            const files = Array.from(selectedFiles)
            files.sort((a, b) => a.toString().localeCompare(b.toString(), undefined, { numeric: true }))
            dialog.photos = files
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
