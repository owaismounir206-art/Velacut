// A library of the core pack (Text, Transitions, Filters): category chips, search, and items that show on the player
// while the pointer is over them and apply with a click; a click on the one applied removes it (SPEC 0bis rules 4, 5).
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Vedit.Components
import Vedit.Theme
import Vedit.UI

Rectangle {
    id: panel

    required property Editor editor
    required property int kind // AssetLibraryModel.Kind
    property string title
    readonly property Inspector inspector: editor.inspector
    readonly property bool filters: kind === AssetLibraryModel.Filters
    readonly property bool transitions: kind === AssetLibraryModel.Transitions
    readonly property bool texts: kind === AssetLibraryModel.TextStyles
    readonly property bool animations: kind === AssetLibraryModel.Animations
    // The item applied to what the library acts on (marked in the grid).
    readonly property var current: filters ? [inspector.values["filter"] ?? ""]
                                 : transitions ? [inspector.values["transition.type"] ?? ""]
                                 : animations ? [inspector.values["animation.in"] ?? "", inspector.values["animation.out"] ?? "",
                                                 inspector.values["animation.loop"] ?? ""]
                                 : [inspector.values["text.preset"] ?? ""]

    function preview(assetId) {
        if (filters) {
            inspector.previewFilter(assetId)
        } else if (texts) {
            inspector.previewTextStyle(assetId)
        } else {
            // Transitions and animations play in a loop on the player, without moving the playhead.
            const range = transitions ? inspector.previewTransition(assetId) : inspector.previewAnimation(assetId)
            if (range.start !== undefined) {
                loop.first = range.start
                loop.last = range.end
                loop.frame = range.start
                loop.restart()
            }
        }
    }
    function endPreview() {
        if (loop.running) {
            loop.stop()
            editor.player.endSkim()
        }
        inspector.clearPreview()
    }
    function apply(assetId) {
        endPreview()
        if (filters)
            inspector.toggleFilter(assetId)
        else if (transitions)
            inspector.toggleTransition(assetId)
        else if (animations)
            inspector.toggleAnimation(assetId)
        else if (inspector.kind === Inspector.Text)
            inspector.applyTextStyle(assetId)
        else
            editor.addText(assetId)
    }
    // "+": a new text in that style; for filters and transitions the same as a click.
    function add(assetId) {
        if (texts) {
            endPreview()
            editor.addText(assetId)
        } else {
            apply(assetId)
        }
    }

    color: Theme.color.surface

    Timer {
        id: loop
        property int first
        property int last
        property int frame
        interval: Theme.motion.medium2 / 4
        repeat: true
        onTriggered: {
            const step = Math.max(1, Math.round(panel.editor.frameRate * interval / 1000))
            frame = frame + step > last ? first : frame + step
            panel.editor.player.skim(frame)
        }
    }

    AssetLibraryModel {
        id: library
        kind: panel.kind
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.space.md
        spacing: Theme.space.sm

        RowLayout {
            Layout.fillWidth: true
            Label {
                Layout.fillWidth: true
                role: "titleMedium"
                text: panel.title
            }
            Button {
                objectName: "addDefaultText"
                visible: panel.texts
                variant: "tonal"
                iconName: "add"
                text: qsTr("Default text")
                onClicked: panel.editor.addText()
            }
        }
        SearchBar {
            Layout.fillWidth: true
            placeholderText: qsTr("Search")
            onTextChanged: library.search = text
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
                    checked: library.category === ""
                    onClicked: library.category = ""
                }
                Repeater {
                    model: library.categories
                    delegate: Chip {
                        required property var modelData
                        text: modelData.name
                        checkable: false
                        checked: library.category === modelData.id
                        onClicked: library.category = modelData.id
                    }
                }
            }
        }

        // Actions on every clip or cut (SPEC 0bis rule 5, §5.11bis).
        Button {
            objectName: "filterApplyAll"
            Layout.fillWidth: true
            visible: panel.filters && panel.current[0] !== ""
            variant: "tonal"
            text: qsTr("Apply %1 to all clips").arg(library.nameOf(panel.current[0]))
            onClicked: panel.inspector.applyToAll("filter")
        }
        Flow {
            Layout.fillWidth: true
            visible: panel.transitions
            spacing: Theme.space.xs
            Button {
                objectName: "transitionApplyAll"
                visible: panel.inspector.kind === Inspector.Transition
                variant: "tonal"
                text: qsTr("Apply to all cuts")
                onClicked: panel.inspector.applyToAll("transition")
            }
            Button {
                variant: "text"
                iconName: "shuffle"
                text: qsTr("Random")
                onClicked: panel.inspector.randomTransitions()
            }
            Button {
                variant: "text"
                iconName: "delete"
                text: qsTr("Remove all")
                onClicked: panel.inspector.removeAllTransitions()
            }
        }
        Label {
            Layout.fillWidth: true
            visible: panel.filters && !panel.inspector.active && panel.editor.timeline.duration === 0
            wrapMode: Text.WordWrap
            role: "bodySmall"
            color: Theme.color.onSurfaceVariant
            text: qsTr("Add a video or a photo first: filters apply to the selected clip, or to the one on screen.")
        }

        GridView {
            id: grid
            objectName: "assetGrid"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: library
            cellWidth: Math.floor(width / Math.max(1, Math.floor(width / (Theme.editor.assetTileWidth + Theme.space.sm))))
            cellHeight: Theme.editor.assetTileHeight + Theme.space.xl + Theme.space.sm
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}

            delegate: Item {
                id: tile
                required property string assetId
                required property string name
                readonly property bool applied: panel.current.includes(assetId)

                objectName: "asset_" + assetId
                width: grid.cellWidth
                height: grid.cellHeight
                Accessible.role: Accessible.Button
                Accessible.name: name

                MouseArea {
                    id: mouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onContainsMouseChanged: containsMouse ? panel.preview(tile.assetId) : panel.endPreview()
                    onClicked: panel.apply(tile.assetId)

                    Rectangle {
                        id: frame
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: grid.cellWidth - Theme.space.sm
                        height: Theme.editor.assetTileHeight
                        radius: Theme.shape.small
                        color: Theme.color.surfaceContainerHigh
                        border.width: tile.applied ? Theme.editor.selectionBorder : 0
                        border.color: Theme.color.primary

                        AssetThumbnail {
                            anchors.fill: parent
                            anchors.margins: tile.applied ? Theme.editor.selectionBorder : 0
                            editor: panel.editor
                            kind: panel.kind
                            assetId: tile.assetId
                            // Transitions play while the pointer is over them.
                            progress: 0.5
                            NumberAnimation on progress {
                                running: (panel.transitions || panel.animations) && mouse.containsMouse
                                from: 0
                                to: 1
                                duration: Theme.motion.long4 * 2
                                loops: Animation.Infinite
                            }
                        }
                        IconButton {
                            objectName: "assetAdd_" + tile.assetId
                            anchors.right: parent.right
                            anchors.top: parent.top
                            visible: mouse.containsMouse
                            variant: "filled"
                            iconName: "add"
                            label: panel.texts ? qsTr("New text in this style") : qsTr("Apply")
                            onClicked: panel.add(tile.assetId)
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
