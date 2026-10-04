// Audio level of a track or of the whole mix (SPEC §5.9): lit segments on a −60…0 dB scale (it reads as a meter,
// not as a scroll bar), the loud ones (above −6 dB) in the tertiary colour, red when the sound clips. It reads the
// level while the video plays and until the peak has fallen back.
pragma ComponentBehavior: Bound
import QtQuick
import Vedit.Theme
import Vedit.UI

Item {
    id: meter

    required property TimelinePlayer player
    required property string key // a track id, or "master"
    property real level: 0 // linear peak, 1 = full scale

    readonly property real fraction: level <= 0.001 ? 0 : Math.max(0, Math.min(1, (20 * Math.log10(level) + 60) / 60))
    // One segment every ~3 dB, fewer on short meters.
    readonly property int segments: Math.max(4, Math.min(20, Math.floor(height / (Theme.editor.meterWidth * 1.5))))

    implicitWidth: Theme.editor.meterWidth
    Accessible.role: Accessible.ProgressBar
    Accessible.name: qsTr("Audio level")

    Timer {
        interval: Theme.editor.meterInterval
        repeat: true
        running: meter.visible && (meter.player.playing || meter.level > 0.001)
        onTriggered: meter.level = meter.player.audioLevel(meter.key)
    }
    Column {
        anchors.fill: parent
        spacing: Theme.editor.hairline
        Repeater {
            model: meter.segments
            delegate: Rectangle {
                required property int index
                // From the top: the last segment is full scale.
                readonly property real threshold: 1 - index / meter.segments
                width: meter.width
                height: (meter.height - (meter.segments - 1) * Theme.editor.hairline) / meter.segments
                radius: Theme.editor.hairline
                color: meter.fraction < threshold - 1 / meter.segments + 0.0001 ? Theme.color.surfaceContainerHighest
                     : index === 0 && meter.level >= 1 ? Theme.color.error
                     : threshold > 0.9 ? Theme.color.tertiary : Theme.color.primary
            }
        }
    }
    // The bar itself, for tests and screen readers: as tall as the level.
    Item {
        objectName: "meterBar"
        anchors.bottom: parent.bottom
        width: parent.width
        height: parent.height * meter.fraction
    }
}
