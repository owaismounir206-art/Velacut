// A property of the selected clip as a slider with its name and value (SliderRow): a drag is one undo step, a double
// click on the name puts back the neutral value, the value can be typed in.
pragma ComponentBehavior: Bound
import QtQuick
import Velacut.Theme
import Velacut.UI

SliderRow {
    id: root

    required property Inspector inspector
    required property string key
    // The value that changes nothing (double click on the name).
    property real neutral: 0
    // The keyframe diamond of the parameter ("position", "scale", "rotation", "opacity"), "" = none.
    property string keyframeKey: ""

    objectName: "property_" + key
    trailingWidth: diamond.implicitWidth
    slider.objectName: "slider_" + key
    value: inspector.values[key] ?? neutral
    onMoved: (v) => root.inspector.set(root.key, v)
    onCommitted: root.inspector.endGesture()
    onResetRequested: {
        root.inspector.set(root.key, root.neutral)
        root.inspector.endGesture()
    }

    KeyframeButton {
        id: diamond
        visible: root.keyframeKey !== ""
        inspector: root.inspector
        key: root.keyframeKey
    }
}
