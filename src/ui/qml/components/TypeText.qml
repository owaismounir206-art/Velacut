// Text with a Material 3 type role (inside components; application code uses Label from QtQuick.Controls).
import QtQuick
import Velacut.Theme

Text {
    property string role: "bodyMedium"

    font: Theme.type[role]
    lineHeight: Theme.type[role + "LineHeight"]
    lineHeightMode: Text.FixedHeight
    color: Theme.color.onSurface
    verticalAlignment: Text.AlignVCenter
}
