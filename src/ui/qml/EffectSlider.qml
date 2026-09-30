// A parameter of one of the video effects of the selected clip (ClipInspector.setEffectParam): a drag is one undo
// step, a double click on the name puts back the effect's own value.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Vedit.Theme
import Vedit.UI

ColumnLayout {
    id: root

    required property Inspector inspector
    required property int index
    required property string name
    property string label
    property real value
    property real from: 0
    property real to: 1
    property real neutral: 0

    function format(v) { return name === "angle" ? Math.round(v) + "°"
                              : name === "speed" ? "×" + v.toLocaleString(Qt.locale(), "f", 1)
                              : Math.round(v * 100) + " %" }

    Layout.fillWidth: true
    spacing: 0
    objectName: "effect_" + index + "_" + name

    RowLayout {
        Layout.fillWidth: true
        Label {
            Layout.fillWidth: true
            text: root.label
            role: "bodyMedium"
            elide: Text.ElideRight
            TapHandler {
                onDoubleTapped: {
                    root.inspector.setEffectParam(root.index, root.name, root.neutral)
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
        from: root.from
        to: root.to
        valueText: root.format(value)
        Accessible.name: root.label
        onMoved: root.inspector.setEffectParam(root.index, root.name, value)
        onPressedChanged: if (!pressed) root.inspector.endGesture()
        onActiveFocusChanged: if (!activeFocus) root.inspector.endGesture()
        Binding on value {
            value: root.value
            when: !slider.pressed
        }
    }
}
