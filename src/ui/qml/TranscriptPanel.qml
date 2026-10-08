// Editing by the transcript (SPEC §5.8, Phase 6): what is said in the main track, sentence by sentence; a click plays
// a word, shift-click selects up to it, Delete (or the button) cuts the selected words out of the video — the rest
// stays together. "Remove filler words" takes out every "ehm" at once. Everything is ordinary cuts, undoable.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Velacut.Components
import Velacut.Theme
import Velacut.UI

Item {
    id: panel

    required property Editor editor
    readonly property TranscriptEditor transcript: editor.transcript
    readonly property AiTools ai: editor.ai
    // Speech recognition is missing something: Preferences → AI models.
    signal setUpRequested()

    property int anchorIndex: -1
    property int focusIndex: -1
    readonly property int selectionLow: Math.min(anchorIndex, focusIndex)
    readonly property int selectionHigh: Math.max(anchorIndex, focusIndex)
    readonly property bool hasSelection: anchorIndex >= 0 && focusIndex >= 0
    readonly property var languages: [{ code: "auto", text: qsTr("Find the language") }, { code: "it", text: "Italiano" },
                                      { code: "en", text: "English" }, { code: "es", text: "Español" },
                                      { code: "fr", text: "Français" }, { code: "de", text: "Deutsch" },
                                      { code: "pt", text: "Português" }]
    property int language: 0

    function select(index, extend) {
        if (extend && anchorIndex >= 0) {
            focusIndex = index
        } else {
            anchorIndex = index
            focusIndex = index
            transcript.seekToWord(index)
        }
        words.forceActiveFocus()
    }
    function clearSelection() {
        anchorIndex = -1
        focusIndex = -1
    }
    function cutSelection() {
        if (hasSelection && transcript.deleteWords(selectionLow, selectionHigh))
            clearSelection()
    }

    component Tool: IconButton {
        implicitWidth: Theme.editor.toolButtonSize
        implicitHeight: Theme.editor.toolButtonSize
        iconSize: Theme.editor.toolIconSize
    }
    // Language and "Transcribe", or what is missing for it.
    component TranscribeControls: ColumnLayout {
        spacing: Theme.space.sm
        ComboBox {
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: Theme.editor.panelMinimumWidth
            visible: panel.ai.speechStatus === 0
            model: panel.languages.map(l => l.text)
            currentIndex: panel.language
            displayText: model[currentIndex] ?? ""
            Accessible.name: qsTr("Language of the speech")
            onActivated: (index) => panel.language = index
        }
        Button {
            objectName: "transcribeButton"
            Layout.alignment: Qt.AlignHCenter
            variant: "filled"
            iconName: "description"
            enabled: panel.ai.speechStatus === 0 && !panel.ai.busy
            text: qsTr("Transcribe")
            onClicked: panel.ai.transcribe(panel.languages[panel.language].code)
        }
        Label {
            Layout.fillWidth: true
            visible: panel.ai.speechStatus !== 0
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            role: "bodySmall"
            color: Theme.color.onSurfaceVariant
            text: panel.ai.speechStatus === 1
                  ? qsTr("The transcript needs whisper.cpp, which is not installed: “%1”.").arg(panel.ai.speechInstallCommand)
                  : qsTr("The transcript needs a speech model: download one once, it stays on this computer.")
        }
        Button {
            Layout.alignment: Qt.AlignHCenter
            visible: panel.ai.speechStatus !== 0
            variant: "text"
            iconName: "neurology"
            text: panel.ai.speechStatus === 1 ? qsTr("How to install") : qsTr("Download a model")
            onClicked: panel.setUpRequested()
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.space.md
        anchors.topMargin: 0
        spacing: Theme.space.sm

        LibraryHeader {
            Layout.fillWidth: true
            title: qsTr("Transcript")
            Tool {
                objectName: "chaptersButton"
                anchors.verticalCenter: parent.verticalCenter
                visible: panel.transcript.available
                iconName: "toc"
                label: qsTr("Chapters for YouTube: markers on the timeline and the list for the description")
                onClicked: panel.transcript.makeChapters()
            }
            Tool {
                objectName: "removeFillersButton"
                anchors.verticalCenter: parent.verticalCenter
                visible: panel.transcript.fillerCount > 0
                iconName: "voice_over_off"
                label: qsTr("Remove %n filler word(s)", "", panel.transcript.fillerCount)
                onClicked: panel.transcript.removeFillerWords()
            }
        }

        // Nothing transcribed yet.
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: !panel.transcript.available
            spacing: Theme.space.md
            Item { Layout.fillHeight: true }
            Icon {
                Layout.alignment: Qt.AlignHCenter
                name: "description"
                size: Theme.space.xxxl
                color: Theme.color.primary
            }
            Label {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                role: "bodyMedium"
                color: Theme.color.onSurfaceVariant
                text: qsTr("Transcribe the video, then cut it like a text: delete words and they are gone from the video.")
            }
            TranscribeControls { Layout.fillWidth: true }
            Item { Layout.fillHeight: true }
        }

        // Some clips are not transcribed yet (added after the transcript).
        Rectangle {
            Layout.fillWidth: true
            visible: panel.transcript.available && panel.transcript.incomplete
            implicitHeight: banner.implicitHeight + 2 * Theme.space.sm
            radius: Theme.shape.small
            color: Theme.color.secondaryContainer
            RowLayout {
                id: banner
                anchors.fill: parent
                anchors.margins: Theme.space.sm
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    role: "bodySmall"
                    color: Theme.color.onSecondaryContainer
                    text: qsTr("Some clips are not in the transcript yet.")
                }
                Button {
                    variant: "text"
                    enabled: panel.ai.speechStatus === 0 && !panel.ai.busy
                    text: qsTr("Transcribe")
                    onClicked: panel.ai.transcribe(panel.languages[panel.language].code)
                }
            }
        }

        ListView {
            id: words
            objectName: "transcriptWords"
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: panel.transcript.available
            clip: true
            spacing: Theme.space.sm
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
            model: panel.transcript.paragraphs
            focus: true
            activeFocusOnTab: true
            Keys.onDeletePressed: panel.cutSelection()
            Keys.onBackPressed: panel.cutSelection()
            Keys.onEscapePressed: panel.clearSelection()

            delegate: Flow {
                id: paragraph
                required property var modelData
                width: ListView.view.width
                spacing: Theme.space.xxs
                Repeater {
                    model: paragraph.modelData.words
                    delegate: Rectangle {
                        id: word
                        required property var modelData
                        required property int index
                        readonly property int globalIndex: paragraph.modelData.first + index
                        readonly property bool selected: panel.hasSelection && globalIndex >= panel.selectionLow
                                                         && globalIndex <= panel.selectionHigh
                        readonly property bool current: globalIndex === panel.transcript.currentIndex
                        objectName: "word_" + globalIndex
                        width: text.implicitWidth + Theme.space.xs
                        height: text.implicitHeight + Theme.space.xxs
                        radius: Theme.shape.extraSmall
                        color: selected ? Theme.color.primaryContainer
                             : current ? Theme.color.secondaryContainer
                             : hover.hovered ? Theme.color.surfaceContainerHigh : "transparent"
                        Accessible.role: Accessible.StaticText
                        Accessible.name: modelData.text
                        Label {
                            id: text
                            anchors.centerIn: parent
                            role: "bodyMedium"
                            font.italic: word.modelData.filler
                            color: word.selected ? Theme.color.onPrimaryContainer
                                 : word.modelData.filler ? Theme.color.onSurfaceVariant : Theme.color.onSurface
                            text: word.modelData.text
                        }
                        MouseArea {
                            id: hover
                            readonly property bool hovered: containsMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: (mouse) => panel.select(word.globalIndex, mouse.modifiers & Qt.ShiftModifier)
                        }
                    }
                }
            }
        }

        // The selection: how many words, and the cut.
        RowLayout {
            Layout.fillWidth: true
            visible: panel.hasSelection
            spacing: Theme.space.xs
            Button {
                objectName: "cutWordsButton"
                Layout.fillWidth: true
                variant: "filled"
                iconName: "content_cut"
                text: qsTr("Cut %n word(s) from the video", "", panel.selectionHigh - panel.selectionLow + 1)
                onClicked: panel.cutSelection()
            }
            Tool {
                iconName: "close"
                label: qsTr("Clear the selection")
                onClicked: panel.clearSelection()
            }
        }
    }
}
