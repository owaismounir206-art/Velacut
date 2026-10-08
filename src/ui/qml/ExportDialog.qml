// Export on one screen (SPEC §5.15, usability test 8: "Export" twice). Recommended settings = the project's; name,
// folder, resolution, frame rate and quality in plain words, with the expected duration and size. While exporting
// the editor stays usable; at the end: open the folder, play, copy the path.
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
            quality.currentIndex = defaults.quality
            const saved = defaults.format
            kind = saved === "gif" ? 1 : audioFormats.some(f => f.value === saved) ? 2 : 0
            videoFormat.currentIndex = Math.max(0, videoFormats.findIndex(f => f.value === saved))
            audioFormat.currentIndex = Math.max(0, audioFormats.findIndex(f => f.value === saved))
            resetSizeAndRate()
            codec.currentIndex = Math.max(0, codecModel.findIndex(c => c.value === defaults.codec))
            hardwareUse.checked = defaults.hardware
            maxSize.currentIndex = Math.max(0, sizeModel.findIndex(s => s.value === defaults.maxFileSizeMB))
            captionsFile.checked = defaults.captionsFile
            onlyInOut.checked = editor.hasInOut
            stage = "settings"
        }
        open()
    }
    property string folder
    // What is made, in plain words (SPEC 0bis rule 7): 0 a video, 1 an animated GIF, 2 the sound alone. The file
    // formats of each are in "Advanced".
    property int kind: 0
    readonly property var videoFormats: [
        { value: "mp4", label: qsTr("MP4 — plays everywhere") },
        { value: "mov", label: qsTr("MOV — for editing in another program") },
        { value: "webm", label: qsTr("WebM — for web pages") },
        { value: "png", label: qsTr("PNG pictures — one per frame, in a folder") }]
    readonly property var audioFormats: [
        { value: "mp3", label: qsTr("MP3 — plays everywhere") },
        { value: "m4a", label: qsTr("M4A (AAC) — small, good quality") },
        { value: "wav", label: qsTr("WAV — not compressed") },
        { value: "flac", label: qsTr("FLAC — compressed without loss") }]
    readonly property string format: kind === 1 ? "gif"
                                   : kind === 2 ? audioFormats[Math.max(0, audioFormat.currentIndex)].value
                                   : videoFormats[Math.max(0, videoFormat.currentIndex)].value
    // A GIF is small and short: its own sizes and frame rates.
    // Never larger than the project (a GIF made bigger only weighs more).
    readonly property var gifResolutions: {
        const project = defaults.resolution ?? 720
        const sizes = [240, 360, 480, 720].filter(v => v <= project)
        if (sizes.length === 0 || sizes[sizes.length - 1] !== project && project < 720)
            sizes.push(project)
        return sizes.map(v => ({ label: v + "p", value: v }))
    }
    readonly property var gifRates: [{ label: "10", value: "10" }, { label: "15", value: "15" },
                                     { label: "20", value: "20" }, { label: "25", value: "25" }]
    readonly property var resolutionModel: kind === 1 ? gifResolutions : (defaults.resolutions ?? [])
    readonly property var rateModel: kind === 1 ? gifRates : (defaults.frameRates ?? [])
    readonly property int shortSide: resolutionModel.length > 0 ? resolutionModel[Math.min(resolution.currentIndex, resolutionModel.length - 1)].value : 1080
    readonly property string rate: rateModel.length > 0 ? rateModel[Math.min(frameRate.currentIndex, rateModel.length - 1)].value : "30"
    function resetSizeAndRate() {
        if (kind === 1) {
            resolution.currentIndex = Math.max(0, Math.min(gifResolutions.findIndex(r => r.value >= 480), gifResolutions.length - 1))
            if (gifResolutions.findIndex(r => r.value >= 480) < 0)
                resolution.currentIndex = gifResolutions.length - 1 // the project is smaller: its own size
            frameRate.currentIndex = 1  // 15 fps
        } else if (defaults.resolutions) {
            resolution.currentIndex = Math.max(0, defaults.resolutions.findIndex(r => r.value === defaults.resolution))
            frameRate.currentIndex = Math.max(0, defaults.frameRates.findIndex(r => r.value === defaults.frameRate))
        }
    }
    onKindChanged: resetSizeAndRate()
    // Advanced (SPEC §5.15): everything technical closed by default.
    property bool advancedOpen: false
    readonly property var allCodecs: [
        { value: "h264", label: qsTr("H.264 — plays everywhere"), formats: ["mp4", "mov"] },
        { value: "hevc", label: qsTr("H.265 — smaller file"), formats: ["mp4", "mov"] },
        { value: "av1", label: qsTr("AV1 — smallest file (new)"), formats: ["mp4", "webm"] },
        { value: "prores", label: qsTr("ProRes — for editing, very large"), formats: ["mov"] },
        { value: "vp9", label: qsTr("VP9 — for web pages"), formats: ["webm"] }]
    readonly property var codecModel: allCodecs.filter(c => c.formats.includes(format))
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
    readonly property string codecValue: codecModel.length > 0 ? codecModel[Math.min(Math.max(0, codec.currentIndex), codecModel.length - 1)].value : "h264"
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
            Label { role: "labelLarge"; text: qsTr("Export as") }
            SegmentedButton {
                id: kindChoice
                objectName: "exportKind"
                model: [{ text: qsTr("Video"), iconName: "movie" }, { text: qsTr("GIF"), iconName: "gif_box" },
                        { text: qsTr("Sound only"), iconName: "music_note" }]
                currentIndex: dialog.kind
                onActivated: (index) => dialog.kind = index
            }
            Label { role: "labelLarge"; text: qsTr("Resolution"); visible: dialog.kind !== 2 }
            SegmentedButton {
                id: resolution
                visible: dialog.kind !== 2
                model: dialog.resolutionModel.map(r => ({ text: r.label }))
            }
            Label { role: "labelLarge"; text: qsTr("Frame rate"); visible: dialog.kind !== 2 }
            SegmentedButton {
                id: frameRate
                visible: dialog.kind !== 2
                model: dialog.rateModel.map(r => ({ text: r.label }))
            }
            Label { role: "labelLarge"; text: qsTr("Quality"); visible: dialog.kind !== 1 && dialog.format !== "png" }
            SegmentedButton {
                id: quality
                visible: dialog.kind !== 1 && dialog.format !== "png"
                model: [{ text: qsTr("Low") }, { text: qsTr("Recommended") }, { text: qsTr("High") }]
            }
            // One click per platform (SPEC §5.15): sets the resolution the platform likes.
            Flow {
                Layout.fillWidth: true
                visible: dialog.kind === 0
                spacing: Theme.space.sm
                Repeater {
                    model: dialog.platformModel
                    Chip {
                        required property var modelData
                        variant: "assist"
                        text: modelData.label
                        onClicked: {
                            dialog.kind = 0
                            videoFormat.currentIndex = 0
                            const index = dialog.defaults.resolutions.findIndex(r => r.value === modelData.shortSide)
                            if (index >= 0)
                                resolution.currentIndex = index
                        }
                    }
                }
            }
            Label {
                objectName: "exportEstimate"
                Layout.fillWidth: true
                role: "bodyMedium"
                color: Theme.color.onSurfaceVariant
                wrapMode: Text.WordWrap
                text: dialog.defaults.resolutions
                      ? dialog.editor.exportEstimate(dialog.shortSide, dialog.rate, quality.currentIndex,
                                                     dialog.codecValue, dialog.maxSizeValue, dialog.format,
                                                     onlyInOut.visible && onlyInOut.checked)
                      : ""
            }
            CheckBox {
                id: onlyInOut
                objectName: "exportOnlyInOut"
                visible: dialog.editor.hasInOut
                text: qsTr("Only between the In and Out points")
            }
            CheckBox {
                id: captionsFile
                objectName: "exportCaptionsFile"
                visible: Boolean(dialog.defaults.hasCaptions) && dialog.kind === 0 && dialog.format !== "png"
                text: qsTr("Also save the captions as a file (SRT)")
                ToolTip.visible: hovered
                ToolTip.text: qsTr("The captions stay in the picture too; the file lets players and websites show them on their own.")
            }
            CheckBox {
                id: normalizeAudio
                objectName: "normalizeAudioCheck"
                visible: dialog.kind !== 1 && dialog.format !== "png"
                // Plain words here (SPEC 0bis rule 7); the measure is in the tooltip: −14 LUFS, EBU R128.
                text: qsTr("Even out the volume, as social networks want it")
                checked: false
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Loudness normalized to −14 LUFS (EBU R128), the level of YouTube, TikTok and Instagram.")
            }

            // ---- advanced (closed by default, SPEC 0bis rules 7 and 8) ----
            Button {
                objectName: "exportAdvancedButton"
                visible: dialog.kind !== 1
                variant: "text"
                iconName: dialog.advancedOpen ? "expand_less" : "expand_more"
                text: qsTr("Advanced")
                onClicked: dialog.advancedOpen = !dialog.advancedOpen
            }
            ColumnLayout {
                visible: dialog.advancedOpen && dialog.kind !== 1
                Layout.fillWidth: true
                spacing: Theme.space.md
                Label { role: "labelLarge"; text: qsTr("File format") }
                ComboBox {
                    id: videoFormat
                    objectName: "exportVideoFormat"
                    Layout.fillWidth: true
                    visible: dialog.kind === 0
                    model: dialog.videoFormats
                    textRole: "label"
                    valueRole: "value"
                }
                ComboBox {
                    id: audioFormat
                    objectName: "exportAudioFormat"
                    Layout.fillWidth: true
                    visible: dialog.kind === 2
                    model: dialog.audioFormats
                    textRole: "label"
                    valueRole: "value"
                }
                Label { role: "labelLarge"; text: qsTr("Video codec"); visible: codec.visible }
                ComboBox {
                    id: codec
                    Layout.fillWidth: true
                    visible: dialog.kind === 0 && dialog.codecModel.length > 1
                    model: dialog.codecModel
                    textRole: "label"
                    valueRole: "value"
                }
                RowLayout {
                    Layout.fillWidth: true
                    visible: dialog.kind === 0 && dialog.codecValue !== "prores" && dialog.format !== "png"
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
                            visible: Boolean(dialog.defaults && dialog.defaults.hardwareAvailable)
                            text: (dialog.defaults && dialog.defaults.gpuName) ? dialog.defaults.gpuName : ""
                        }
                        Label {
                            Layout.fillWidth: true
                            role: "bodySmall"
                            color: Theme.color.onSurfaceVariant
                            visible: !Boolean(dialog.defaults && dialog.defaults.hardwareAvailable)
                            text: qsTr("No GPU encoder was found on this computer: the export uses the processor.")
                        }
                    }
                    Switch {
                        id: hardwareUse
                        objectName: "hardwareEncodeSwitch"
                        enabled: Boolean(dialog.defaults && dialog.defaults.hardwareAvailable)
                    }
                }
                Label { role: "labelLarge"; text: qsTr("Maximum file size"); visible: maxSize.visible }
                ComboBox {
                    id: maxSize
                    Layout.fillWidth: true
                    visible: dialog.format === "mp4" || dialog.format === "webm"
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
                    text: dialog.format === "gif" ? qsTr("Animated GIF without sound, with its own colours: for chats and web pages.")
                         : dialog.kind === 2 ? qsTr("The sound of the video alone.")
                         : dialog.format === "png" ? qsTr("A folder with one PNG picture per frame.")
                         : dialog.format === "mov" ? (dialog.codecValue === "prores" ? qsTr("MOV video (ProRes and uncompressed sound): for editing in another program.")
                                                                                     : qsTr("MOV video: plays on Apple devices and in editing programs."))
                         : dialog.format === "webm" ? qsTr("WebM video (%1 and Opus): for web pages.").arg(dialog.codecValue === "av1" ? "AV1" : "VP9")
                         : dialog.codecValue === "h264" ? qsTr("MP4 video (H.264 and AAC): plays everywhere.")
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
                                                      quality.currentIndex, normalizeAudio.visible && normalizeAudio.checked, -14.0,
                                                      dialog.codecValue, hardwareUse.checked,
                                                      maxSize.visible ? dialog.maxSizeValue : 0, dialog.format,
                                                      onlyInOut.visible && onlyInOut.checked,
                                                      captionsFile.visible && captionsFile.checked))
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
