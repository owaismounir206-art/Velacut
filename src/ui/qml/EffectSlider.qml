// A parameter of one of the video effects of the selected clip (ClipInspector.setEffectParam), as a SliderRow: a drag
// is one undo step, a double click on the name puts back the effect's own value.
pragma ComponentBehavior: Bound
import QtQuick
import Vedit.UI

SliderRow {
    id: root

    required property Inspector inspector
    required property int index
    required property string name
    property real neutral: 0

    objectName: "effect_" + index + "_" + name
    format: v => name === "angle" ? Math.round(v) + "°"
               : name === "speed" ? "×" + v.toLocaleString(Qt.locale(), "f", 1)
               : Math.round(v * 100) + " %"
    onMoved: (v) => root.inspector.setEffectParam(root.index, root.name, v)
    onCommitted: root.inspector.endGesture()
    onResetRequested: {
        root.inspector.setEffectParam(root.index, root.name, root.neutral)
        root.inspector.endGesture()
    }
}
