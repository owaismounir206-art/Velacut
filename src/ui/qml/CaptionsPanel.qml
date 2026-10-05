// Captions (SPEC §5.8, §4 "Sottotitoli"): the lines of the caption track, edited in place (a click plays the line,
// split, join, delete, find and replace, move them all to match the speech), and the social styles of the library,
// shown on the player under the pointer and applied with a click to every line. Subtitle files in (SRT, WebVTT) and out.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import Vedit.Components
import Vedit.Theme
import Vedit.UI

Item {
    id: panel

    required property Editor editor
    readonly property Captions captions: editor.captions
    property int page: 0 // 0 = lines, 1 = styles

    component Tool: IconButton {
        implicitWidth: Theme.editor.toolButtonSize
        implicitHeight: Theme.editor.toolButtonSize
        iconSize: Theme.editor.toolIconSize
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
                        text: qsTr("Bring a subtitle file, or type the captions line by line: each word lights up while it is said.")
                    }
                    Button {
                        objectName: "captionImportEmpty"
                        Layout.alignment: Qt.AlignHCenter
                        variant: "filled"
                        iconName: "upload_file"
                        text: qsTr("Import a subtitle file")
                        onClicked: importDialog.open()
                    }
                    Button {
                        objectName: "captionTypeEmpty"
                        Layout.alignment: Qt.AlignHCenter
                        variant: "tonal"
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
