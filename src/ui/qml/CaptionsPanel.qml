// Captions (SPEC §5.8, §4 "Sottotitoli"): the lines of the caption track, edited in place (a click plays the line,
// split, join, delete, find and replace, move them all to match the speech), and the social styles of the library,
// shown on the player under the pointer and applied with a click to every line. Subtitle files in (SRT, WebVTT) and out.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import Velacut.Components
import Velacut.Theme
import Velacut.UI

Item {
    id: panel

    required property Editor editor
    readonly property Captions captions: editor.captions
    readonly property AiTools ai: editor.ai
    property int page: 0 // 0 = lines, 1 = styles
    // Speech recognition is missing something: Preferences → AI models.
    signal setUpRequested()

    // Languages offered for automatic captions (whisper.cpp codes); "auto" finds it.
    readonly property var languages: [{ code: "auto", text: qsTr("Find the language") }, { code: "it", text: "Italiano" },
                                      { code: "en", text: "English" }, { code: "es", text: "Español" },
                                      { code: "fr", text: "Français" }, { code: "de", text: "Deutsch" },
                                      { code: "pt", text: "Português" }]
    property int language: 0
    // Bilingual captions: each line also in English, smaller, under the words (whisper.cpp translates into English).
    property bool bilingual: false

    // Captions just made from the speech: their look comes next (one click to change it).
    Connections {
        target: panel.ai
        function onCaptionsMade() { panel.page = 1 }
    }

    component Tool: IconButton {
        implicitWidth: Theme.editor.toolButtonSize
        implicitHeight: Theme.editor.toolButtonSize
        iconSize: Theme.editor.toolIconSize
    }

    // Captions from a script: the text as written, timed by the speech.
    Dialog {
        id: scriptDialog
        objectName: "captionScriptDialog"
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(parent ? parent.width - 2 * Theme.space.xl : Theme.editor.dialogWidth, Theme.editor.dialogWidth)
        modal: true
        title: qsTr("Captions from a script")
        standardButtons: Dialog.Ok | Dialog.Cancel
        onOpened: script.forceActiveFocus()
        onAccepted: panel.ai.captionsFromScript(script.text, panel.languages[panel.language].code)
        ColumnLayout {
            anchors.fill: parent
            spacing: Theme.space.sm
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                role: "bodyMedium"
                color: Theme.color.onSurfaceVariant
                text: qsTr("Paste what is said in the video: the captions keep your words and spelling, each at the moment it is said.")
            }
            ScrollView {
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.editor.dialogWidth / 2
                TextArea {
                    id: script
                    objectName: "captionScript"
                    wrapMode: TextEdit.Wrap
                    placeholderText: qsTr("Paste the script here")
                }
            }
        }
    }

    FileDialog {
        id: importDialog
        objectName: "captionImportDialog"
        title: qsTr("Import captions")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("Subtitles (%1)").arg("*.srt *.vtt"), qsTr("All files (*)")]
        onAccepted: if (panel.captions.importFile(selectedFile)) panel.page = 0
    }
    FileDialog {
        id: exportDialog
        title: qsTr("Save the captions")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "srt"
        nameFilters: [qsTr("SubRip (%1)").arg("*.srt"), qsTr("WebVTT (%1)").arg("*.vtt")]
        onAccepted: panel.captions.exportFile(selectedFile)
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.space.md
        anchors.topMargin: 0
        spacing: Theme.space.sm

        LibraryHeader {
            Layout.fillWidth: true
            title: qsTr("Captions")
            Tool {
                objectName: "autoCaptionsHeaderButton"
                anchors.verticalCenter: parent.verticalCenter
                visible: panel.captions.hasCaptions
                enabled: panel.ai.speechStatus === 0 && !panel.ai.busy
                iconName: "auto_awesome"
                label: qsTr("Make the captions again from the speech")
                onClicked: panel.ai.autoCaptions(panel.languages[panel.language].code, panel.bilingual)
            }
            Tool {
                objectName: "captionImportButton"
                anchors.verticalCenter: parent.verticalCenter
                iconName: "upload_file"
                label: qsTr("Import a subtitle file (SRT, WebVTT)")
                onClicked: importDialog.open()
            }
            Tool {
                objectName: "captionExportButton"
                anchors.verticalCenter: parent.verticalCenter
                visible: panel.captions.hasCaptions
                iconName: "download"
                label: qsTr("Save the captions as a file (SRT, WebVTT)")
                onClicked: {
                    exportDialog.selectedFile = panel.captions.exportUrl()
                    exportDialog.open()
                }
            }
        }

        SegmentedButton {
            objectName: "captionPages"
            Layout.fillWidth: true
            model: [{ text: qsTr("Lines"), iconName: "subtitles" }, { text: qsTr("Styles"), iconName: "format_color_text" }]
            currentIndex: panel.page
            onActivated: (index) => panel.page = index
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: panel.page

            // ---- Lines ----
            Item {
                // No captions yet: where they come from.
                ColumnLayout {
                    anchors.fill: parent
                    visible: !panel.captions.hasCaptions
                    spacing: Theme.space.md
                    Item { Layout.fillHeight: true }
                    Icon {
                        Layout.alignment: Qt.AlignHCenter
                        name: "subtitles"
                        size: Theme.space.xxxl
                        color: Theme.color.primary
                    }
                    Label {
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                        role: "bodyMedium"
                        color: Theme.color.onSurfaceVariant
                        text: qsTr("Captions from the speech, from a subtitle file, or typed line by line: each word lights up while it is said.")
                    }
                    // Automatic captions (SPEC §5.8): the language, one click.
                    ComboBox {
                        objectName: "captionLanguage"
                        Layout.alignment: Qt.AlignHCenter
                        Layout.preferredWidth: Theme.editor.panelMinimumWidth
                        visible: panel.ai.speechStatus === 0
                        model: panel.languages.map(l => l.text)
                        currentIndex: panel.language
                        displayText: model[currentIndex] ?? ""
                        Accessible.name: qsTr("Language of the speech")
                        onActivated: (index) => panel.language = index
                    }
                    RowLayout {
                        Layout.alignment: Qt.AlignHCenter
                        Layout.preferredWidth: Theme.editor.panelMinimumWidth
                        visible: panel.ai.speechStatus === 0
                        Label {
                            Layout.fillWidth: true
                            role: "bodyMedium"
                            text: qsTr("Also in English")
                        }
                        Switch {
                            objectName: "captionBilingual"
                            checked: panel.bilingual
                            Accessible.name: qsTr("Bilingual captions: each line also in English, under the words")
                            onToggled: panel.bilingual = checked
                        }
                    }
                    Button {
                        objectName: "autoCaptionsButton"
                        Layout.alignment: Qt.AlignHCenter
                        variant: "filled"
                        iconName: "auto_awesome"
                        enabled: panel.ai.speechStatus === 0 && !panel.ai.busy
                        text: qsTr("Auto captions")
                        onClicked: panel.ai.autoCaptions(panel.languages[panel.language].code, panel.bilingual)
                    }
                    // What is missing, and where to get it (never a button that does nothing).
                    Label {
                        Layout.fillWidth: true
                        visible: panel.ai.speechStatus !== 0
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                        role: "bodySmall"
                        color: Theme.color.onSurfaceVariant
                        text: panel.ai.speechStatus === 1
                              ? qsTr("Auto captions need whisper.cpp, which is not installed: “%1”.").arg(panel.ai.speechInstallCommand)
                              : qsTr("Auto captions need a speech model: download one once, it stays on this computer.")
                    }
                    Button {
                        objectName: "speechSetUp"
                        Layout.alignment: Qt.AlignHCenter
                        visible: panel.ai.speechStatus !== 0
                        variant: "text"
                        iconName: "neurology"
                        text: panel.ai.speechStatus === 1 ? qsTr("How to install") : qsTr("Download a model")
                        onClicked: panel.setUpRequested()
                    }
                    Button {
                        objectName: "captionScriptEmpty"
                        Layout.alignment: Qt.AlignHCenter
                        visible: panel.ai.speechStatus === 0
                        variant: "text"
                        iconName: "article"
                        enabled: !panel.ai.busy
                        text: qsTr("Captions from a script")
                        onClicked: scriptDialog.open()
                    }
                    Button {
                        objectName: "captionImportEmpty"
                        Layout.alignment: Qt.AlignHCenter
                        variant: "tonal"
                        iconName: "upload_file"
                        text: qsTr("Import a subtitle file")
                        onClicked: importDialog.open()
                    }
                    Button {
                        objectName: "captionTypeEmpty"
                        Layout.alignment: Qt.AlignHCenter
                        variant: "text"
                        iconName: "add_comment"
                        text: qsTr("Type the captions")
                        onClicked: panel.captions.addLine()
                    }
                    Item { Layout.fillHeight: true }
                }

                ColumnLayout {
                    anchors.fill: parent
                    visible: panel.captions.hasCaptions
                    spacing: Theme.space.sm

                    // Find, and replace in every line.
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.space.xs
                        SearchBar {
                            id: find
                            objectName: "captionFind"
                            Layout.fillWidth: true
                            compact: true
                            placeholderText: qsTr("Find in the captions")
                        }
                        Tool {
                            objectName: "captionReplaceToggle"
                            iconName: "find_replace"
                            checkable: true
                            checked: replaceRow.visible
                            label: qsTr("Replace")
                            onClicked: replaceRow.visible = !replaceRow.visible
                        }
                    }
                    RowLayout {
                        id: replaceRow
                        Layout.fillWidth: true
                        visible: false
                        spacing: Theme.space.xs
                        TextField {
                            id: replacement
                            objectName: "captionReplacement"
                            Layout.fillWidth: true
                            placeholderText: qsTr("Replace with")
                        }
                        Button {
                            // The lines are read again when they change.
                            readonly property int matchCount: panel.captions.lines, panel.captions.countMatches(find.text)
                            objectName: "captionReplaceAll"
                            variant: "tonal"
                            enabled: matchCount > 0
                            text: qsTr("Replace all (%1)").arg(matchCount)
                            onClicked: panel.captions.replaceAll(find.text, replacement.text)
                        }
                    }

                    ListView {
                        id: lineList
                        objectName: "captionLines"
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        spacing: Theme.space.xxs
                        boundsBehavior: Flickable.StopAtBounds
                        ScrollBar.vertical: ScrollBar {}
                        model: panel.captions.lines
                        currentIndex: panel.captions.currentIndex
                        onCurrentIndexChanged: if (currentIndex >= 0 && !lineEditing) positionViewAtIndex(currentIndex, ListView.Contain)
                        property bool lineEditing: false

                        delegate: Rectangle {
                            id: row
                            required property var modelData
                            required property int index
                            readonly property bool current: index === panel.captions.currentIndex
                            readonly property bool matches: find.text.trim() !== ""
                                                            && modelData.text.toLowerCase().includes(find.text.trim().toLowerCase())
                            objectName: "captionLine_" + index
                            width: ListView.view.width
                            height: content.implicitHeight + Theme.space.sm * 2
                            radius: Theme.shape.small
                            color: current ? Theme.color.secondaryContainer
                                 : matches ? Theme.alpha(Theme.color.tertiary, 0.16)
                                 : hover.hovered ? Theme.color.surfaceContainerHigh : "transparent"

                            HoverHandler { id: hover }
                            TapHandler { onTapped: panel.captions.seekToLine(row.index) }

                            RowLayout {
                                id: content
                                anchors.fill: parent
                                anchors.margins: Theme.space.sm
                                spacing: Theme.space.sm
                                Label {
                                    Layout.alignment: Qt.AlignTop
                                    Layout.topMargin: Theme.space.xs
                                    role: "labelSmall"
                                    font.features: { "tnum": 1 }
                                    color: row.current ? Theme.color.onSecondaryContainer : Theme.color.onSurfaceVariant
                                    text: row.modelData.time
                                }
                                ColumnLayout {
                                Layout.fillWidth: true
                                spacing: Theme.space.xxs
                                TextEdit {
                                    id: lineText
                                    objectName: "captionLineText_" + row.index
                                    Layout.fillWidth: true
                                    Layout.topMargin: Theme.space.xxs
                                    wrapMode: TextEdit.Wrap
                                    selectByMouse: true
                                    readOnly: panel.captions.locked
                                    font: Theme.type.bodyMedium
                                    color: row.current ? Theme.color.onSecondaryContainer : Theme.color.onSurface
                                    selectionColor: Theme.alpha(Theme.color.primary, 0.4)
                                    text: row.modelData.text
                                    Accessible.role: Accessible.EditableText
                                    Accessible.name: qsTr("Caption line %1").arg(row.index + 1)
                                    onActiveFocusChanged: {
                                        lineList.lineEditing = activeFocus
                                        if (activeFocus)
                                            panel.captions.seekToLine(row.index)
                                        else if (text !== row.modelData.text)
                                            panel.captions.setLineText(row.index, text)
                                    }
                                    Keys.onReturnPressed: {
                                        panel.captions.setLineText(row.index, text)
                                        lineList.forceActiveFocus()
                                    }
                                    Keys.onEscapePressed: {
                                        text = row.modelData.text
                                        lineList.forceActiveFocus()
                                    }
                                }
                                // Bilingual captions: the line in the other language, edited the same way.
                                TextEdit {
                                    objectName: "captionLineTranslation_" + row.index
                                    Layout.fillWidth: true
                                    visible: row.modelData.translation !== ""
                                    wrapMode: TextEdit.Wrap
                                    selectByMouse: true
                                    readOnly: panel.captions.locked
                                    font: Theme.type.bodySmall
                                    color: row.current ? Theme.color.onSecondaryContainer : Theme.color.onSurfaceVariant
                                    selectionColor: Theme.alpha(Theme.color.primary, 0.4)
                                    text: row.modelData.translation
                                    Accessible.role: Accessible.EditableText
                                    Accessible.name: qsTr("Translation of caption line %1").arg(row.index + 1)
                                    onActiveFocusChanged: {
                                        lineList.lineEditing = activeFocus
                                        if (!activeFocus && text !== row.modelData.translation)
                                            panel.captions.setLineTranslation(row.index, text)
                                    }
                                    Keys.onReturnPressed: {
                                        panel.captions.setLineTranslation(row.index, text)
                                        lineList.forceActiveFocus()
                                    }
                                    Keys.onEscapePressed: {
                                        text = row.modelData.translation
                                        lineList.forceActiveFocus()
                                    }
                                }
                                }
                                Row {
                                    Layout.alignment: Qt.AlignTop
                                    spacing: 0
                                    opacity: hover.hovered || row.current ? 1 : 0
                                    visible: !panel.captions.locked
                                    Tool {
                                        objectName: "captionSplit_" + row.index
                                        iconName: "call_split"
                                        label: qsTr("Split the line at the playhead")
                                        onClicked: panel.captions.splitLine(row.index)
                                    }
                                    Tool {
                                        objectName: "captionJoin_" + row.index
                                        visible: row.index < panel.captions.lines.length - 1
                                        iconName: "merge"
                                        label: qsTr("Join with the next line")
                                        onClicked: panel.captions.joinWithNext(row.index)
                                    }
                                    Tool {
                                        objectName: "captionDelete_" + row.index
                                        iconName: "delete"
                                        label: qsTr("Delete the line")
                                        onClicked: panel.captions.deleteLine(row.index)
                                    }
                                }
                            }
                        }
                    }

                    // Add a line at the playhead; move all the lines to match the speech.
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.space.xs
                        Button {
                            objectName: "captionAddLine"
                            variant: "text"
                            iconName: "add_comment"
                            enabled: !panel.captions.locked
                            text: qsTr("Add a line")
                            onClicked: panel.captions.addLine()
                        }
                        Item { Layout.fillWidth: true }
                        Label {
                            role: "labelSmall"
                            color: Theme.color.onSurfaceVariant
                            text: qsTr("Timing")
                        }
                        Tool {
                            objectName: "captionEarlier"
                            iconName: "fast_rewind"
                            enabled: !panel.captions.locked
                            label: qsTr("All the captions 0.1 s earlier")
                            onClicked: panel.captions.shiftAll(-0.1)
                        }
                        Tool {
                            objectName: "captionLater"
                            iconName: "fast_forward"
                            enabled: !panel.captions.locked
                            label: qsTr("All the captions 0.1 s later")
                            onClicked: panel.captions.shiftAll(0.1)
                        }
                    }
                }
            }

            // ---- Styles ----
            ColumnLayout {
                spacing: Theme.space.sm

                AssetLibraryModel {
                    id: styles
                    kind: AssetLibraryModel.CaptionStyles
                }

                Flickable {
                    Layout.fillWidth: true
                    implicitHeight: chips.implicitHeight
                    contentWidth: chips.implicitWidth
                    clip: true
                    flickableDirection: Flickable.HorizontalFlick
                    boundsBehavior: Flickable.StopAtBounds
                    Row {
                        id: chips
                        spacing: Theme.space.xs
                        Chip {
                            text: qsTr("All")
                            checkable: false
                            checked: styles.category === ""
                            onClicked: styles.category = ""
                        }
                        Repeater {
                            model: styles.categories
                            delegate: Chip {
                                required property var modelData
                                text: modelData.name
                                checkable: false
                                checked: styles.category === modelData.id
                                onClicked: styles.category = modelData.id
                            }
                        }
                    }
                }

                Label {
                    Layout.fillWidth: true
                    visible: !panel.captions.hasCaptions
                    wrapMode: Text.WordWrap
                    role: "bodySmall"
                    color: Theme.color.onSurfaceVariant
                    text: qsTr("Choose the look now: the captions you add get it.")
                }

                GridView {
                    id: grid
                    objectName: "captionStyleGrid"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: styles
                    cellWidth: Math.floor(width / Math.max(1, Math.floor(width / (Theme.editor.assetTileWidth + Theme.space.sm))))
                    cellHeight: Theme.editor.assetTileHeight + Theme.space.xl + Theme.space.sm
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar {}

                    delegate: Item {
                        id: tile
                        required property string assetId
                        required property string name
                        readonly property bool applied: panel.captions.styleId === assetId

                        objectName: "captionStyle_" + assetId
                        width: grid.cellWidth
                        height: grid.cellHeight
                        Accessible.role: Accessible.Button
                        Accessible.name: name

                        MouseArea {
                            id: mouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onContainsMouseChanged: containsMouse ? panel.captions.previewStyle(tile.assetId)
                                                                  : panel.captions.clearPreview()
                            onClicked: panel.captions.applyStyle(tile.assetId)

                            Rectangle {
                                id: frame
                                anchors.horizontalCenter: parent.horizontalCenter
                                width: grid.cellWidth - Theme.space.sm
                                height: Theme.editor.assetTileHeight
                                radius: Theme.shape.small
                                color: Theme.color.surfaceContainerHigh
                                border.width: tile.applied ? Theme.editor.selectionBorder : 0
                                border.color: Theme.color.primary
                                clip: true

                                AssetThumbnail {
                                    anchors.fill: parent
                                    anchors.margins: tile.applied ? Theme.editor.selectionBorder : 0
                                    editor: panel.editor
                                    kind: AssetLibraryModel.CaptionStyles
                                    assetId: tile.assetId
                                    progress: 0.5
                                    NumberAnimation on progress {
                                        running: mouse.containsMouse
                                        from: 0
                                        to: 1
                                        duration: Theme.motion.long4 * 2
                                        loops: Animation.Infinite
                                    }
                                }
                            }
                            Label {
                                anchors.top: frame.bottom
                                anchors.topMargin: Theme.space.xxs
                                width: frame.width
                                x: frame.x
                                horizontalAlignment: Text.AlignHCenter
                                elide: Text.ElideRight
                                role: "labelMedium"
                                color: tile.applied ? Theme.color.primary : Theme.color.onSurface
                                text: tile.name
                            }
                        }
                    }
                }
            }
        }
    }
}
