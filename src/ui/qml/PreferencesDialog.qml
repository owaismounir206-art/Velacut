// Preferences (SPEC §5.16, 1bis rule 6, §4 "colori dinamici"): appearance (Material You: theme, colour source, style,
// contrast, density, motion), language, performance (graphics backend and hardware acceleration, what was detected),
// asset packs (install from a folder or a .zip, remove) and system information. Every choice applies at once, except
// language and graphics, which need a restart (one button).
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

    title: qsTr("Preferences")
    iconName: "settings"
    standardButtons: Dialog.Close
    modal: true
    padding: 0

    property int section: 0
    readonly property int aiModelsSection: 4
    function openSection(index) {
        section = index
        open()
    }
    readonly property var sections: [{ text: qsTr("Appearance"), icon: "palette" },
                                     { text: qsTr("Language"), icon: "translate" },
                                     { text: qsTr("Performance"), icon: "speed" },
                                     { text: qsTr("Asset packs"), icon: "inventory_2" },
                                     { text: qsTr("AI models"), icon: "neurology" },
                                     { text: qsTr("About"), icon: "info" }]

    // A labelled group of the page.
    component Group: ColumnLayout {
        property string title
        property string detail
        default property alias content: body.data
        Layout.fillWidth: true
        spacing: Theme.space.sm
        Label {
            Layout.fillWidth: true
            role: "titleSmall"
            text: parent.title
        }
        Label {
            Layout.fillWidth: true
            visible: text !== ""
            role: "bodySmall"
            color: Theme.color.onSurfaceVariant
            wrapMode: Text.WordWrap
            text: parent.detail
        }
        ColumnLayout {
            id: body
            Layout.fillWidth: true
            spacing: Theme.space.sm
        }
    }
    // A row with a label and a switch.
    component SwitchRow: RowLayout {
        property alias text: label.text
        property alias checked: toggle.checked
        signal toggled(bool on)
        Layout.fillWidth: true
        Label {
            id: label
            Layout.fillWidth: true
            role: "bodyMedium"
            wrapMode: Text.WordWrap
        }
        Switch {
            id: toggle
            Accessible.name: label.text
            onToggled: parent.toggled(checked)
        }
    }
    // A choice among a few: filter chips (they wrap on narrow dialogs).
    component Choice: Flow {
        id: choice
        property var options: []
        property int current: 0
        signal chosen(int value)
        Layout.fillWidth: true
        spacing: Theme.space.xs
        Repeater {
            model: choice.options
            delegate: Chip {
                required property var modelData
                required property int index
                text: modelData
                checkable: false
                checked: choice.current === index
                onClicked: choice.chosen(index)
            }
        }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // Sections on the left.
        Column {
            Layout.preferredWidth: Theme.editor.libraryWidth / 2
            Layout.fillHeight: true
            Layout.margins: Theme.space.sm
            spacing: Theme.space.xxs
            Repeater {
                model: dialog.sections
                delegate: Rectangle {
                    id: entry
                    required property var modelData
                    required property int index
                    objectName: "preferencesSection_" + index
                    width: parent.width
                    height: Theme.space.control(48)
                    radius: Theme.shape.full
                    color: dialog.section === index ? Theme.color.secondaryContainer : "transparent"
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: Theme.space.lg
                        anchors.rightMargin: Theme.space.md
                        spacing: Theme.space.md
                        Icon {
                            name: entry.modelData.icon
                            filled: dialog.section === entry.index
                            size: Theme.editor.toolIconSize
                            color: dialog.section === entry.index ? Theme.color.onSecondaryContainer : Theme.color.onSurfaceVariant
                        }
                        Label {
                            Layout.fillWidth: true
                            role: "labelLarge"
                            elide: Text.ElideRight
                            color: dialog.section === entry.index ? Theme.color.onSecondaryContainer : Theme.color.onSurfaceVariant
                            text: entry.modelData.text
                        }
                    }
                    StateLayer {
                        radius: parent.radius
                        color: Theme.color.onSurface
                        hovered: sectionMouse.containsMouse
                        pressed: sectionMouse.pressed
                        pressPoint: Qt.point(sectionMouse.mouseX, sectionMouse.mouseY)
                    }
                    MouseArea {
                        id: sectionMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: dialog.section = entry.index
                    }
                    Accessible.role: Accessible.PageTab
                    Accessible.name: modelData.text
                    Accessible.selected: dialog.section === index
                }
            }
        }
        Divider { vertical: true; Layout.fillHeight: true }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // Language and graphics apply at the next start: say so where the choice was made.
            Rectangle {
                Layout.fillWidth: true
                Layout.margins: Theme.space.md
                Layout.bottomMargin: 0
                visible: App.restartNeeded
                implicitHeight: restartRow.implicitHeight + 2 * Theme.space.sm
                radius: Theme.shape.medium
                color: Theme.color.tertiaryContainer
                RowLayout {
                    id: restartRow
                    anchors.fill: parent
                    anchors.leftMargin: Theme.space.md
                    anchors.rightMargin: Theme.space.sm
                    spacing: Theme.space.md
                    Icon { name: "restart_alt"; color: Theme.color.onTertiaryContainer }
                    Label {
                        Layout.fillWidth: true
                        role: "bodyMedium"
                        wrapMode: Text.WordWrap
                        color: Theme.color.onTertiaryContainer
                        text: qsTr("Some changes apply when velacut starts again. Your project is saved.")
                    }
                    Button {
                        objectName: "restartButton"
                        variant: "filled"
                        text: qsTr("Restart now")
                        onClicked: App.restart()
                    }
                }
            }

            ScrollView {
                id: scroll
                Layout.fillWidth: true
                Layout.fillHeight: true
                contentWidth: availableWidth
                clip: true

                StackLayout {
                    width: scroll.availableWidth - 2 * Theme.space.lg
                    x: Theme.space.lg
                    y: Theme.space.md
                    currentIndex: dialog.section

                    // ---- Appearance -------------------------------------------------------------------------------
                    ColumnLayout {
                        spacing: Theme.space.xl
                        Group {
                            title: qsTr("Theme")
                            detail: qsTr("Automatic follows the light or dark setting of the desktop.")
                            SegmentedButton {
                                objectName: "themeMode"
                                model: [{ text: qsTr("Automatic"), iconName: "brightness_auto" },
                                        { text: qsTr("Light"), iconName: "light_mode" },
                                        { text: qsTr("Dark"), iconName: "dark_mode" }]
                                currentIndex: Theme.mode
                                onActivated: (index) => Theme.mode = index
                            }
                        }
                        Group {
                            title: qsTr("Colour")
                            detail: qsTr("The interface takes its colours from one colour (Material You).")
                            Choice {
                                objectName: "seedSource"
                                options: [qsTr("Desktop accent"), qsTr("Wallpaper"), qsTr("Project cover"), qsTr("Chosen by me"),
                                          qsTr("velacut default")]
                                current: Theme.seedSource
                                onChosen: (value) => Theme.seedSource = value
                            }
                            Label {
                                Layout.fillWidth: true
                                visible: Theme.seedSource !== Theme.effectiveSeedSource
                                role: "bodySmall"
                                color: Theme.color.onSurfaceVariant
                                wrapMode: Text.WordWrap
                                text: qsTr("Not available here: the next source in the list is used.")
                            }
                            Flow {
                                Layout.fillWidth: true
                                visible: Theme.seedSource === 3
                                spacing: Theme.space.sm
                                Repeater {
                                    // Seeds to try; the dynamic scheme derives every colour from the one picked.
                                    model: Theme.seedSuggestions
                                    delegate: Rectangle {
                                        id: seed
                                        required property color modelData
                                        width: Theme.editor.swatchSize + Theme.space.sm
                                        height: width
                                        radius: Theme.shape.full
                                        color: modelData
                                        border.width: Qt.colorEqual(Theme.manualSeed, modelData) ? Theme.editor.selectionBorder * 2 : 0
                                        border.color: Theme.color.onSurface
                                        Accessible.role: Accessible.Button
                                        Accessible.name: modelData.toString()
                                        MouseArea {
                                            anchors.fill: parent
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: Theme.manualSeed = seed.modelData
                                        }
                                    }
                                }
                                Button {
                                    variant: "text"
                                    iconName: "colorize"
                                    text: qsTr("Other…")
                                    onClicked: seedDialog.open()
                                }
                            }
                        }
                        Group {
                            title: qsTr("Colour style")
                            Choice {
                                objectName: "variant"
                                options: [qsTr("Tonal"), qsTr("Vibrant"), qsTr("Expressive"), qsTr("Neutral"), qsTr("Faithful"),
                                          qsTr("Content"), qsTr("Monochrome")]
                                current: Theme.variant
                                onChosen: (value) => Theme.variant = value
                            }
                            SwitchRow {
                                objectName: "neutralSurfaces"
                                text: qsTr("Grey panels around the video: the colours of the video are easier to judge")
                                checked: Theme.neutralSurfaces
                                onToggled: (on) => Theme.neutralSurfaces = on
                            }
                        }
                        Group {
                            title: qsTr("Contrast")
                            Choice {
                                options: [qsTr("Desktop setting"), qsTr("Standard"), qsTr("Medium"), qsTr("High")]
                                current: Theme.contrast
                                onChosen: (value) => Theme.contrast = value
                            }
                        }
                        Group {
                            title: qsTr("Density")
                            detail: qsTr("Compact puts more controls on the screen.")
                            Choice {
                                options: [qsTr("Comfortable"), qsTr("Compact")]
                                current: Theme.density
                                onChosen: (value) => Theme.density = value
                            }
                        }
                        Group {
                            title: qsTr("Animations")
                            Choice {
                                options: [qsTr("Desktop setting"), qsTr("All"), qsTr("Reduced")]
                                current: Theme.motionPreference
                                onChosen: (value) => Theme.motionPreference = value
                            }
                        }
                        Item { implicitHeight: Theme.space.lg }
                    }

                    // ---- Language ---------------------------------------------------------------------------------
                    ColumnLayout {
                        spacing: Theme.space.xl
                        Group {
                            title: qsTr("Language of the interface")
                            detail: qsTr("Automatic uses the language of the desktop.")
                            Choice {
                                objectName: "language"
                                options: [qsTr("Automatic"), "Italiano", "English"]
                                current: ["auto", "it", "en"].indexOf(App.language)
                                onChosen: (value) => App.language = ["auto", "it", "en"][value]
                            }
                        }
                    }

                    // ---- Performance ------------------------------------------------------------------------------
                    ColumnLayout {
                        spacing: Theme.space.xl
                        Group {
                            title: qsTr("Graphics")
                            detail: App.graphicsReasons.join("\n")
                            Label {
                                Layout.fillWidth: true
                                role: "bodyMedium"
                                text: qsTr("Interface drawn with: %1").arg(App.uiBackend) + (App.safeMode ? " · " + qsTr("safe mode") : "")
                            }
                        }
                        Group {
                            title: qsTr("Interface backend")
                            detail: qsTr("Automatic picks the best one that works on this computer. Software works everywhere, more slowly.")
                            Choice {
                                options: [qsTr("Automatic"), "Vulkan", "OpenGL", qsTr("Software")]
                                current: App.uiBackendChoice
                                onChosen: (value) => App.uiBackendChoice = value
                            }
                        }
                        Group {
                            title: qsTr("Acceleration")
                            detail: qsTr("When the graphics card fails, velacut goes back to the processor by itself.")
                            SwitchRow {
                                text: qsTr("Effects on the graphics card")
                                checked: App.gpuEffects
                                onToggled: (on) => App.gpuEffects = on
                            }
                            SwitchRow {
                                text: qsTr("Hardware video decoding")
                                checked: App.hardwareDecoding
                                onToggled: (on) => App.hardwareDecoding = on
                            }
                            SwitchRow {
                                text: qsTr("Hardware video encoding (export)")
                                checked: App.hardwareEncoding
                                onToggled: (on) => App.hardwareEncoding = on
                            }
                        }
                    }

                    // ---- Asset packs ------------------------------------------------------------------------------
                    ColumnLayout {
                        spacing: Theme.space.md
                        Group {
                            title: qsTr("Asset packs")
                            detail: qsTr("Filters, transitions, effects, text styles, stickers and templates made by others: their items appear in the libraries at once.")
                            RowLayout {
                                spacing: Theme.space.sm
                                Button {
                                    objectName: "installPackZip"
                                    variant: "filled"
                                    iconName: "folder_zip"
                                    text: qsTr("Install from .zip…")
                                    onClicked: zipDialog.open()
                                }
                                Button {
                                    variant: "tonal"
                                    iconName: "folder_open"
                                    text: qsTr("Install from a folder…")
                                    onClicked: folderDialog.open()
                                }
                            }
                        }
                        Repeater {
                            model: PackagesModel { id: packs }
                            delegate: Rectangle {
                                id: pack
                                required property string packId
                                required property string name
                                required property int version
                                required property int items
                                required property bool builtIn
                                Layout.fillWidth: true
                                implicitHeight: packRow.implicitHeight + 2 * Theme.space.md
                                radius: Theme.shape.medium
                                color: Theme.color.surfaceContainerHighest
                                RowLayout {
                                    id: packRow
                                    anchors.fill: parent
                                    anchors.margins: Theme.space.md
                                    spacing: Theme.space.md
                                    Icon {
                                        name: pack.builtIn ? "verified" : "inventory_2"
                                        color: pack.builtIn ? Theme.color.primary : Theme.color.onSurfaceVariant
                                    }
                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: Theme.space.xxs
                                        Label {
                                            Layout.fillWidth: true
                                            role: "titleSmall"
                                            elide: Text.ElideRight
                                            text: pack.name
                                        }
                                        Label {
                                            Layout.fillWidth: true
                                            role: "bodySmall"
                                            color: Theme.color.onSurfaceVariant
                                            elide: Text.ElideRight
                                            text: (pack.builtIn ? qsTr("Included in velacut") + " · " : "")
                                                  + qsTr("%n item(s)", "", pack.items) + " · " + qsTr("version %1").arg(pack.version)
                                                  + " · " + pack.packId
                                        }
                                    }
                                    IconButton {
                                        visible: !pack.builtIn
                                        iconName: "delete"
                                        label: qsTr("Remove the pack")
                                        onClicked: {
                                            const error = packs.remove(pack.packId)
                                            App.message(error !== "" ? error : qsTr("Pack removed"))
                                        }
                                    }
                                }
                            }
                        }
                        Label {
                            Layout.fillWidth: true
                            role: "bodySmall"
                            color: Theme.color.onSurfaceVariant
                            elide: Text.ElideMiddle
                            text: qsTr("Installed in %1").arg(packs.folder)
                        }
                    }

                    // ---- AI models (SPEC §1: optional components, models downloaded only on request) ----------------
                    ColumnLayout {
                        spacing: Theme.space.md
                        AiModelsModel {
                            id: aiModels
                            onMessage: (text) => App.message(text)
                        }
                        Group {
                            title: qsTr("Speech recognition")
                            detail: aiModels.whisperInstalled
                                    ? qsTr("whisper.cpp is installed (%1): automatic captions and editing by the transcript work offline.").arg(aiModels.whisperPath)
                                    : qsTr("Automatic captions and editing by the transcript need whisper.cpp, which is not installed. Install it with this command, then press “Check again”:")
                            RowLayout {
                                visible: !aiModels.whisperInstalled
                                spacing: Theme.space.sm
                                Rectangle {
                                    Layout.fillWidth: true
                                    implicitHeight: command.implicitHeight + 2 * Theme.space.sm
                                    radius: Theme.shape.small
                                    color: Theme.color.surfaceContainerHighest
                                    Label {
                                        id: command
                                        anchors.fill: parent
                                        anchors.margins: Theme.space.sm
                                        font.family: "monospace"
                                        text: aiModels.whisperInstallCommand
                                    }
                                }
                                IconButton {
                                    iconName: "content_copy"
                                    label: qsTr("Copy the command")
                                    onClicked: {
                                        App.copyText(aiModels.whisperInstallCommand)
                                        App.message(qsTr("Command copied"))
                                    }
                                }
                                Button {
                                    objectName: "checkWhisperAgain"
                                    variant: "tonal"
                                    iconName: "refresh"
                                    text: qsTr("Check again")
                                    onClicked: aiModels.refresh()
                                }
                            }
                        }
                        Repeater {
                            model: aiModels
                            delegate: Rectangle {
                                id: aiModel
                                required property string modelId
                                required property string name
                                required property string detail
                                required property string size
                                required property bool installed
                                required property bool downloading
                                required property real progress
                                Layout.fillWidth: true
                                implicitHeight: aiRow.implicitHeight + 2 * Theme.space.md
                                radius: Theme.shape.medium
                                color: Theme.color.surfaceContainerHighest
                                RowLayout {
                                    id: aiRow
                                    anchors.fill: parent
                                    anchors.margins: Theme.space.md
                                    spacing: Theme.space.md
                                    Icon {
                                        name: aiModel.installed ? "download_done" : "neurology"
                                        color: aiModel.installed ? Theme.color.primary : Theme.color.onSurfaceVariant
                                    }
                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: Theme.space.xxs
                                        Label {
                                            Layout.fillWidth: true
                                            role: "titleSmall"
                                            elide: Text.ElideRight
                                            text: aiModel.name
                                        }
                                        Label {
                                            Layout.fillWidth: true
                                            role: "bodySmall"
                                            color: Theme.color.onSurfaceVariant
                                            wrapMode: Text.WordWrap
                                            text: aiModel.detail + " · " + aiModel.size
                                        }
                                        ProgressBar {
                                            Layout.fillWidth: true
                                            visible: aiModel.downloading
                                            value: aiModel.progress
                                        }
                                    }
                                    Button {
                                        objectName: "downloadModel_" + aiModel.modelId
                                        visible: !aiModel.installed && !aiModel.downloading
                                        variant: "tonal"
                                        iconName: "download"
                                        text: qsTr("Download (%1)").arg(aiModel.size)
                                        onClicked: aiModels.download(aiModel.modelId)
                                    }
                                    IconButton {
                                        visible: aiModel.downloading
                                        iconName: "close"
                                        label: qsTr("Stop the download")
                                        onClicked: aiModels.cancel(aiModel.modelId)
                                    }
                                    IconButton {
                                        objectName: "removeModel_" + aiModel.modelId
                                        visible: aiModel.installed
                                        iconName: "delete"
                                        label: qsTr("Remove the model")
                                        onClicked: {
                                            const error = aiModels.remove(aiModel.modelId)
                                            App.message(error !== "" ? error : qsTr("Model removed"))
                                        }
                                    }
                                }
                            }
                        }
                        Label {
                            Layout.fillWidth: true
                            role: "bodySmall"
                            color: Theme.color.onSurfaceVariant
                            wrapMode: Text.WordWrap
                            text: qsTr("Models are downloaded only when you ask, from huggingface.co (whisper.cpp project, MIT licence), and saved in %1.").arg(aiModels.folder)
                        }
                        // Text to speech: Piper and the voices the user chose (their licences differ voice by voice).
                        Group {
                            title: qsTr("Read aloud (text to speech)")
                            detail: aiModels.piperInstalled
                                    ? qsTr("Piper is installed. Add the voices you want (a .onnx file with its .onnx.json, from the Piper project); check the licence of each voice before publishing.")
                                    : qsTr("Reading texts aloud needs Piper, which is not installed: “%1”.").arg(aiModels.piperInstallCommand)
                            RowLayout {
                                spacing: Theme.space.sm
                                Button {
                                    objectName: "addVoice"
                                    variant: "tonal"
                                    iconName: "record_voice_over"
                                    text: qsTr("Add a voice…")
                                    onClicked: voiceDialog.open()
                                }
                                Button {
                                    visible: !aiModels.piperInstalled
                                    variant: "text"
                                    iconName: "refresh"
                                    text: qsTr("Check again")
                                    onClicked: aiModels.refresh()
                                }
                            }
                            Repeater {
                                model: aiModels.voices
                                delegate: RowLayout {
                                    id: voice
                                    required property var modelData
                                    Layout.fillWidth: true
                                    Icon { name: "record_voice_over"; color: Theme.color.onSurfaceVariant }
                                    Label {
                                        Layout.fillWidth: true
                                        role: "bodyMedium"
                                        elide: Text.ElideRight
                                        text: voice.modelData.name
                                    }
                                    IconButton {
                                        iconName: "delete"
                                        label: qsTr("Remove the voice")
                                        onClicked: {
                                            const error = aiModels.removeVoice(voice.modelData.path)
                                            App.message(error !== "" ? error : qsTr("Voice removed"))
                                        }
                                    }
                                }
                            }
                        }
                        FileDialog {
                            id: voiceDialog
                            title: qsTr("Add a Piper voice")
                            fileMode: FileDialog.OpenFile
                            nameFilters: [qsTr("Piper voices (%1)").arg("*.onnx")]
                            onAccepted: {
                                const error = aiModels.addVoice(selectedFile)
                                App.message(error !== "" ? error : qsTr("Voice added"))
                            }
                        }
                    }

                    // ---- About ------------------------------------------------------------------------------------
                    ColumnLayout {
                        spacing: Theme.space.md
                        Label {
                            role: "headlineSmall"
                            text: "velacut " + App.version
                        }
                        Label {
                            Layout.fillWidth: true
                            role: "bodyMedium"
                            wrapMode: Text.WordWrap
                            color: Theme.color.onSurfaceVariant
                            text: qsTr("An offline video editor: everything runs on this computer. Free software under the GPL 3.0 licence.")
                        }
                        Rectangle {
                            Layout.fillWidth: true
                            implicitHeight: info.implicitHeight + 2 * Theme.space.md
                            radius: Theme.shape.medium
                            color: Theme.color.surfaceContainerHighest
                            Label {
                                id: info
                                anchors.fill: parent
                                anchors.margins: Theme.space.md
                                role: "bodySmall"
                                wrapMode: Text.WrapAnywhere
                                textFormat: Text.PlainText
                                text: App.systemInformation
                            }
                        }
                        Button {
                            variant: "tonal"
                            iconName: "content_copy"
                            text: qsTr("Copy system information")
                            onClicked: {
                                App.copySystemInformation()
                                App.message(qsTr("System information copied"))
                            }
                        }
                    }
                }
            }
        }
    }

    ColorDialog {
        id: seedDialog
        title: qsTr("Colour of the interface")
        selectedColor: Theme.manualSeed
        onAccepted: Theme.manualSeed = selectedColor
    }
    FileDialog {
        id: zipDialog
        title: qsTr("Install an asset pack")
        nameFilters: [qsTr("Asset packs (%1)").arg("*.zip"), qsTr("All files (*)")]
        onAccepted: {
            const error = packs.install(selectedFile)
            App.message(error !== "" ? error : qsTr("Pack installed: its items are in the libraries"))
        }
    }
    FolderDialog {
        id: folderDialog
        title: qsTr("Install an asset pack from a folder")
        onAccepted: {
            const error = packs.install(selectedFolder)
            App.message(error !== "" ? error : qsTr("Pack installed: its items are in the libraries"))
        }
    }
}
