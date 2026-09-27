// Export on one screen (SPEC §5.15, usability test 8: "Export" twice). Recommended settings = the project's; name,
// folder, resolution, frame rate and quality in plain words, with the expected duration and size. While exporting
// the editor stays usable; at the end: open the folder, play, copy the path.
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
    objectName: "exportDialog"

    required property Editor editor
    readonly property RenderJob job: editor.exportJob
    // "settings", "running", "done", "failed"
    property string stage: "settings"
    property var defaults: ({})
    property string outputPath
    property string errorText
    property string errorDetail

    function openDialog() {
        if (!job.running && stage !== "done" && stage !== "failed") {
            defaults = editor.exportDefaults()
            nameField.text = defaults.fileName
            folder = defaults.folder
            resolution.currentIndex = Math.max(0, defaults.resolutions.findIndex(r => r.value === defaults.resolution))
            frameRate.currentIndex = Math.max(0, defaults.frameRates.findIndex(r => r.value === defaults.frameRate))
            quality.currentIndex = defaults.quality
            stage = "settings"
        }
        open()
    }
    property string folder
    readonly property int shortSide: defaults.resolutions ? defaults.resolutions[resolution.currentIndex].value : 1080
    readonly property string rate: defaults.frameRates ? defaults.frameRates[frameRate.currentIndex].value : "30"

    title: stage === "done" ? qsTr("Your video is ready")
         : stage === "failed" ? qsTr("The export did not finish")
         : stage === "running" ? qsTr("Exporting…") : qsTr("Export")
    iconName: stage === "done" ? "check_circle" : stage === "failed" ? "error" : "file_upload"
    modal: true

    Connections {
        target: dialog.job
        function onFinished(path) { dialog.outputPath = path; dialog.stage = "done"; dialog.open() }
        function onFailed(message, detail) {
            if (dialog.stage !== "running")
                return // refused before starting: already said by a snackbar
            dialog.errorText = message
            dialog.errorDetail = detail
            dialog.stage = "failed"
            dialog.open()
        }
        function onCancelled() { dialog.stage = "settings" }
    }

    FolderDialog {
        id: folderDialog
        title: qsTr("Choose where to save the video")
        currentFolder: App.fileUrl(dialog.folder)
        onAccepted: dialog.folder = dialog.editor.folderPath(selectedFolder)
    }

    contentItem: StackLayout {
        currentIndex: ["settings", "running", "done", "failed"].indexOf(dialog.stage)
        implicitHeight: children[currentIndex].implicitHeight

        // ---- settings ----
        ColumnLayout {
            spacing: Theme.space.lg
            TextField {
                id: nameField
                Layout.fillWidth: true
                label: qsTr("File name")
                onAccepted: exportButton.clicked()
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.space.sm
                Icon { name: "folder"; color: Theme.color.onSurfaceVariant }
                Label {
                    Layout.fillWidth: true
                    role: "bodyMedium"
                    elide: Text.ElideMiddle
                    text: dialog.folder
                }
                Button {
                    variant: "text"
                    text: qsTr("Change")
                    onClicked: folderDialog.open()
                }
            }
            Label { role: "labelLarge"; text: qsTr("Resolution") }
            SegmentedButton {
                id: resolution
                model: (dialog.defaults.resolutions ?? []).map(r => ({ text: r.label }))
            }
            Label { role: "labelLarge"; text: qsTr("Frame rate") }
            SegmentedButton {
                id: frameRate
                model: (dialog.defaults.frameRates ?? []).map(r => ({ text: r.label }))
            }
            Label { role: "labelLarge"; text: qsTr("Quality") }
            SegmentedButton {
                id: quality
                model: [{ text: qsTr("Low") }, { text: qsTr("Recommended") }, { text: qsTr("High") }]
            }
            Label {
                Layout.fillWidth: true
                role: "bodyMedium"
                color: Theme.color.onSurfaceVariant
                wrapMode: Text.WordWrap
                text: dialog.defaults.resolutions ? dialog.editor.exportEstimate(dialog.shortSide, dialog.rate, quality.currentIndex) : ""
            }
            CheckBox {
                id: normalizeAudio
                objectName: "normalizeAudioCheck"
                text: qsTr("Normalize loudness to −14 LUFS (EBU R128)")
                checked: false
            }
            RowLayout {
                Layout.fillWidth: true
                Label {
                    Layout.fillWidth: true
                    role: "bodySmall"
                    color: Theme.color.onSurfaceVariant
                    text: qsTr("MP4 video (H.264 and AAC): plays everywhere.")
                    wrapMode: Text.WordWrap
                }
                Button {
                    variant: "text"
                    text: qsTr("Cancel")
                    onClicked: dialog.close()
                }
                Button {
                    id: exportButton
                    objectName: "exportConfirmButton"
                    variant: "filled"
                    iconName: "file_upload"
                    text: qsTr("Export")
                    onClicked: {
                        if (dialog.editor.startExport(nameField.text, dialog.folder, dialog.shortSide, dialog.rate,
                                                      quality.currentIndex, normalizeAudio.checked, -14.0))
                            dialog.stage = "running"
                        else
                            dialog.close()
                    }
                }
            }
        }

        // ---- running ----
        ColumnLayout {
            spacing: Theme.space.lg
            ProgressBar {
                Layout.fillWidth: true
                value: dialog.job.progress
            }
            Label {
                Layout.fillWidth: true
                role: "bodyLarge"
                font.features: { "tnum": 1 }
                text: dialog.job.secondsLeft >= 0
                      ? qsTr("%1% · about %2 s left").arg(Math.floor(dialog.job.progress * 100)).arg(dialog.job.secondsLeft)
                      : qsTr("%1%").arg(Math.floor(dialog.job.progress * 100))
            }
            Label {
                Layout.fillWidth: true
                role: "bodySmall"
                color: Theme.color.onSurfaceVariant
                wrapMode: Text.WordWrap
                text: qsTr("You can keep editing: the export uses the project as it was when you started it.")
            }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                Button {
                    variant: "text"
                    text: qsTr("Stop export")
                    onClicked: dialog.job.cancel()
                }
                Button {
                    variant: "tonal"
                    text: qsTr("Keep editing")
                    onClicked: dialog.close()
                }
            }
        }

        // ---- done ----
        ColumnLayout {
            spacing: Theme.space.lg
            Label {
                Layout.fillWidth: true
                role: "bodyMedium"
                elide: Text.ElideMiddle
                text: dialog.outputPath
            }
            Flow {
                Layout.fillWidth: true
                spacing: Theme.space.sm
                Button {
                    variant: "filled"
                    iconName: "play_arrow"
                    text: qsTr("Play")
                    onClicked: App.openFile(dialog.outputPath)
                }
                Button {
                    variant: "tonal"
                    iconName: "folder_open"
                    text: qsTr("Open folder")
                    onClicked: App.openFolderOf(dialog.outputPath)
                }
                Button {
                    variant: "tonal"
                    iconName: "content_copy"
                    text: qsTr("Copy path")
                    onClicked: App.copyText(dialog.outputPath)
                }
                Button {
                    variant: "text"
                    text: qsTr("Close")
                    onClicked: { dialog.stage = "settings"; dialog.close() }
                }
            }
        }

        // ---- failed ----
        ColumnLayout {
            spacing: Theme.space.lg
            Label {
                Layout.fillWidth: true
                role: "bodyLarge"
                wrapMode: Text.WordWrap
                text: dialog.errorText
            }
            RowLayout {
                Layout.fillWidth: true
                Button {
                    variant: "text"
                    iconName: "content_copy"
                    text: qsTr("Copy details")
                    visible: dialog.errorDetail !== ""
                    onClicked: App.copyText(dialog.errorText + "\n" + dialog.errorDetail)
                }
                Item { Layout.fillWidth: true }
                Button {
                    variant: "text"
                    text: qsTr("Close")
                    onClicked: { dialog.stage = "settings"; dialog.close() }
                }
                Button {
                    variant: "filled"
                    text: qsTr("Try again")
                    onClicked: { dialog.stage = "settings"; dialog.openDialog() }
                }
            }
        }
    }
}
