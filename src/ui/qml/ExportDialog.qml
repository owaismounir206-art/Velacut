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
            codec.currentIndex = Math.max(0, codecModel.findIndex(c => c.value === defaults.codec))
            hardwareUse.checked = defaults.hardware
            maxSize.currentIndex = Math.max(0, sizeModel.findIndex(s => s.value === defaults.maxFileSizeMB))
            stage = "settings"
        }
        open()
    }
    property string folder
    readonly property int shortSide: defaults.resolutions ? defaults.resolutions[resolution.currentIndex].value : 1080
    readonly property string rate: defaults.frameRates ? defaults.frameRates[frameRate.currentIndex].value : "30"
    // Advanced (SPEC §5.15): everything technical closed by default.
    property bool advancedOpen: false
    readonly property var codecModel: [
        { value: "h264", label: qsTr("H.264 — plays everywhere") },
        { value: "hevc", label: qsTr("H.265 — smaller file") },
        { value: "av1", label: qsTr("AV1 — smallest file (new)") }]
    readonly property var sizeModel: [
        { value: 0, label: qsTr("No size limit") },
        { value: 16, label: qsTr("Under 16 MB (e-mail)") },
        { value: 25, label: qsTr("Under 25 MB (WhatsApp)") },
        { value: 50, label: qsTr("Under 50 MB (Telegram)") },
        { value: 100, label: qsTr("Under 100 MB (Discord)") }]
    readonly property var platformModel: [
        { label: qsTr("YouTube"), shortSide: 1080 },
        { label: qsTr("TikTok"), shortSide: 1080 },
        { label: qsTr("Reels"), shortSide: 1080 },
        { label: qsTr("Shorts"), shortSide: 1080 },
        { label: qsTr("X"), shortSide: 720 }]
    readonly property string codecValue: codecModel[Math.max(0, codec.currentIndex)].value
    readonly property int maxSizeValue: sizeModel[Math.max(0, maxSize.currentIndex)].value

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
            // One click per platform (SPEC §5.15): sets the resolution the platform likes.
            Flow {
                Layout.fillWidth: true
                spacing: Theme.space.sm
                Repeater {
                    model: dialog.platformModel
                    Chip {
                        variant: "assist"
                        text: modelData.label
                        onClicked: {
                            const index = dialog.defaults.resolutions.findIndex(r => r.value === modelData.shortSide)
                            if (index >= 0)
                                resolution.currentIndex = index
                        }
                    }
                }
            }
            Label {
                Layout.fillWidth: true
                role: "bodyMedium"
                color: Theme.color.onSurfaceVariant
                wrapMode: Text.WordWrap
                text: dialog.defaults.resolutions
                      ? dialog.editor.exportEstimate(dialog.shortSide, dialog.rate, quality.currentIndex,
                                                     dialog.codecValue, dialog.maxSizeValue)
                      : ""
            }
            CheckBox {
                id: normalizeAudio
                objectName: "normalizeAudioCheck"
                text: qsTr("Normalize loudness to −14 LUFS (EBU R128)")
                checked: false
            }

            // ---- advanced (closed by default, SPEC 0bis rules 7 and 8) ----
            Button {
                variant: "text"
                iconName: dialog.advancedOpen ? "expand_less" : "expand_more"
                text: qsTr("Advanced")
                onClicked: dialog.advancedOpen = !dialog.advancedOpen
            }
            ColumnLayout {
                visible: dialog.advancedOpen
                Layout.fillWidth: true
                spacing: Theme.space.md
                Label { role: "labelLarge"; text: qsTr("Video codec") }
                ComboBox {
                    id: codec
                    Layout.fillWidth: true
                    model: dialog.codecModel
                    textRole: "label"
                    valueRole: "value"
                }
                RowLayout {
                    Layout.fillWidth: true
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0
                        Label {
                            Layout.fillWidth: true
                            role: "bodyMedium"
                            text: qsTr("Use hardware acceleration")
                        }
                        Label {
                            Layout.fillWidth: true
                            role: "bodySmall"
                            color: Theme.color.onSurfaceVariant
                            elide: Text.ElideMiddle
                            visible: dialog.defaults.hardwareAvailable
                            text: dialog.defaults.gpuName ?? ""
                        }
                        Label {
                            Layout.fillWidth: true
                            role: "bodySmall"
                            color: Theme.color.onSurfaceVariant
                            visible: !dialog.defaults.hardwareAvailable
                            text: qsTr("No GPU encoder was found on this computer: the export uses the processor.")
                        }
                    }
                    Switch {
                        id: hardwareUse
                        objectName: "hardwareEncodeSwitch"
                        enabled: dialog.defaults.hardwareAvailable
                    }
                }
                Label { role: "labelLarge"; text: qsTr("Maximum file size") }
                ComboBox {
                    id: maxSize
                    Layout.fillWidth: true
                    model: dialog.sizeModel
                    textRole: "label"
                    valueRole: "value"
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Label {
                    Layout.fillWidth: true
                    role: "bodySmall"
                    color: Theme.color.onSurfaceVariant
                    wrapMode: Text.WordWrap
                    text: dialog.codecValue === "h264" ? qsTr("MP4 video (H.264 and AAC): plays everywhere.")
                         : dialog.codecValue === "hevc" ? qsTr("MP4 video (H.265 and AAC): smaller files, recent players.")
                         : qsTr("MP4 video (AV1 and AAC): the smallest files, newest players.")
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
                                                      quality.currentIndex, normalizeAudio.checked, -14.0,
                                                      dialog.codecValue, hardwareUse.checked, dialog.maxSizeValue))
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
