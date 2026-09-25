// Audio level of a track or of the whole mix (SPEC §5.9): a bar on a −60…0 dB scale, red when the sound clips.
// It reads the level while the video plays and until the peak has fallen back.
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

    implicitWidth: Theme.editor.meterWidth
    Accessible.role: Accessible.ProgressBar
    Accessible.name: qsTr("Audio level")

    Timer {
        interval: Theme.editor.meterInterval
        repeat: true
        running: meter.visible && (meter.player.playing || meter.level > 0.001)
        onTriggered: meter.level = meter.player.audioLevel(meter.key)
    }
    Rectangle {
        anchors.fill: parent
        radius: width / 2
        color: Theme.color.surfaceContainerHighest
    }
    Rectangle {
        objectName: "meterBar"
        anchors.bottom: parent.bottom
        width: parent.width
        height: parent.height * meter.fraction
        radius: width / 2
        color: meter.level >= 1 ? Theme.color.error : Theme.color.primary
    }
}
