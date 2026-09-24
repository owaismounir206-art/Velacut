import QtQuick
import QtQuick.Templates as T
import Vedit.Components
import Vedit.Theme

T.MenuSeparator {
    implicitWidth: 112
    implicitHeight: 17
    topPadding: 8
    bottomPadding: 8
    contentItem: Rectangle {
        implicitHeight: 1
        color: Theme.color.outlineVariant
    }
}
