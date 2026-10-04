// Material 3 dialog actions: text buttons at the bottom right (the confirming one last).
import QtQuick
import QtQuick.Templates as T
import Vedit.Theme

T.DialogButtonBox {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            (control.count === 1 ? implicitContentWidth * 2 : implicitContentWidth) + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset, implicitContentHeight + topPadding + bottomPadding)
    spacing: Theme.space.sm
    padding: Theme.space.xl
    topPadding: Theme.space.md
    alignment: Qt.AlignRight
    // M3: the dismissive action first, the confirming one last (rightmost), whatever the platform's habit.
    buttonLayout: T.DialogButtonBox.GnomeLayout

    delegate: Button {
        variant: "text"
    }

    contentItem: ListView {
        implicitWidth: contentWidth
        model: control.contentModel
        spacing: control.spacing
        orientation: ListView.Horizontal
        boundsBehavior: Flickable.StopAtBounds
        snapMode: ListView.SnapToItem
    }

    background: Item {}
}
