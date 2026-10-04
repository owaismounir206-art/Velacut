// Brand kit (SPEC §5.13ter, §4 "Kit del marchio"): the kit's colours, logos, fonts, intro and outro and music, saved
// once (App.brandKits) and used in this project with a click: a logo at the playhead or as a watermark, the intro at
// the start, the outro at the end, a song under the video, a text in the brand's font and colour. Several kits.
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
    readonly property BrandKitModel kits: App.brandKits
    readonly property bool hasKit: kits.count > 0
    readonly property bool textSelected: editor.inspector.kind === Inspector.Text

    component Section: ColumnLayout {
        property string title
        property string detail
        default property alias content: body.data
        Layout.fillWidth: true
        spacing: Theme.space.sm
        Label {
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
    component Tool: IconButton {
        implicitWidth: Theme.editor.toolButtonSize
        implicitHeight: Theme.editor.toolButtonSize
        iconSize: Theme.editor.toolIconSize
    }
    function report(error, done) {
        App.message(error !== "" ? error : done)
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.space.md
        anchors.topMargin: 0
        spacing: Theme.space.sm

        LibraryHeader {
            Layout.fillWidth: true
            title: qsTr("Brand kit")
            IconButton {
                anchors.verticalCenter: parent.verticalCenter
                visible: panel.hasKit
                implicitWidth: Theme.editor.toolButtonSize
                implicitHeight: Theme.editor.toolButtonSize
                iconSize: Theme.editor.toolIconSize
                iconName: "more_vert"
                label: qsTr("Kit options")
                onClicked: kitMenu.popup()
                Menu {
                    id: kitMenu
                    MenuItem {
                        iconName: "edit"
                        text: qsTr("Rename the kit")
                        onTriggered: renameDialog.ask()
                    }
                    MenuItem {
                        iconName: "delete"
                        text: qsTr("Delete the kit")
                        onTriggered: {
                            const name = panel.kits.name
                            const id = panel.kits.removeKit()
                            if (id !== "")
                                snackbar.show(qsTr("Kit “%1” deleted").arg(name), qsTr("Undo"), () => panel.kits.restoreKit(id))
                        }
                    }
                }
            }
        }

        // No kit yet: what it is for, and the button that makes one.
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: !panel.hasKit
            spacing: Theme.space.md
            Item { Layout.fillHeight: true }
            Icon {
                Layout.alignment: Qt.AlignHCenter
                name: "verified"
                size: Theme.space.xxxl
                color: Theme.color.primary
            }
            Label {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                role: "bodyMedium"
                color: Theme.color.onSurfaceVariant
                text: qsTr("Save your logo, colours, fonts, intro and outro once: use them in every project with a click.")
            }
            Button {
                objectName: "createBrandKit"
                Layout.alignment: Qt.AlignHCenter
                variant: "filled"
                iconName: "add"
                text: qsTr("Create your kit")
                onClicked: panel.kits.createKit(qsTr("My brand"))
            }
            Item { Layout.fillHeight: true }
        }

        // The kits, then the kit in use.
        Flickable {
            Layout.fillWidth: true
            visible: panel.hasKit
            implicitHeight: kitChips.implicitHeight
            contentWidth: kitChips.implicitWidth
            clip: true
            flickableDirection: Flickable.HorizontalFlick
            boundsBehavior: Flickable.StopAtBounds
            Row {
                id: kitChips
                spacing: Theme.space.xs
                Repeater {
                    model: panel.kits
                    delegate: Chip {
                        required property string name
                        required property int index
                        text: name
                        checkable: false
                        checked: panel.kits.current === index
                        onClicked: panel.kits.current = index
                    }
                }
                Chip {
                    variant: "assist"
                    iconName: "add"
                    text: qsTr("New kit")
                    onClicked: panel.kits.createKit(qsTr("Brand %1").arg(panel.kits.count + 1))
                }
            }
        }

        ScrollView {
            id: scroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: panel.hasKit
            contentWidth: availableWidth
            clip: true

            ColumnLayout {
                width: scroll.availableWidth
                spacing: Theme.space.lg

                Button {
                    objectName: "addBrandText"
                    Layout.fillWidth: true
                    variant: "tonal"
                    iconName: "title"
                    text: qsTr("Text in the brand's style")
                    onClicked: panel.editor.addBrandText(panel.kits.fonts[0] ?? "", panel.kits.colors[0] ?? "")
                }

                Section {
                    title: qsTr("Colours")
                    detail: panel.textSelected ? qsTr("A click colours the selected text. They come first in every colour picker.")
                                               : qsTr("They come first in every colour picker.")
                    Flow {
                        Layout.fillWidth: true
                        spacing: Theme.space.xs
                        Repeater {
                            model: panel.kits.colors
                            delegate: Rectangle {
                                id: swatch
                                required property color modelData
                                required property int index
                                width: Theme.editor.swatchSize + Theme.space.sm
                                height: width
                                radius: Theme.shape.full
                                color: modelData
                                border.width: Theme.editor.hairline
                                border.color: Theme.color.outline
                                Accessible.role: Accessible.Button
                                Accessible.name: modelData.toString()
                                MouseArea {
                                    anchors.fill: parent
                                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                                    cursorShape: Qt.PointingHandCursor
                                    hoverEnabled: true
                                    ToolTip.visible: containsMouse
                                    ToolTip.delay: 600
                                    ToolTip.text: qsTr("%1 · right click removes it").arg(swatch.modelData.toString().toUpperCase())
                                    onClicked: (mouse) => {
                                        if (mouse.button === Qt.RightButton)
                                            panel.kits.removeColor(swatch.index)
                                        else if (panel.textSelected)
                                            panel.editor.inspector.set("text.color", swatch.modelData)
                                    }
                                }
                            }
                        }
                        Tool {
                            objectName: "addBrandColor"
                            iconName: "add"
                            variant: "outlined"
                            label: qsTr("Add a colour")
                            onClicked: colorDialog.open()
                        }
                    }
                }

                Section {
                    title: qsTr("Logos")
                    Flow {
                        Layout.fillWidth: true
                        spacing: Theme.space.sm
                        Repeater {
                            model: panel.kits.logos
                            delegate: Rectangle {
                                id: logo
                                required property url modelData
                                required property int index
                                width: Theme.editor.assetTileWidth
                                height: Theme.editor.assetTileHeight
                                radius: Theme.shape.small
                                color: Theme.color.surfaceContainerHighest
                                Image {
                                    anchors.fill: parent
                                    anchors.margins: Theme.space.sm
                                    source: logo.modelData
                                    sourceSize: Qt.size(width * 2, height * 2)
                                    fillMode: Image.PreserveAspectFit
                                    asynchronous: true
                                }
                                HoverHandler { id: logoHover }
                                Row {
                                    anchors.right: parent.right
                                    anchors.bottom: parent.bottom
                                    anchors.margins: Theme.space.xxs
                                    visible: logoHover.hovered
                                    spacing: Theme.space.xxs
                                    Tool {
                                        variant: "filled"
                                        iconName: "add"
                                        label: qsTr("Add at the playhead")
                                        onClicked: panel.editor.addLogo(logo.modelData)
                                    }
                                    Tool {
                                        variant: "tonal"
                                        iconName: "branding_watermark"
                                        label: qsTr("Watermark over the whole video")
                                        onClicked: panel.editor.addWatermark(logo.modelData)
                                    }
                                }
                                Tool {
                                    anchors.right: parent.right
                                    anchors.top: parent.top
                                    visible: logoHover.hovered
                                    iconName: "close"
                                    label: qsTr("Remove from the kit")
                                    onClicked: panel.kits.removeLogo(logo.index)
                                }
                            }
                        }
                        Rectangle {
                            width: Theme.editor.assetTileWidth
                            height: Theme.editor.assetTileHeight
                            radius: Theme.shape.small
                            color: "transparent"
                            border.width: Theme.editor.hairline
                            border.color: Theme.color.outline
                            Accessible.role: Accessible.Button
                            Accessible.name: qsTr("Add a logo")
                            Column {
                                anchors.centerIn: parent
                                spacing: Theme.space.xxs
                                Icon {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    name: "add_photo_alternate"
                                    color: Theme.color.onSurfaceVariant
                                }
                                Label {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    role: "labelSmall"
                                    color: Theme.color.onSurfaceVariant
                                    text: qsTr("Logo")
                                }
                            }
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: logoDialog.open()
                            }
                        }
                    }
                }

                Section {
                    title: qsTr("Fonts")
                    Repeater {
                        model: panel.kits.fonts
                        delegate: RowLayout {
                            id: fontRow
                            required property string modelData
                            required property int index
                            Layout.fillWidth: true
                            spacing: Theme.space.sm
                            Label {
                                Layout.fillWidth: true
                                text: fontRow.modelData
                                font.family: fontRow.modelData
                                font.pixelSize: Theme.type.titleMedium.pixelSize
                                elide: Text.ElideRight
                            }
                            Tool {
                                iconName: panel.textSelected ? "format_paint" : "add"
                                label: panel.textSelected ? qsTr("Use for the selected text") : qsTr("A text in this font")
                                onClicked: {
                                    if (panel.textSelected)
                                        panel.editor.inspector.set("text.font", fontRow.modelData)
                                    else
                                        panel.editor.addBrandText(fontRow.modelData, panel.kits.colors[0] ?? "")
                                }
                            }
                            Tool {
                                iconName: "close"
                                label: qsTr("Remove from the kit")
                                onClicked: panel.kits.removeFont(fontRow.index)
                            }
                        }
                    }
                    ComboBox {
                        objectName: "addBrandFont"
                        Layout.fillWidth: true
                        model: Qt.fontFamilies()
                        displayText: qsTr("Add a font…")
                        currentIndex: -1
                        Accessible.name: qsTr("Add a font")
                        onActivated: (index) => panel.kits.addFont(model[index])
                    }
                }

                Section {
                    title: qsTr("Intro and outro")
                    detail: qsTr("Short clips that open and close your videos.")
                    Repeater {
                        model: [{ key: "intro", label: qsTr("Intro"), url: panel.kits.intro, put: qsTr("Add at the start") },
                                { key: "outro", label: qsTr("Outro"), url: panel.kits.outro, put: qsTr("Add at the end") }]
                        delegate: RowLayout {
                            id: endRow
                            required property var modelData
                            Layout.fillWidth: true
                            spacing: Theme.space.sm
                            Icon {
                                name: endRow.modelData.key === "intro" ? "start" : "keyboard_tab"
                                size: Theme.editor.toolIconSize
                                color: Theme.color.onSurfaceVariant
                            }
                            Label {
                                Layout.fillWidth: true
                                role: "bodyMedium"
                                elide: Text.ElideMiddle
                                text: endRow.modelData.url.toString() !== ""
                                      ? endRow.modelData.label + " · " + App.localPath(endRow.modelData.url).split("/").pop()
                                      : endRow.modelData.label
                            }
                            Button {
                                visible: endRow.modelData.url.toString() !== ""
                                implicitHeight: Theme.editor.toolButtonSize
                                variant: "tonal"
                                text: endRow.modelData.put
                                onClicked: endRow.modelData.key === "intro" ? panel.editor.addIntro(endRow.modelData.url)
                                                                            : panel.editor.addOutro(endRow.modelData.url)
                            }
                            Tool {
                                iconName: endRow.modelData.url.toString() !== "" ? "close" : "upload_file"
                                label: endRow.modelData.url.toString() !== "" ? qsTr("Remove from the kit") : qsTr("Choose a clip")
                                onClicked: {
                                    if (endRow.modelData.url.toString() !== "") {
                                        if (endRow.modelData.key === "intro")
                                            panel.kits.clearIntro()
                                        else
                                            panel.kits.clearOutro()
                                    } else {
                                        clipDialog.slot = endRow.modelData.key
                                        clipDialog.open()
                                    }
                                }
                            }
                        }
                    }
                }

                Section {
                    title: qsTr("Music")
                    Repeater {
                        model: panel.kits.music
                        delegate: RowLayout {
                            id: songRow
                            required property var modelData
                            required property int index
                            Layout.fillWidth: true
                            spacing: Theme.space.sm
                            Icon { name: "music_note"; size: Theme.editor.toolIconSize; color: Theme.color.onSurfaceVariant }
                            Label {
                                Layout.fillWidth: true
                                role: "bodyMedium"
                                elide: Text.ElideRight
                                text: songRow.modelData.name
                            }
                            Tool {
                                variant: "tonal"
                                iconName: "add"
                                label: qsTr("Add under the video")
                                onClicked: panel.editor.addBrandMusic(songRow.modelData.url)
                            }
                            Tool {
                                iconName: "close"
                                label: qsTr("Remove from the kit")
                                onClicked: panel.kits.removeMusic(songRow.index)
                            }
                        }
                    }
                    Button {
                        variant: "text"
                        iconName: "library_music"
                        text: qsTr("Add music to the kit…")
                        onClicked: musicDialog.open()
                    }
                }
                Item { implicitHeight: Theme.space.md }
            }
        }
    }

    Snackbar {
        id: snackbar
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Theme.space.md
        width: Math.min(implicitWidth, parent.width - 2 * Theme.space.md)
        z: 10
    }

    ColorDialog {
        id: colorDialog
        title: qsTr("A colour of the brand")
        onAccepted: panel.kits.addColor(selectedColor)
    }
    FileDialog {
        id: logoDialog
        title: qsTr("Add a logo")
        nameFilters: [qsTr("Pictures (%1)").arg("*.png *.svg *.webp *.jpg *.jpeg"), qsTr("All files (*)")]
        onAccepted: panel.report(panel.kits.addLogo(selectedFile), qsTr("Logo added to the kit"))
    }
    FileDialog {
        id: clipDialog
        property string slot
        title: slot === "intro" ? qsTr("Choose the intro") : qsTr("Choose the outro")
        nameFilters: [qsTr("Videos and photos (%1)").arg("*.mp4 *.mov *.mkv *.webm *.png *.jpg *.jpeg"), qsTr("All files (*)")]
        onAccepted: panel.report(slot === "intro" ? panel.kits.setIntro(selectedFile) : panel.kits.setOutro(selectedFile),
                                 slot === "intro" ? qsTr("Intro saved in the kit") : qsTr("Outro saved in the kit"))
    }
    FileDialog {
        id: musicDialog
        title: qsTr("Add music to the kit")
        nameFilters: [qsTr("Music (%1)").arg("*.mp3 *.wav *.flac *.aac *.ogg *.opus *.m4a"), qsTr("All files (*)")]
        onAccepted: panel.report(panel.kits.addMusic(selectedFile), qsTr("Music added to the kit"))
    }
    Dialog {
        id: renameDialog
        function ask() {
            nameField.text = panel.kits.name
            open()
            nameField.selectAll()
            nameField.forceActiveFocus()
        }
        title: qsTr("Rename the kit")
        iconName: "edit"
        anchors.centerIn: Overlay.overlay
        width: Math.min(Theme.editor.dialogWidth, panel.Window.window ? panel.Window.window.width - 2 * Theme.space.xl : Theme.editor.dialogWidth)
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: panel.kits.renameKit(nameField.text)
        TextField {
            id: nameField
            width: parent.width
            label: qsTr("Name")
            onAccepted: renameDialog.accept()
        }
    }
}
