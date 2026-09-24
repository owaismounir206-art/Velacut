// Material 3 badge: small dot (count 0) or large badge with a number.
import QtQuick
import Vedit.Theme

Rectangle {
    property int count: 0
    readonly property bool small: count <= 0

    implicitWidth: small ? 6 : Math.max(16, label.implicitWidth + 8)
    implicitHeight: small ? 6 : 16
    radius: height / 2
    color: Theme.color.error

    TypeText {
        id: label
        anchors.centerIn: parent
        visible: !parent.small
        text: parent.count > 999 ? "999+" : parent.count
        role: "labelSmall"
        color: Theme.color.onError
    }
}
