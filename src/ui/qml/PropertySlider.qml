// A property of the selected clip as a slider with its name and value: a drag is one undo step, a double click on
// the name puts back the neutral value.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Vedit.Theme
import Vedit.UI

ColumnLayout {
    id: root

    required property Inspector inspector
    required property string key
    property string label
    property real from: 0
    property real to: 1
    property real stepSize: 0
    // The value that changes nothing (double click on the name).
    property real neutral: 0
    // Equal steps multiply the value (speed): the slider works on the logarithm.
    property bool logarithmic: false
    // The value as shown to the user.
    property var format: function (v) { return (Math.round(v * 100) / 100).toLocaleString(Qt.locale()) }

    readonly property real value: inspector.values[key] ?? neutral

    function toSlider(v) { return logarithmic ? Math.log(Math.max(v, from)) : v }
    function fromSlider(v) { return logarithmic ? Math.exp(v) : v }

    Layout.fillWidth: true
    spacing: 0
    objectName: "property_" + key

    RowLayout {
        Layout.fillWidth: true
        Label {
            Layout.fillWidth: true
            text: root.label
            role: "bodyMedium"
            elide: Text.ElideRight
            TapHandler {
                onDoubleTapped: {
                    root.inspector.set(root.key, root.neutral)
                    root.inspector.endGesture()
                }
            }
        }
        Label {
            Layout.preferredWidth: Theme.editor.valueWidth
            horizontalAlignment: Text.AlignRight
            text: root.format(root.value)
            role: "labelLarge"
            font.features: { "tnum": 1 }
            color: Theme.color.onSurfaceVariant
        }
    }
    Slider {
        id: slider
        Layout.fillWidth: true
        from: root.toSlider(root.from)
        to: root.toSlider(root.to)
        stepSize: root.logarithmic ? 0 : root.stepSize
        valueText: root.format(root.fromSlider(value))
        Accessible.name: root.label
        onMoved: root.inspector.set(root.key, root.fromSlider(value))
        onPressedChanged: if (!pressed) root.inspector.endGesture()
        onActiveFocusChanged: if (!activeFocus) root.inspector.endGesture()
        Binding on value {
            value: root.toSlider(root.value)
            when: !slider.pressed
        }
    }
}
