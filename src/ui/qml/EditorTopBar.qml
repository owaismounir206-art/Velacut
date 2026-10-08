// Editor top bar (SPEC §4): the menu and the way back to the drafts on the left, the project name in the middle (click
// to rename) with "Saved" next to it, search and a prominent Export on the right. Undo/redo live on the timeline
// toolbar, the format under the player.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Velacut.Components
import Velacut.Theme
import Velacut.UI

Item {
    id: bar

    required property Editor editor
    signal backRequested()
    signal exportRequested()
    signal searchRequested()
    signal importRequested()
    signal frameRequested()
    signal infoRequested()
    signal shortcutsRequested()
    signal preferencesRequested()

    implicitHeight: Theme.editor.topBarHeight

    RowLayout {
        id: leading
        anchors { left: parent.left; verticalCenter: parent.verticalCenter }
        spacing: Theme.space.xxs

        IconButton {
            objectName: "backButton"
            implicitWidth: Theme.editor.toolButtonSize + Theme.space.xs
            implicitHeight: implicitWidth
            iconName: "arrow_back"
            label: qsTr("Back to projects")
            onClicked: bar.backRequested()
        }
        // The menu: what is less frequent than the buttons around it, for those who look for it (SPEC 0bis rule 2).
        Button {
            objectName: "mainMenuButton"
            variant: "text"
            iconName: "menu"
            text: qsTr("Menu")
            onClicked: menu.open()

            Menu {
                id: menu
                y: parent.height
                MenuItem {
                    iconName: "add"
                    text: qsTr("New project")
                    shortcutText: qsTr("Ctrl+N")
                    onTriggered: App.newProject()
                }
                MenuItem {
                    iconName: "upload_file"
                    text: qsTr("Import media…")
                    shortcutText: qsTr("Ctrl+I")
                    onTriggered: bar.importRequested()
                }
                MenuItem {
                    iconName: "file_upload"
                    text: qsTr("Export…")
                    shortcutText: qsTr("Ctrl+E")
                    enabled: bar.editor.timeline.duration > 0
                    onTriggered: bar.exportRequested()
                }
                MenuItem {
                    iconName: "photo_camera"
                    text: qsTr("Save the current frame")
                    shortcutText: qsTr("Ctrl+Shift+E")
                    enabled: bar.editor.timeline.duration > 0
                    onTriggered: bar.frameRequested()
                }
                MenuSeparator {}
                MenuItem {
                    iconName: "search"
                    text: qsTr("Search…")
                    shortcutText: qsTr("Ctrl+K")
                    onTriggered: bar.searchRequested()
                }
                MenuItem {
                    iconName: "settings"
                    text: qsTr("Preferences…")
                    shortcutText: qsTr("Ctrl+,")
                    onTriggered: bar.preferencesRequested()
                }
                MenuItem {
                    iconName: "keyboard"
                    text: qsTr("Keyboard shortcuts")
                    onTriggered: bar.shortcutsRequested()
                }
                MenuItem {
                    iconName: "info"
                    text: qsTr("System information")
                    onTriggered: bar.infoRequested()
                }
                MenuSeparator {}
                MenuItem {
                    iconName: "home"
                    text: qsTr("Back to projects")
                    onTriggered: bar.backRequested()
                }
            }
        }
    }

    // The name is edited in place: click, type, Enter. "Saved" is always visible: there is no Save (SPEC 0bis rule 2).
    RowLayout {
        anchors.centerIn: parent
        width: Math.min(implicitWidth, bar.width - 2 * Math.max(leading.width, trailing.width) - 2 * Theme.space.lg)
        spacing: Theme.space.sm

        TextInput {
            id: nameInput
            Layout.fillWidth: true
            Layout.preferredWidth: contentWidth + Theme.space.xs
            Layout.maximumWidth: contentWidth + Theme.space.xs
            text: bar.editor.name
            font: Theme.type.titleSmall
            color: Theme.color.onSurface
            horizontalAlignment: TextInput.AlignHCenter
            selectByMouse: true
            clip: true
            Accessible.name: qsTr("Project name")
            onEditingFinished: {
                bar.editor.name = text
                text = Qt.binding(() => bar.editor.name)
                focus = false
            }
            Keys.onEscapePressed: { text = bar.editor.name; focus = false }
            Rectangle {
                anchors { left: parent.left; right: parent.right; top: parent.bottom }
                height: Theme.editor.hairline
                color: nameInput.activeFocus ? Theme.color.primary
                     : nameHover.hovered ? Theme.color.outline : "transparent"
            }
            HoverHandler { id: nameHover; cursorShape: Qt.IBeamCursor }
            ToolTip.visible: nameHover.hovered && !activeFocus
            ToolTip.delay: 600
            ToolTip.text: qsTr("Click to rename the project")
        }
        Icon {
            name: bar.editor.saveState === Editor.SaveFailed ? "cloud_off"
                : bar.editor.saveState === Editor.Saving ? "cloud_sync" : "cloud_done"
            size: Theme.editor.smallIconSize
            color: bar.editor.saveState === Editor.SaveFailed ? Theme.color.error : Theme.color.onSurfaceVariant
        }
        Label {
            role: "labelMedium"
            color: bar.editor.saveState === Editor.SaveFailed ? Theme.color.error : Theme.color.onSurfaceVariant
            text: bar.editor.saveState === Editor.SaveFailed ? qsTr("Not saved: retrying")
                : bar.editor.saveState === Editor.Saving ? qsTr("Saving…") : qsTr("Saved")
            ToolTip.visible: saveHover.hovered && bar.editor.saveError !== ""
            ToolTip.text: bar.editor.saveError
            HoverHandler { id: saveHover }
        }
    }

    RowLayout {
        id: trailing
        anchors { right: parent.right; verticalCenter: parent.verticalCenter }
        spacing: Theme.space.sm

        // Universal search (Ctrl+K), shown as a search field: the place where everything can be found (SPEC 0bis 14).
        Rectangle {
            objectName: "searchButton"
            implicitWidth: Theme.editor.libraryWidth / 2
            implicitHeight: Theme.editor.toolButtonSize
            radius: Theme.shape.full
            color: Theme.color.panel
            Accessible.role: Accessible.Button
            Accessible.name: qsTr("Search commands, filters, transitions…")
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.space.md
                anchors.rightMargin: Theme.space.md
                spacing: Theme.space.sm
                Icon {
                    name: "search"
                    size: Theme.editor.toolIconSize
                    color: Theme.color.onSurfaceVariant
                }
                Label {
                    Layout.fillWidth: true
                    role: "labelLarge"
                    elide: Text.ElideRight
                    color: Theme.color.onSurfaceVariant
                    text: qsTr("Search")
                }
                Label {
                    role: "labelSmall"
                    color: Theme.color.onSurfaceVariant
                    text: qsTr("Ctrl+K")
                }
            }
            StateLayer {
                radius: parent.radius
                color: Theme.color.onSurface
                hovered: searchMouse.containsMouse
                pressed: searchMouse.pressed
                pressPoint: Qt.point(searchMouse.mouseX, searchMouse.mouseY)
            }
            MouseArea {
                id: searchMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: bar.searchRequested()
            }
        }

        // Export: visible while it runs, a click opens the details.
        Button {
            objectName: "exportButton"
            variant: "filled"
            iconName: bar.editor.exportJob.running ? "hourglass_top" : "file_upload"
            text: bar.editor.exportJob.running ? qsTr("Exporting %1%").arg(Math.floor(bar.editor.exportJob.progress * 100))
                                                : qsTr("Export")
            enabled: bar.editor.timeline.duration > 0 || bar.editor.exportJob.running
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Save the video (Ctrl+E)")
            onClicked: bar.exportRequested()
        }
    }
}
