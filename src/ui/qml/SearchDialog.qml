// Universal search (Ctrl+K, SPEC 0bis rule 14): commands, filters, transitions, text styles, animations, music and
// media of the project. Arrows choose, Enter applies or opens, Esc closes.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Vedit.Components
import Vedit.Theme
import Vedit.UI

Popup {
    id: popup

    required property Editor editor
    property var results: []

    function refresh() {
        results = editor.actions.search(field.text)
        list.currentIndex = results.length > 0 ? 0 : -1
    }
    function run(index) {
        const result = results[index]
        if (!result || !result.enabled)
            return
        close()
        editor.actions.activate(result.kind, result.id)
    }

    objectName: "universalSearch"
    modal: true
    focus: true
    width: Math.min(parent.width - 2 * Theme.space.xl, Theme.editor.dialogWidth)
    height: Math.min(parent.height - 2 * Theme.space.xxxl, Theme.editor.dialogWidth)
    x: (parent.width - width) / 2
    y: Theme.space.xxxl
    padding: Theme.space.md
    onOpened: {
        field.text = ""
        refresh()
        field.forceActiveFocus()
    }

    background: Rectangle {
        radius: Theme.shape.extraLarge
        color: Theme.color.surfaceContainerHigh
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: Theme.space.sm

        SearchBar {
            id: field
            objectName: "universalSearchField"
            Layout.fillWidth: true
            placeholderText: qsTr("Search commands, filters, transitions, texts, music…")
            Accessible.name: qsTr("Search")
            onTextChanged: popup.refresh()
            Keys.onDownPressed: list.incrementCurrentIndex()
            Keys.onUpPressed: list.decrementCurrentIndex()
            Keys.onReturnPressed: popup.run(list.currentIndex)
            Keys.onEnterPressed: popup.run(list.currentIndex)
            Keys.onEscapePressed: popup.close()
        }
        ListView {
            id: list
            objectName: "universalSearchResults"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: popup.results
            boundsBehavior: Flickable.StopAtBounds
            highlightMoveDuration: 0
            ScrollBar.vertical: ScrollBar {}

            delegate: Item {
                id: row
                required property var modelData
                required property int index
                readonly property bool current: ListView.isCurrentItem

                objectName: "searchResult_" + index
                width: list.width
                implicitHeight: Math.max(Theme.space.minimumTarget, content.implicitHeight + 2 * Theme.space.sm)
                opacity: modelData.enabled ? 1 : Theme.state.disabledContent
                Accessible.role: Accessible.ListItem
                Accessible.name: modelData.text

                Rectangle {
                    anchors.fill: parent
                    radius: Theme.shape.medium
                    color: row.current ? Theme.color.secondaryContainer : "transparent"
                }
                RowLayout {
                    id: content
                    anchors.fill: parent
                    anchors.leftMargin: Theme.space.md
                    anchors.rightMargin: Theme.space.md
                    spacing: Theme.space.md
                    Icon {
                        name: row.modelData.icon
                        color: row.current ? Theme.color.onSecondaryContainer : Theme.color.onSurfaceVariant
                    }
                    Label {
                        Layout.fillWidth: true
                        role: "bodyLarge"
                        elide: Text.ElideRight
                        color: row.current ? Theme.color.onSecondaryContainer : Theme.color.onSurface
                        text: row.modelData.text
                    }
                    Label {
                        role: "labelMedium"
                        color: row.current ? Theme.color.onSecondaryContainer : Theme.color.onSurfaceVariant
                        text: row.modelData.detail
                    }
                }
                MouseArea {
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onEntered: list.currentIndex = row.index
                    onClicked: popup.run(row.index)
                }
            }
        }
        Label {
            Layout.fillWidth: true
            visible: popup.results.length === 0
            horizontalAlignment: Text.AlignHCenter
            role: "bodyMedium"
            color: Theme.color.onSurfaceVariant
            text: qsTr("Nothing found")
        }
    }
}
