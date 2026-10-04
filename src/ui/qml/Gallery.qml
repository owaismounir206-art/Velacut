// Component gallery (vedit --component-gallery): every Material 3 component of vedit, the full color scheme
// and the type scale, in light, dark and high contrast, with the seed coming from the system (SPEC §4).
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material as M
import QtQuick.Layouts
import Vedit.Components
import Vedit.Theme
import Vedit.UI

ApplicationWindow {
    id: window

    width: 1280
    height: 900
    visible: true
    title: qsTr("Component gallery — vedit")
    color: Theme.color.surface

    M.Material.theme: Theme.dark ? M.Material.Dark : M.Material.Light
    M.Material.accent: Theme.color.primary
    M.Material.primary: Theme.color.primary
    M.Material.background: Theme.color.surface
    M.Material.foreground: Theme.color.onSurface

    readonly property var sections: [
        { text: qsTr("Buttons"), iconName: "smart_button" },
        { text: qsTr("Selection"), iconName: "check_box" },
        { text: qsTr("Text"), iconName: "text_fields" },
        { text: qsTr("Containers"), iconName: "dashboard" },
        { text: qsTr("Feedback"), iconName: "notifications" },
        { text: qsTr("Colors"), iconName: "palette" },
        { text: qsTr("Type"), iconName: "format_size" }
    ]
    readonly property var sectionItems: [buttonsSection, selectionSection, textSection, containersSection,
                                         feedbackSection, colorsSection, typeSection]

    header: TopAppBar {
        title: qsTr("Component gallery")
        scrolled: !scroller.atYBeginning
        trailing: [
            SegmentedButton {
                model: [{ text: qsTr("Auto") }, { text: qsTr("Light") }, { text: qsTr("Dark") }]
                currentIndex: Theme.mode === Theme.Auto ? 0 : Theme.mode === Theme.Light ? 1 : 2
                onActivated: (index) => Theme.mode = [Theme.Auto, Theme.Light, Theme.Dark][index]
            },
            SegmentedButton {
                model: [{ text: qsTr("Standard") }, { text: qsTr("Medium") }, { text: qsTr("High") }]
                currentIndex: Theme.contrast === Theme.Medium ? 1 : Theme.contrast === Theme.High ? 2 : 0
                onActivated: (index) => Theme.contrast = [Theme.Standard, Theme.Medium, Theme.High][index]
            },
            IconButton {
                iconName: "density_medium"
                label: qsTr("Compact density")
                checkable: true
                checked: Theme.density === Theme.Compact
                onToggled: Theme.density = checked ? Theme.Compact : Theme.Comfortable
            }
        ]
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        NavigationRail {
            Layout.fillHeight: true
            model: window.sections
            onActivated: (index) => scroller.contentY = Math.min(window.sectionItems[index].y,
                                                                 scroller.contentHeight - scroller.height)
        }

        Flickable {
            id: scroller
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: width
            contentHeight: content.implicitHeight + 2 * Theme.space.xl
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { }

            ColumnLayout {
                id: content
                x: Theme.space.xl
                y: Theme.space.xl
                width: scroller.width - 2 * Theme.space.xl
                spacing: Theme.space.xxl

                // Seed and scheme
                Card {
                    Layout.fillWidth: true
                    Layout.preferredHeight: seedColumn.implicitHeight + 2 * Theme.space.lg
                    variant: "outlined"
                    ColumnLayout {
                        id: seedColumn
                        x: Theme.space.lg
                        y: Theme.space.lg
                        width: parent.width - 2 * Theme.space.lg
                        spacing: Theme.space.md
                        Label {
                            role: "titleMedium"
                            text: qsTr("Seed color: %1 (source: %2%3)").arg(Theme.seedColor)
                                  .arg([qsTr("system accent"), qsTr("wallpaper"), qsTr("project cover"), qsTr("manual"), qsTr("default")][Theme.effectiveSeedSource])
                                  .arg(Theme.effectiveSeedSource === Theme.System ? " — " + Theme.systemAccentOrigin : "")
                        }
                        Flow {
                            Layout.fillWidth: true
                            spacing: Theme.space.sm
                            Repeater {
                                model: [{ text: qsTr("System accent"), value: Theme.System },
                                        { text: qsTr("Wallpaper"), value: Theme.Wallpaper },
                                        { text: qsTr("Manual"), value: Theme.Manual },
                                        { text: qsTr("Default"), value: Theme.Default }]
                                Chip {
                                    required property var modelData
                                    checkable: false
                                    text: modelData.text
                                    checked: Theme.seedSource === modelData.value
                                    onClicked: Theme.seedSource = modelData.value
                                }
                            }
                        }
                        Flow {
                            Layout.fillWidth: true
                            spacing: Theme.space.sm
                            Repeater {
                                model: [{ text: "Tonal spot", value: Theme.TonalSpot }, { text: "Vibrant", value: Theme.Vibrant },
                                        { text: "Expressive", value: Theme.Expressive }, { text: "Neutral", value: Theme.Neutral },
                                        { text: "Fidelity", value: Theme.Fidelity }, { text: "Content", value: Theme.Content },
                                        { text: "Monochrome", value: Theme.Monochrome }]
                                Chip {
                                    required property var modelData
                                    checkable: false
                                    text: modelData.text
                                    checked: Theme.variant === modelData.value
                                    onClicked: Theme.variant = modelData.value
                                }
                            }
                        }
                        Row {
                            spacing: Theme.space.sm
                            Label { role: "labelLarge"; text: qsTr("Manual seed:"); anchors.verticalCenter: parent.verticalCenter }
                            Repeater {
                                // Sample seeds for trying the dynamic color (the user picks any color in Preferences).
                                model: Theme.seedSuggestions
                                Rectangle {
                                    required property color modelData
                                    width: 32
                                    height: 32
                                    radius: Theme.shape.full
                                    color: modelData
                                    border.width: Theme.seedSource === Theme.Manual && Qt.colorEqual(Theme.manualSeed, modelData) ? 3 : 0
                                    border.color: Theme.color.onSurface
                                    TapHandler {
                                        onTapped: { Theme.manualSeed = parent.modelData; Theme.seedSource = Theme.Manual }
                                    }
                                    Accessible.role: Accessible.Button
                                    Accessible.name: modelData.toString()
                                }
                            }
                        }
                    }
                }

                // Buttons
                ColumnLayout {
                    id: buttonsSection
                    Layout.fillWidth: true
                    spacing: Theme.space.md
                    Label { role: "headlineSmall"; text: qsTr("Buttons") }
                    Repeater {
                        model: [true, false]
                        Flow {
                            id: buttonRow
                            required property bool modelData
                            Layout.fillWidth: true
                            spacing: Theme.space.md
                            Button { text: qsTr("Filled"); enabled: buttonRow.modelData }
                            Button { text: qsTr("Tonal"); variant: "tonal"; enabled: buttonRow.modelData }
                            Button { text: qsTr("Outlined"); variant: "outlined"; enabled: buttonRow.modelData }
                            Button { text: qsTr("Text"); variant: "text"; enabled: buttonRow.modelData }
                            Button { text: qsTr("Elevated"); variant: "elevated"; enabled: buttonRow.modelData }
                            Button { text: qsTr("Export"); iconName: "upload"; enabled: buttonRow.modelData }
                            Button { text: qsTr("Add"); iconName: "add"; variant: "tonal"; enabled: buttonRow.modelData }
                        }
                    }
                    Flow {
                        Layout.fillWidth: true
                        spacing: Theme.space.md
                        IconButton { iconName: "content_cut"; label: qsTr("Split"); shortcutText: "S" }
                        IconButton { iconName: "delete"; label: qsTr("Delete"); variant: "filled" }
                        IconButton { iconName: "speed"; label: qsTr("Speed"); variant: "tonal" }
                        IconButton { iconName: "flip"; label: qsTr("Mirror"); variant: "outlined" }
                        IconButton { iconName: "link"; label: qsTr("Link audio and video"); checkable: true; checked: true }
                        IconButton { iconName: "join_inner"; label: qsTr("Snapping"); checkable: true; variant: "tonal" }
                        IconButton { iconName: "lock"; label: qsTr("Disabled"); enabled: false }
                    }
                    Flow {
                        Layout.fillWidth: true
                        spacing: Theme.space.md
                        Fab { iconName: "add" }
                        Fab { iconName: "edit"; color: "secondary" }
                        Fab { iconName: "auto_awesome"; color: "tertiary" }
                        Fab { iconName: "upload"; text: qsTr("Export") }
                        Fab { iconName: "movie"; text: qsTr("New project"); color: "surface" }
                    }
                    SegmentedButton {
                        model: [{ text: "16:9" }, { text: "9:16" }, { text: "1:1" }, { text: "4:5" }, { text: "21:9" }]
                    }
                }

                // Selection
                ColumnLayout {
                    id: selectionSection
                    Layout.fillWidth: true
                    spacing: Theme.space.md
                    Label { role: "headlineSmall"; text: qsTr("Selection") }
                    Flow {
                        Layout.fillWidth: true
                        spacing: Theme.space.sm
                        Chip { text: qsTr("Favorites"); iconName: "star" }
                        Chip { text: qsTr("Recent"); checked: true }
                        Chip { text: qsTr("Transitions"); variant: "assist"; iconName: "animation" }
                        Chip { text: "spiaggia.mp4"; variant: "input" }
                        Chip { text: qsTr("Disabled"); enabled: false }
                    }
                    RowLayout {
                        spacing: Theme.space.xl
                        ColumnLayout {
                            Switch { text: qsTr("Snapping"); checked: true }
                            Switch { text: qsTr("Preview skimming") }
                            Switch { text: qsTr("Disabled"); enabled: false }
                        }
                        ColumnLayout {
                            CheckBox { text: qsTr("Apply to all"); checked: true }
                            CheckBox { text: qsTr("Partially"); tristate: true; checkState: Qt.PartiallyChecked }
                            CheckBox { text: qsTr("Off") }
                        }
                        ColumnLayout {
                            ButtonGroup { id: qualityGroup }
                            RadioButton { text: qsTr("Low"); ButtonGroup.group: qualityGroup }
                            RadioButton { text: qsTr("Recommended"); checked: true; ButtonGroup.group: qualityGroup }
                            RadioButton { text: qsTr("High"); ButtonGroup.group: qualityGroup }
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.space.lg
                        Slider { Layout.fillWidth: true; value: 0.4; valueText: Math.round(value * 100) + "%" }
                        Slider { Layout.fillWidth: true; value: 0.7; enabled: false }
                    }
                }

                // Text
                ColumnLayout {
                    id: textSection
                    Layout.fillWidth: true
                    spacing: Theme.space.md
                    Label { role: "headlineSmall"; text: qsTr("Text fields") }
                    Flow {
                        Layout.fillWidth: true
                        spacing: Theme.space.lg
                        TextField { label: qsTr("Project name"); text: "Viaggio a Roma" }
                        TextField { label: qsTr("Duration"); supportingText: qsTr("Seconds, for example 3") }
                        TextField { variant: "outlined"; label: qsTr("File name"); text: "video.mp4" }
                        TextField { variant: "outlined"; label: qsTr("Bitrate"); text: "abc"; error: true; supportingText: qsTr("Enter a number") }
                    }
                    SearchBar { placeholderText: qsTr("Search effects, transitions, music…") }
                }

                // Containers
                ColumnLayout {
                    id: containersSection
                    Layout.fillWidth: true
                    spacing: Theme.space.md
                    Label { role: "headlineSmall"; text: qsTr("Containers") }
                    Flow {
                        Layout.fillWidth: true
                        spacing: Theme.space.lg
                        Repeater {
                            model: [{ variant: "filled", text: qsTr("Filled card") }, { variant: "elevated", text: qsTr("Elevated card") },
                                    { variant: "outlined", text: qsTr("Outlined card") }]
                            Card {
                                id: card
                                required property var modelData
                                width: 200
                                height: 140
                                variant: modelData.variant
                                interactive: true
                                Column {
                                    anchors.fill: parent
                                    anchors.margins: Theme.space.lg
                                    spacing: Theme.space.sm
                                    Icon { name: "movie"; color: Theme.color.primary }
                                    Label { role: "titleMedium"; text: card.modelData.text }
                                    Label { role: "bodySmall"; color: Theme.color.onSurfaceVariant; text: qsTr("Hover to preview") }
                                }
                            }
                        }
                    }
                    RowLayout {
                        spacing: Theme.space.lg
                        Button { text: qsTr("Open dialog"); variant: "outlined"; onClicked: dialog.open() }
                        Button { text: qsTr("Open menu"); variant: "outlined"; onClicked: menu.popup() }
                        Menu {
                            id: menu
                            MenuItem { text: qsTr("Split"); iconName: "content_cut"; shortcutText: "S" }
                            MenuItem { text: qsTr("Duplicate"); iconName: "content_copy"; shortcutText: "Ctrl+D" }
                            MenuSeparator { }
                            MenuItem { text: qsTr("Delete"); iconName: "delete"; shortcutText: "Del" }
                        }
                        Dialog {
                            id: dialog
                            iconName: "delete"
                            title: qsTr("Delete this project?")
                            standardButtons: Dialog.Cancel | Dialog.Ok
                            Label {
                                width: 320
                                role: "bodyMedium"
                                color: Theme.color.onSurfaceVariant
                                wrapMode: Text.Wrap
                                text: qsTr("The draft will be removed. The media files on disk are not deleted.")
                            }
                        }
                    }
                }

                // Feedback
                ColumnLayout {
                    id: feedbackSection
                    Layout.fillWidth: true
                    spacing: Theme.space.md
                    Label { role: "headlineSmall"; text: qsTr("Feedback") }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.space.xl
                        ProgressBar { Layout.fillWidth: true; value: 0.6 }
                        ProgressBar { Layout.fillWidth: true; indeterminate: true }
                        BusyIndicator { running: true }
                        BusyIndicator { progress: 0.7 }
                        Row {
                            spacing: Theme.space.lg
                            Badge { count: 0 }
                            Badge { count: 3 }
                            Badge { count: 1200 }
                        }
                    }
                    RowLayout {
                        spacing: Theme.space.lg
                        Button {
                            text: qsTr("Show snackbar")
                            variant: "tonal"
                            onClicked: snackbar.show(qsTr("Clip deleted"), qsTr("Undo"))
                        }
                        Button {
                            text: qsTr("Hover for tooltip")
                            variant: "text"
                            ToolTip.visible: hovered
                            ToolTip.text: qsTr("Tooltips show the keyboard shortcut (S)")
                        }
                    }
                }

                // Colors
                ColumnLayout {
                    id: colorsSection
                    Layout.fillWidth: true
                    spacing: Theme.space.md
                    Label { role: "headlineSmall"; text: qsTr("Color roles") }
                    GridLayout {
                        Layout.fillWidth: true
                        columns: Math.max(2, Math.floor(content.width / 220))
                        columnSpacing: Theme.space.sm
                        rowSpacing: Theme.space.sm
                        Repeater {
                            model: Theme.colorRoleNames()
                            Rectangle {
                                id: swatch
                                required property string modelData
                                Layout.fillWidth: true
                                Layout.preferredHeight: 56
                                radius: Theme.shape.small
                                color: Theme.color[modelData]
                                border.width: 1
                                border.color: Theme.color.outlineVariant
                                Label {
                                    anchors.fill: parent
                                    anchors.margins: Theme.space.sm
                                    role: "labelMedium"
                                    color: Theme.readableOn(swatch.color)
                                    text: swatch.modelData + "\n" + swatch.color
                                    lineHeightMode: Text.ProportionalHeight
                                    lineHeight: 1.1
                                    verticalAlignment: Text.AlignTop
                                }
                            }
                        }
                    }
                }

                // Type scale
                ColumnLayout {
                    id: typeSection
                    Layout.fillWidth: true
                    spacing: Theme.space.sm
                    Label { role: "headlineSmall"; text: qsTr("Type scale") }
                    Repeater {
                        model: ["displayLarge", "displayMedium", "displaySmall", "headlineLarge", "headlineMedium", "headlineSmall",
                                "titleLarge", "titleMedium", "titleSmall", "bodyLarge", "bodyMedium", "bodySmall",
                                "labelLarge", "labelMedium", "labelSmall"]
                        Label {
                            required property string modelData
                            Layout.fillWidth: true
                            role: modelData
                            elide: Text.ElideRight
                            text: modelData + " — " + qsTr("Quick, simple, offline editing")
                        }
                    }
                }
            }
        }
    }

    Snackbar {
        id: snackbar
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Theme.space.xl
    }
}
