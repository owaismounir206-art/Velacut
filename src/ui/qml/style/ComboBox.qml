// Material 3 exposed dropdown menu, in the dense filled form of the editor's panels: a tonal field with the current
// choice and an arrow, the choices in an M3 menu. Models: arrays of strings, or of objects with `textRole`.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import Vedit.Components
import Vedit.Theme

T.ComboBox {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Theme.space.control(40)
    leftPadding: Theme.space.md
    rightPadding: Theme.space.md + Theme.editor.toolIconSize + Theme.space.xs
    font: Theme.type.labelLarge

    Accessible.role: Accessible.ComboBox

    delegate: T.ItemDelegate {
        id: item
        required property var model
        required property var modelData
        required property int index
        width: ListView.view.width
        implicitHeight: Theme.space.control(40)
        leftPadding: Theme.space.md
        rightPadding: Theme.space.md
        highlighted: control.highlightedIndex === index
        hoverEnabled: control.hoverEnabled
        text: control.textRole ? (Array.isArray(control.model) ? modelData[control.textRole] : model[control.textRole]) : modelData
        Accessible.role: Accessible.ListItem
        Accessible.name: text
        contentItem: Row {
            spacing: Theme.space.sm
            Icon {
                anchors.verticalCenter: parent.verticalCenter
                name: "check"
                size: Theme.editor.toolIconSize
                color: Theme.color.primary
                opacity: control.currentIndex === item.index ? 1 : 0
            }
            Label {
                anchors.verticalCenter: parent.verticalCenter
                width: item.availableWidth - Theme.editor.toolIconSize - Theme.space.sm
                role: "labelLarge"
                elide: Text.ElideRight
                color: control.currentIndex === item.index ? Theme.color.primary : Theme.color.onSurface
                text: item.text
            }
        }
        background: Item {
            StateLayer {
                color: Theme.color.onSurface
                hovered: item.highlighted || item.hovered
                pressed: item.pressed
                pressPoint: Qt.point(item.pressX, item.pressY)
            }
        }
    }

    indicator: Icon {
        x: control.width - width - Theme.space.sm
        y: (control.height - height) / 2
        name: "arrow_drop_down"
        size: Theme.editor.toolIconSize + Theme.space.xs
        color: control.enabled ? Theme.color.onSurfaceVariant : Theme.alpha(Theme.color.onSurface, Theme.state.disabledContent)
        rotation: control.popup.visible ? 180 : 0
        Behavior on rotation { NumberAnimation { duration: Theme.motion.short4 } }
    }

    contentItem: Label {
        role: "labelLarge"
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
        color: control.enabled ? Theme.color.onSurface : Theme.alpha(Theme.color.onSurface, Theme.state.disabledContent)
        text: control.displayText
    }

    background: Rectangle {
        implicitWidth: Theme.editor.valueWidth * 2
        implicitHeight: Theme.space.control(40)
        radius: Theme.shape.small
        color: control.enabled ? Theme.color.surfaceContainerHighest : Theme.alpha(Theme.color.onSurface, Theme.state.disabledContainer)
        border.width: control.visualFocus || control.popup.visible ? Theme.editor.selectionBorder : 0
        border.color: Theme.color.primary
        StateLayer {
            radius: parent.radius
            color: Theme.color.onSurface
            active: control.enabled
            hovered: control.hovered
            pressed: control.pressed
            pressPoint: Qt.point(control.width / 2, control.height / 2)
        }
    }

    popup: T.Popup {
        y: control.height + Theme.space.xxs
        width: Math.max(control.width, Theme.editor.libraryWidth / 2)
        implicitHeight: Math.min(contentItem.implicitHeight + topPadding + bottomPadding,
                                 (control.Window.window ? control.Window.window.height : Theme.editor.dialogWidth) / 2)
        topPadding: Theme.space.sm
        bottomPadding: Theme.space.sm
        enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.motion.short4 } }
        exit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Theme.motion.short2 } }

        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: control.delegateModel
            currentIndex: control.highlightedIndex
            highlightMoveDuration: 0
            boundsBehavior: Flickable.StopAtBounds
            T.ScrollIndicator.vertical: T.ScrollIndicator {}
            Component.onCompleted: positionViewAtIndex(control.currentIndex, ListView.Center)
        }

        background: Rectangle {
            radius: Theme.shape.extraSmall
            color: Theme.color.surfaceContainer
            Shadow { level: 2; radius: parent.radius }
        }
    }
}
