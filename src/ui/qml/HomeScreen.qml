// Home screen: a big "New project" (no questions: the editor opens at once) and the drafts, which save themselves.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Vedit.Components
import Vedit.Theme
import Vedit.UI

Item {
    id: root

    signal infoRequested()

    Component.onCompleted: App.drafts.refresh()

    Shortcut { sequences: [StandardKey.New]; onActivated: App.newProject() }

    TopAppBar {
        id: bar
        anchors { left: parent.left; right: parent.right; top: parent.top }
        title: "vedit"
        scrolled: scroller.contentY > 0
        trailing: [
            IconButton {
                iconName: "info"
                label: qsTr("System information")
                onClicked: root.infoRequested()
            }
        ]
    }

    Flickable {
        id: scroller
        anchors { left: parent.left; right: parent.right; top: bar.bottom; bottom: parent.bottom }
        contentWidth: width
        contentHeight: content.implicitHeight + 2 * Theme.space.xl
        clip: true
        ScrollBar.vertical: ScrollBar {}

        ColumnLayout {
            id: content
            width: Math.min(scroller.width - 2 * Theme.space.xl, Theme.editor.contentMaxWidth)
            x: (scroller.width - width) / 2
            y: Theme.space.xl
            spacing: Theme.space.xl

            // "New project": the most important action, the largest target.
            Card {
                id: hero
                objectName: "newProjectButton"
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.editor.heroHeight
                variant: "filled"
                interactive: true
                Accessible.name: qsTr("New project")
                onClicked: App.newProject()

                Rectangle {
                    anchors.fill: parent
                    radius: Theme.shape.medium
                    color: Theme.color.primaryContainer
                }
                RowLayout {
                    anchors.fill: parent
                    anchors.margins: Theme.space.xl
                    spacing: Theme.space.xl
                    Rectangle {
                        Layout.preferredWidth: Theme.editor.heroHeight - 2 * Theme.space.xl
                        Layout.preferredHeight: Layout.preferredWidth
                        radius: Theme.shape.extraLarge
                        color: Theme.color.primary
                        Icon {
                            anchors.centerIn: parent
                            name: "add"
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
                            text: qsTr("New project")
                        }
                        Label {
                            Layout.fillWidth: true
                            role: "bodyLarge"
                            color: Theme.color.onPrimaryContainer
                            wrapMode: Text.WordWrap
                            text: qsTr("Add videos, photos and music, cut and export. Everything is saved automatically.")
                        }
                    }
                }
            }

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

            Label {
                Layout.fillWidth: true
                visible: App.drafts.count === 0
                role: "bodyLarge"
                color: Theme.color.onSurfaceVariant
                wrapMode: Text.WordWrap
                text: qsTr("Your projects appear here, ready to continue exactly where you left them.")
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
