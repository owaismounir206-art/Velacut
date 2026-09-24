// Text with a Material 3 type role: Label { role: "titleMedium"; text: qsTr("…") }.
import QtQuick
import QtQuick.Templates as T
import Vedit.Components
import Vedit.Theme

T.Label {
    property string role: "bodyMedium"

    font: Theme.type[role]
    lineHeight: Theme.type[role + "LineHeight"]
    lineHeightMode: Text.FixedHeight
    color: Theme.color.onSurface
    verticalAlignment: Text.AlignVCenter
    linkColor: Theme.color.primary
}
