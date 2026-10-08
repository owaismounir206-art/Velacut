// A project template on the home screen (SPEC §5.13): a drawn preview in the template's format (its slots as a strip
// of shots, its title in the middle), its name, length and number of shots. A click opens a new project made from it,
// with a placeholder for each shot.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Velacut.Components
import Velacut.Theme

Item {
    id: card

    required property string assetId
    required property string name
    required property string category
    required property var details
    required property int index
    property string categoryName: ""
    signal chosen(string templateId)

    readonly property var aspect: {
        const parts = (details.canvas ?? "16:9").split(":")
        return parts.length === 2 ? Number(parts[0]) / Number(parts[1]) : 16 / 9
    }
    // The three tonal families of the scheme, in turn: every card a different colour, all from the dynamic theme. The
    // "fixed" roles keep their brightness in the light and the dark scheme, like the picture of a video.
    readonly property color tone: [Theme.color.primaryFixed, Theme.color.tertiaryFixed, Theme.color.secondaryFixed][index % 3]
    readonly property color toneEnd: [Theme.color.primaryFixedDim, Theme.color.tertiaryFixedDim,
                                      Theme.color.secondaryFixedDim][index % 3]
    readonly property color onTone: [Theme.color.onPrimaryFixed, Theme.color.onTertiaryFixed,
                                     Theme.color.onSecondaryFixed][index % 3]

    implicitWidth: Theme.editor.draftThumbnailHeight * 1.25
    implicitHeight: preview.height + texts.implicitHeight + Theme.space.sm
    objectName: "template_" + assetId
    Accessible.role: Accessible.Button
    Accessible.name: qsTr("New project from the template %1").arg(name)

    Rectangle {
        id: preview
        width: parent.width
        height: Theme.editor.draftThumbnailHeight * 1.25
        radius: Theme.shape.medium
        color: Theme.color.surfaceContainerHigh
        clip: true

        // The video's frame, in its own format.
        Rectangle {
            id: frame
            anchors.centerIn: parent
            readonly property real room: parent.height - 2 * Theme.space.md
            height: card.aspect >= 1 ? Math.min(room, (parent.width - 2 * Theme.space.md) / card.aspect) : room
            width: height * card.aspect
            radius: Theme.shape.small
            gradient: Gradient {
                GradientStop { position: 0; color: card.tone }
                GradientStop { position: 1; color: card.toneEnd }
            }
            Label {
                anchors.centerIn: parent
                width: parent.width - 2 * Theme.space.sm
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                maximumLineCount: 3
                elide: Text.ElideRight
                role: card.aspect < 1 ? "labelMedium" : "labelLarge"
                color: card.onTone
                text: card.name
            }
            // The shots, in proportion to their length.
            Row {
                anchors { left: parent.left; right: parent.right; bottom: parent.bottom; margins: Theme.space.xs }
                height: Theme.space.sm
                spacing: Theme.editor.hairline * 2
                Repeater {
                    model: card.details.slots ?? []
                    delegate: Rectangle {
                        required property var modelData
                        width: (parent.width - ((card.details.slots ?? []).length - 1) * parent.spacing) * modelData
                               / Math.max(0.01, card.details.seconds ?? 1)
                        height: parent.height
                        radius: Theme.shape.extraSmall
                        color: Theme.alpha(card.onTone, 0.35)
                    }
                }
            }
        }
        Rectangle {
            anchors { right: parent.right; top: parent.top; margins: Theme.space.sm }
            width: formatLabel.implicitWidth + 2 * Theme.space.sm
            height: formatLabel.implicitHeight + Theme.space.xxs
            radius: Theme.shape.extraSmall
            color: Theme.color.inverseSurface
            Label {
                id: formatLabel
                anchors.centerIn: parent
                role: "labelSmall"
                color: Theme.color.inverseOnSurface
                text: card.details.canvas ?? ""
            }
        }
        StateLayer {
            radius: parent.radius
            color: Theme.color.onSurface
            hovered: mouse.containsMouse
            pressed: mouse.pressed
            pressPoint: Qt.point(mouse.mouseX, mouse.mouseY)
        }
        Rectangle {
            anchors.fill: parent
            radius: parent.radius
            color: "transparent"
            border.width: mouse.containsMouse ? Theme.editor.selectionBorder : 0
            border.color: Theme.color.primary
        }
    }

    ColumnLayout {
        id: texts
        anchors { left: parent.left; right: parent.right; top: preview.bottom; topMargin: Theme.space.sm }
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
            text: qsTr("%1 · %n shot(s) · %2 s", "", (card.details.slots ?? []).length)
                    .arg(card.categoryName).arg(Math.round(card.details.seconds ?? 0))
        }
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: card.chosen(card.assetId)
    }
}
