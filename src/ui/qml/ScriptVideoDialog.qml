// "Script to video" (SPEC §5.13bis): paste a text, choose the shape of the video and a song; every scene becomes a slot
// for your shots with its words as animated captions, read aloud when a voice is installed.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import Velacut.Components
import Velacut.Theme
import Velacut.UI

Dialog {
    id: dialog

    title: qsTr("Script to video")
    iconName: "article"
    modal: true

    property int preset: 1 // 9:16
    property url music: ""
    property string musicName: ""
    property bool musicFromFile: false
    // The voice that will read the script ("" = no voice installed).
    property string voice: ""

    function clearChoices() {
        script.text = ""
        preset = 1
        music = ""
        musicName = ""
        musicFromFile = false
    }
    onOpened: {
        App.audioLibrary.load()
        voice = App.voiceName()
        script.forceActiveFocus()
    }

    footer: DialogButtonBox {
        Button {
            variant: "text"
            text: qsTr("Cancel")
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
        }
        Button {
            objectName: "createScriptVideo"
            variant: "filled"
            iconName: "auto_awesome"
            text: qsTr("Create")
            enabled: script.text.trim() !== ""
            DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
        }
    }
    onAccepted: {
        const chosen = { text: script.text, preset: preset, music: music }
        clearChoices()
        App.newFromScript(chosen.text, chosen.preset, chosen.music)
    }
    onRejected: clearChoices()

    ColumnLayout {
        width: parent.width
        spacing: Theme.space.lg

        ScrollView {
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.editor.dialogWidth / 2.2
            TextArea {
                id: script
                objectName: "scriptText"
                wrapMode: TextEdit.Wrap
                placeholderText: qsTr("Paste or write the script: every paragraph becomes a scene.")
            }
        }
        Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            role: "bodySmall"
            color: Theme.color.onSurfaceVariant
            text: dialog.voice !== "" ? qsTr("Read aloud with the voice “%1”, with animated captions.").arg(dialog.voice)
                                      : qsTr("No voice installed (Preferences → AI models): the words appear as animated captions.")
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.space.sm
            Label { Layout.fillWidth: true; role: "titleSmall"; text: qsTr("Format") }
            Repeater {
                model: [{ preset: 1, text: qsTr("9:16") }, { preset: 0, text: qsTr("16:9") }, { preset: 2, text: qsTr("1:1") }]
                delegate: Chip {
                    required property var modelData
                    objectName: "scriptFormat_" + modelData.preset
                    text: modelData.text
                    checkable: false
                    checked: dialog.preset === modelData.preset
                    onClicked: dialog.preset = modelData.preset
                }
            }
        }

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
