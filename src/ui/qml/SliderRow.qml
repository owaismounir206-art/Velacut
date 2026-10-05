// A value as one row, as video editors lay out their properties: the name, the slider and a box with the value that
// can also be typed in (Enter applies it, Esc puts back the value). On a narrow panel the slider goes under the name.
// A double click on the name puts back the neutral value. Items given as children (a keyframe diamond) go at the end.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Vedit.Theme

GridLayout {
    id: root

    property string label
    property real value
    property real from: 0
    property real to: 1
    property real stepSize: 0
    // Equal steps multiply the value (speed): the slider works on the logarithm.
    property bool logarithmic: false
    // The value as shown to the user.
    property var format: function (v) { return (Math.round(v * 100) / 100).toLocaleString(Qt.locale()) }
    property alias slider: slider
    default property alias trailing: trailingRow.data
    // Room kept at the end for the trailing items even when they are hidden, so that the sliders of a page line up.
    property real trailingWidth: 0
    readonly property bool inline: width >= Theme.editor.inlinePropertyWidth

    // While dragging, with the value in the model's units.
    signal moved(real value)
    // A gesture is over (slider released, value typed): one undo step.
    signal committed()
    signal resetRequested()

    function toSlider(v) { return logarithmic ? Math.log(Math.max(v, from)) : v }
    function fromSlider(v) { return logarithmic ? Math.exp(v) : v }

    // The number in a text as people type it ("1,5", "1.5", "-3 dB", "1.920" with Italian thousands).
    function numberIn(text) {
        let t = String(text).replace(/[^0-9.,+\-]/g, "")
        const comma = t.lastIndexOf(","), dot = t.lastIndexOf(".")
        if (comma >= 0 && dot >= 0) {
            const decimal = comma > dot ? "," : "."
            t = t.split(decimal === "," ? "." : ",").join("").replace(decimal, ".")
        } else if (comma >= 0) {
            t = t.replace(",", ".")
        } else if (dot >= 0 && Qt.locale().groupSeparator === "." && /^[+\-]?\d{1,3}(\.\d{3})+$/.test(t)) {
            t = t.split(".").join("")
        }
        const n = parseFloat(t)
        return isNaN(n) ? null : n
    }
    // The value whose text shows `wanted`: the format is monotonic, so a bisection of the slider's range finds it.
    function valueFor(wanted) {
        let low = slider.from, high = slider.to
        const rising = numberIn(format(fromSlider(high))) >= numberIn(format(fromSlider(low)))
        for (let i = 0; i < 48; ++i) {
            const middle = (low + high) / 2
            const shown = numberIn(format(fromSlider(middle)))
            if ((shown < wanted) === rising)
                low = middle
            else
                high = middle
        }
        let v = fromSlider((low + high) / 2)
        if (stepSize > 0 && !logarithmic)
            v = Math.round(v / stepSize) * stepSize
        return Math.min(Math.max(v, from), to)
    }

    Layout.fillWidth: true
    columns: inline ? 4 : 3
    columnSpacing: Theme.space.sm
    rowSpacing: 0

    Label {
        id: name
        Layout.row: 0
        Layout.column: 0
        Layout.fillWidth: !root.inline
        Layout.preferredWidth: root.inline ? Theme.editor.propertyLabelWidth : -1
        Layout.maximumWidth: root.inline ? Theme.editor.propertyLabelWidth : Number.POSITIVE_INFINITY
        text: root.label
        role: "bodyMedium"
        color: Theme.color.onSurfaceVariant
        elide: Text.ElideRight
        HoverHandler { id: nameHover }
        ToolTip.visible: name.truncated && nameHover.hovered
        ToolTip.text: root.label
        TapHandler { onDoubleTapped: root.resetRequested() }
    }
    Slider {
        id: slider
        Layout.row: root.inline ? 0 : 1
        Layout.column: root.inline ? 1 : 0
        Layout.columnSpan: root.inline ? 1 : 3
        Layout.fillWidth: true
        from: root.toSlider(root.from)
        to: root.toSlider(root.to)
        stepSize: root.logarithmic ? 0 : root.stepSize
        valueText: root.format(root.fromSlider(value))
        Accessible.name: root.label
        onMoved: root.moved(root.fromSlider(value))
        onPressedChanged: if (!pressed) root.committed()
        onActiveFocusChanged: if (!activeFocus) root.committed()
        Binding on value {
            value: root.toSlider(root.value)
            when: !slider.pressed
        }
    }
    // The value, typed in.
    Rectangle {
        id: box
        Layout.row: 0
        Layout.column: root.inline ? 2 : 1
        Layout.preferredWidth: Theme.editor.valueWidth
        Layout.preferredHeight: Theme.editor.valueFieldHeight
        radius: Theme.shape.extraSmall
        color: field.activeFocus ? Theme.color.surfaceContainerHighest
             : boxHover.hovered ? Theme.color.surfaceContainerHigh : Theme.alpha(Theme.color.surfaceContainerHighest, 0.6)
        border.width: field.activeFocus ? Theme.editor.hairline : 0
        border.color: Theme.color.primary
        HoverHandler { id: boxHover; cursorShape: Qt.IBeamCursor }
        TextInput {
            id: field
            objectName: "value_" + root.objectName
            anchors.fill: parent
            anchors.leftMargin: Theme.space.xs
            anchors.rightMargin: Theme.space.xs
            verticalAlignment: TextInput.AlignVCenter
            horizontalAlignment: TextInput.AlignRight
            clip: true
            selectByMouse: true
            font: Theme.type.labelLarge
            color: Theme.color.onSurface
            selectionColor: Theme.alpha(Theme.color.primary, 0.4)
            selectedTextColor: Theme.color.onSurface
            Accessible.name: root.label
            Binding on text {
                value: root.format(slider.pressed ? root.fromSlider(slider.value) : root.value)
                when: !field.activeFocus
                restoreMode: Binding.RestoreNone
            }
            onActiveFocusChanged: if (activeFocus) selectAll()
            Keys.onReturnPressed: field.apply()
            Keys.onEnterPressed: field.apply()
            Keys.onEscapePressed: {
                text = root.format(root.value)
                focus = false
            }
            function apply() {
                const wanted = root.numberIn(text)
                if (wanted !== null) {
                    root.moved(root.valueFor(wanted))
                    root.committed()
                }
                focus = false
                text = root.format(root.value)
            }
        }
    }
    Row {
        id: trailingRow
        Layout.row: 0
        Layout.column: root.inline ? 3 : 2
        Layout.alignment: Qt.AlignVCenter
        Layout.preferredWidth: Math.max(implicitWidth, root.trailingWidth)
    }
}
