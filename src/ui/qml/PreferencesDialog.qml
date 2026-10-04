// Preferences (SPEC §5.16, 1bis rule 6, §4 "colori dinamici"): appearance (Material You: theme, colour source, style,
// contrast, density, motion), language, performance (graphics backend and hardware acceleration, what was detected),
// asset packs (install from a folder or a .zip, remove) and system information. Every choice applies at once, except
// language and graphics, which need a restart (one button).
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

    title: qsTr("Preferences")
    iconName: "settings"
    standardButtons: Dialog.Close
    modal: true
    padding: 0

    property int section: 0
    readonly property var sections: [{ text: qsTr("Appearance"), icon: "palette" },
                                     { text: qsTr("Language"), icon: "translate" },
                                     { text: qsTr("Performance"), icon: "speed" },
                                     { text: qsTr("Asset packs"), icon: "inventory_2" },
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
                        text: qsTr("Some changes apply when vedit starts again. Your project is saved.")
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
                                          qsTr("vedit default")]
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
                            detail: qsTr("When the graphics card fails, vedit goes back to the processor by itself.")
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
                                            text: (pack.builtIn ? qsTr("Included in vedit") + " · " : "")
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

                    // ---- About ------------------------------------------------------------------------------------
                    ColumnLayout {
                        spacing: Theme.space.md
                        Label {
                            role: "headlineSmall"
                            text: "vedit " + App.version
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
