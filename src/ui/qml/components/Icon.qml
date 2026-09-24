// Material Symbols Rounded (Apache-2.0) icon by ligature name, e.g. Icon { name: "play_arrow" }.
// `filled` animates the FILL axis (used for the active state).
import QtQuick
import Vedit.Theme

Text {
    id: root
    property string name
    property real size: 24
    property bool filled: false
    property int weight: 400

    text: Theme.icon(name)
    color: Theme.color.onSurfaceVariant
    font.family: Theme.iconFontFamily
    font.pixelSize: size
    font.variableAxes: { "FILL": filled ? 1 : 0, "wght": weight, "GRAD": 0, "opsz": Math.max(20, Math.min(48, size)) }
    horizontalAlignment: Text.AlignHCenter
    verticalAlignment: Text.AlignVCenter
    width: size
    height: size
    Accessible.ignored: true
}
