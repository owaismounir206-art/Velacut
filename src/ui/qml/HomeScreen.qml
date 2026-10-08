// Home screen (SPEC §4, 0bis rules 1, 2, 13): a big "New project" (no questions: the editor opens at once; files dropped
// anywhere here start a project with them), the quick ways to start, the project templates, and the drafts, which save
// themselves.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Velacut.Components
import Velacut.Theme
import Velacut.UI

Item {
    id: root

    signal infoRequested()
    signal preferencesRequested()

    Component.onCompleted: App.drafts.refresh()

    Shortcut { sequences: [StandardKey.New]; onActivated: App.newProject() }

    Rectangle {
        anchors.fill: parent
        color: Theme.color.backdrop
    }

    // Header: the name of the app, information on the right.
    Item {
        id: bar
        anchors { left: parent.left; right: parent.right; top: parent.top }
        height: Theme.editor.topBarHeight
        RowLayout {
            anchors.left: parent.left
            anchors.leftMargin: Theme.space.lg
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.space.sm
            Rectangle {
                implicitWidth: Theme.editor.toolButtonSize
                implicitHeight: implicitWidth
                radius: Theme.shape.small
                color: Theme.color.primary
                Icon {
                    anchors.centerIn: parent
                    name: "movie_edit"
                    size: Theme.editor.toolIconSize
                    filled: true
                    color: Theme.color.onPrimary
                }
            }
            Label {
                role: "titleMedium"
                text: "velacut"
            }
        }
        Row {
            anchors.right: parent.right
            anchors.rightMargin: Theme.space.sm
            anchors.verticalCenter: parent.verticalCenter
            IconButton {
                objectName: "homePreferencesButton"
                iconName: "settings"
                label: qsTr("Preferences")
                shortcutText: qsTr("Ctrl+,")
                onClicked: root.preferencesRequested()
            }
            IconButton {
                iconName: "info"
                label: qsTr("System information")
                onClicked: root.infoRequested()
            }
        }
    }

    Flickable {
        id: scroller
        anchors { left: parent.left; right: parent.right; top: bar.bottom; bottom: parent.bottom }
        contentWidth: width
        contentHeight: content.implicitHeight + 2 * Theme.space.xl
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {}

        ColumnLayout {
            id: content
            width: Math.min(scroller.width - 2 * Theme.space.xl, Theme.editor.contentMaxWidth)
            x: (scroller.width - width) / 2
            y: Theme.space.lg
            spacing: Theme.space.xl

            ColumnLayout {
                Layout.fillWidth: true
                spacing: Theme.space.md

                // "New project": the most important action, the largest target.
                Item {
                    id: hero
                    objectName: "newProjectButton"
                    Layout.fillWidth: true
                    Layout.preferredHeight: Theme.editor.heroHeight
                    Accessible.role: Accessible.Button
                    Accessible.name: qsTr("New project")

                    Rectangle {
                        anchors.fill: parent
                        radius: Theme.shape.large
                        gradient: Gradient {
                            orientation: Gradient.Horizontal
                            GradientStop { position: 0; color: Theme.color.primaryContainer }
                            GradientStop { position: 1; color: Theme.color.tertiaryContainer }
                        }
                        border.width: drop.containsDrag ? Theme.editor.selectionBorder : 0
                        border.color: Theme.color.primary
                    }
                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: Theme.space.xl
                        spacing: Theme.space.xl
                        Rectangle {
                            Layout.preferredWidth: Theme.editor.heroHeight - 2 * Theme.space.xxl
                            Layout.preferredHeight: Layout.preferredWidth
                            radius: Theme.shape.extraLarge
                            color: Theme.color.primary
                            scale: heroMouse.pressed ? 0.96 : heroMouse.containsMouse ? 1.04 : 1
                            Behavior on scale { NumberAnimation { duration: Theme.motion.short4; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.motion.standard } }
                            Icon {
                                anchors.centerIn: parent
                                name: drop.containsDrag ? "file_download" : "add"
                                size: parent.width / 2
                                color: Theme.color.onPrimary
                            }
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: Theme.space.xs
                            Label {
                                role: "headlineMedium"
                                color: Theme.color.onPrimaryContainer
                                text: drop.containsDrag ? qsTr("Drop to start a project") : qsTr("New project")
                            }
                            Label {
                                Layout.fillWidth: true
                                role: "bodyLarge"
                                color: Theme.color.onPrimaryContainer
                                wrapMode: Text.WordWrap
                                text: qsTr("Add videos, photos and music, cut and export. Everything is saved automatically.")
                            }
                            Label {
                                Layout.fillWidth: true
                                role: "bodySmall"
                                color: Theme.color.onPrimaryContainer
                                opacity: 0.8
                                wrapMode: Text.WordWrap
                                text: qsTr("Tip: drop files here to start with them.")
                            }
                        }
                    }
                    StateLayer {
                        radius: Theme.shape.large
                        color: Theme.color.onPrimaryContainer
                        hovered: heroMouse.containsMouse
                        pressed: heroMouse.pressed
                        pressPoint: Qt.point(heroMouse.mouseX, heroMouse.mouseY)
                    }
                    MouseArea {
                        id: heroMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: App.newProject()
                    }
                }

                // Other ways to start, each a project the user can change freely afterwards (SPEC §5.13bis).
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.space.md
                    Repeater {
                        model: [{ id: "montageQuickButton", icon: "movie_edit", text: qsTr("Automatic montage"),
                                  detail: qsTr("Your shots and a song become a video on the beat") },
                                { id: "scriptQuickButton", icon: "article", text: qsTr("Script to video"),
                                  detail: qsTr("A text becomes scenes, captions and a voice") },
                                { id: "recordScreenQuickButton", icon: "screen_record", text: qsTr("Record screen"),
                                  detail: qsTr("Screen, webcam or both, with the teleprompter") },
                                { id: "slideshowQuickButton", icon: "slideshow", text: qsTr("Slideshow"),
                                  detail: qsTr("Photos and music become a video, on the beat") },
                                { id: "templatesQuickButton", icon: "dashboard_customize", text: qsTr("Templates"),
                                  detail: qsTr("Pick a ready-made video and put your own shots in it") }]
                        delegate: Rectangle {
                            id: tool
                            required property var modelData
                            objectName: modelData.id
                            Layout.fillWidth: true
                            Layout.preferredWidth: 1 // equal parts
                            implicitHeight: toolRow.implicitHeight + 2 * Theme.space.lg
                            radius: Theme.shape.large
                            color: Theme.color.panel
                            Accessible.role: Accessible.Button
                            Accessible.name: modelData.text
                            // Icon on top, then the name and what it does: five tools fit in a row with their names whole.
                            ColumnLayout {
                                id: toolRow
                                anchors.fill: parent
                                anchors.margins: Theme.space.lg
                                spacing: Theme.space.sm
                                Rectangle {
                                    implicitWidth: Theme.space.xxl + Theme.space.sm
                                    implicitHeight: implicitWidth
                                    radius: Theme.shape.medium
                                    color: Theme.color.secondaryContainer
                                    Icon {
                                        anchors.centerIn: parent
                                        name: tool.modelData.icon
                                        color: Theme.color.onSecondaryContainer
                                    }
                                }
                                Label {
                                    Layout.fillWidth: true
                                    role: "titleSmall"
                                    elide: Text.ElideRight
                                    text: tool.modelData.text
                                }
                                Label {
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    verticalAlignment: Text.AlignTop
                                    role: "bodySmall"
                                    color: Theme.color.onSurfaceVariant
                                    wrapMode: Text.WordWrap
                                    maximumLineCount: 2
                                    elide: Text.ElideRight
                                    text: tool.modelData.detail
                                }
                            }
                            StateLayer {
                                radius: parent.radius
                                color: Theme.color.onSurface
                                hovered: toolMouse.containsMouse
                                pressed: toolMouse.pressed
                                pressPoint: Qt.point(toolMouse.mouseX, toolMouse.mouseY)
                            }
                            MouseArea {
                                id: toolMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    if (tool.modelData.id === "montageQuickButton")
                                        montage.open()
                                    else if (tool.modelData.id === "scriptQuickButton")
                                        scriptVideo.open()
                                    else if (tool.modelData.id === "recordScreenQuickButton")
                                        App.recordScreen()
                                    else if (tool.modelData.id === "slideshowQuickButton")
                                        slideshow.open()
                                    else
                                        scroller.contentY = Math.min(templatesSection.y + content.y,
                                                                     scroller.contentHeight - scroller.height)
                                }
                            }
                        }
                    }
                }
            }

            // ---- Templates ------------------------------------------------------------------------------------------
            ColumnLayout {
                id: templatesSection
                Layout.fillWidth: true
                spacing: Theme.space.md

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.space.md
                    Label {
                        role: "titleLarge"
                        text: qsTr("Templates")
                    }
                    Label {
                        Layout.fillWidth: true
                        role: "bodyMedium"
                        color: Theme.color.onSurfaceVariant
                        elide: Text.ElideRight
                        text: qsTr("Choose one, then replace each shot with your videos and photos")
                    }
                }
                Flickable {
                    Layout.fillWidth: true
                    implicitHeight: chipRow.implicitHeight
                    contentWidth: chipRow.implicitWidth
                    clip: true
                    flickableDirection: Flickable.HorizontalFlick
                    boundsBehavior: Flickable.StopAtBounds
                    Row {
                        id: chipRow
                        spacing: Theme.space.xs
                        Chip {
                            text: qsTr("All")
                            checkable: false
                            checked: templates.category === ""
                            onClicked: templates.category = ""
                        }
                        Repeater {
                            model: templates.categories
                            delegate: Chip {
                                required property var modelData
                                text: modelData.name
                                checkable: false
                                checked: templates.category === modelData.id
                                onClicked: templates.category = modelData.id
                            }
                        }
                    }
                }
                ListView {
                    id: templateList
                    objectName: "templateList"
                    Layout.fillWidth: true
                    implicitHeight: Theme.editor.draftThumbnailHeight * 1.25 + Theme.space.xxxl + Theme.space.sm
                    orientation: ListView.Horizontal
                    spacing: Theme.space.md
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    model: AssetLibraryModel {
                        id: templates
                        kind: AssetLibraryModel.Templates
                    }
                    delegate: TemplateCard {
                        categoryName: {
                            for (const entry of templates.categories)
                                if (entry.id === category)
                                    return entry.name
                            return ""
                        }
                        onChosen: (templateId) => App.newProjectFromTemplate(templateId)
                    }
                    ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded }
                    WheelHandler {
                        onWheel: (event) => {
                            const delta = event.angleDelta.y !== 0 ? event.angleDelta.y : event.angleDelta.x
                            templateList.contentX = Math.max(0, Math.min(templateList.contentWidth - templateList.width,
                                                                         templateList.contentX - delta))
                        }
                    }
                }
            }

            // ---- Drafts ---------------------------------------------------------------------------------------------
            ColumnLayout {
                Layout.fillWidth: true
                spacing: Theme.space.md

                RowLayout {
                    Layout.fillWidth: true
                    Label {
                        Layout.fillWidth: true
                        role: "titleLarge"
                        text: qsTr("Drafts")
                    }
                    Label {
                        role: "labelLarge"
                        color: Theme.color.onSurfaceVariant
                        visible: App.drafts.count > 0
                        text: qsTr("%n project(s)", "", App.drafts.count)
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    visible: App.drafts.count === 0
                    implicitHeight: emptyDrafts.implicitHeight + 2 * Theme.space.xl
                    radius: Theme.shape.large
                    color: Theme.color.panel
                    ColumnLayout {
                        id: emptyDrafts
                        anchors.centerIn: parent
                        width: parent.width - 2 * Theme.space.xl
                        spacing: Theme.space.sm
                        Icon {
                            Layout.alignment: Qt.AlignHCenter
                            name: "video_library"
                            size: Theme.space.xxxl
                            color: Theme.color.outline
                        }
                        Label {
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignHCenter
                            role: "bodyLarge"
                            color: Theme.color.onSurfaceVariant
                            wrapMode: Text.WordWrap
                            text: qsTr("Your projects appear here, ready to continue exactly where you left them.")
                        }
                    }
                }

                Flow {
                    Layout.fillWidth: true
                    spacing: Theme.space.lg
                    Repeater {
                        model: App.drafts
                        delegate: DraftCard {
                            onOpenRequested: (draftId) => App.openDraft(draftId)
                            onRenameRequested: (draftId, name) => renameDialog.ask(draftId, name)
                        }
                    }
                }
            }
        }
    }

    // Files dropped anywhere on the home screen start a project with them (as in the editor's timeline).
    DropArea {
        id: drop
        anchors.fill: parent
        keys: ["text/uri-list"]
        onDropped: (event) => {
            if (event.hasUrls && App.newProjectWithFiles(event.urls))
                event.acceptProposedAction()
        }
    }

    SlideshowDialog {
        id: slideshow
        anchors.centerIn: parent
        width: Math.min(root.width - 2 * Theme.space.xl, Theme.editor.dialogWidth * 1.15)
    }
    ScriptVideoDialog {
        id: scriptVideo
        objectName: "scriptVideoDialog"
        anchors.centerIn: parent
        width: Math.min(root.width - 2 * Theme.space.xl, Theme.editor.dialogWidth * 1.15)
    }
    MontageDialog {
        id: montage
        objectName: "montageDialog"
        anchors.centerIn: parent
        width: Math.min(root.width - 2 * Theme.space.xl, Theme.editor.dialogWidth * 1.15)
    }

    Dialog {
        id: renameDialog
        property string draftId
        function ask(id, name) {
            draftId = id
            nameField.text = name
            open()
            nameField.selectAll()
            nameField.forceActiveFocus()
        }
        title: qsTr("Rename project")
        iconName: "edit"
        anchors.centerIn: parent
        width: Math.min(root.width - 2 * Theme.space.xl, Theme.editor.dialogWidth)
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: {
            const error = App.drafts.rename(draftId, nameField.text)
            if (error !== "")
                App.message(error)
        }
        TextField {
            id: nameField
            width: parent.width
            label: qsTr("Name")
            onAccepted: renameDialog.accept()
        }
    }
}
