// One draft on the home screen: thumbnail, name, duration and date; its menu renames, duplicates, deletes.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Velacut.Components
import Velacut.Theme
import Velacut.UI

Card {
    id: card

    required property string draftId
    required property string name
    required property string durationText
    required property string modifiedText
    required property string thumbnail
    required property bool openElsewhere

    signal openRequested(string draftId)
    signal renameRequested(string draftId, string name)

    width: Theme.editor.draftCardWidth
    height: Theme.editor.draftThumbnailHeight + details.implicitHeight + 2 * Theme.space.md
    variant: "filled"
    containerColor: Theme.color.panel
    interactive: !openElsewhere
    Accessible.name: qsTr("Open %1").arg(name)
    onClicked: openRequested(draftId)

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.editor.draftThumbnailHeight
            radius: Theme.shape.medium
            color: Theme.color.surfaceContainerHighest
            clip: true

            Image {
                anchors.fill: parent
                source: card.thumbnail
                fillMode: Image.PreserveAspectCrop
                asynchronous: true
                cache: false // rewritten when the project changes
                visible: status === Image.Ready
            }
            Icon {
                anchors.centerIn: parent
                visible: card.thumbnail === ""
                name: "movie"
                size: Theme.space.xxxl
                color: Theme.color.onSurfaceVariant
            }
            Rectangle {
                anchors { right: parent.right; bottom: parent.bottom; margins: Theme.space.sm }
                width: duration.implicitWidth + 2 * Theme.space.sm
                height: duration.implicitHeight + Theme.space.xs
                radius: Theme.shape.extraSmall
                color: Theme.color.inverseSurface
                Label {
                    id: duration
                    anchors.centerIn: parent
                    role: "labelMedium"
                    color: Theme.color.inverseOnSurface
                    font.features: { "tnum": 1 }
                    text: card.durationText
                }
            }
            IconButton {
                objectName: "draftMenuButton"
                anchors { right: parent.right; top: parent.top; margins: Theme.space.xs }
                visible: card.hovered || menu.opened
                variant: "tonal"
                iconName: "more_vert"
                label: qsTr("Project options")
                onClicked: menu.popup()
            }
        }

        ColumnLayout {
            id: details
            Layout.fillWidth: true
            Layout.margins: Theme.space.md
            spacing: Theme.space.xxs
            Label {
                Layout.fillWidth: true
                role: "titleSmall"
                elide: Text.ElideRight
                text: card.name
            }
            Label {
                Layout.fillWidth: true
                role: "bodySmall"
                color: Theme.color.onSurfaceVariant
                elide: Text.ElideRight
                text: card.openElsewhere ? qsTr("Open in another window") : card.modifiedText
            }
        }
    }

    Menu {
        id: menu
        MenuItem {
            iconName: "edit"
            text: qsTr("Rename")
            onTriggered: card.renameRequested(card.draftId, card.name)
        }
        MenuItem {
            iconName: "content_copy"
            text: qsTr("Duplicate")
            onTriggered: {
                const error = App.drafts.duplicate(card.draftId)
                if (error !== "")
                    App.message(error)
            }
        }
        MenuSeparator {}
        MenuItem {
            iconName: "delete"
            text: qsTr("Move to trash")
            onTriggered: {
                const name = card.name
                const error = App.drafts.remove(card.draftId)
                App.message(error !== "" ? error : qsTr("“%1” moved to the trash").arg(name))
            }
        }
    }
}
