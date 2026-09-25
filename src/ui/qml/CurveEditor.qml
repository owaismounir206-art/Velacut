// The curve of the movement from the keyframe at the playhead to the next one (SPEC §5.6): time to the right, progress
// upwards (a little room above and below for overshoot). Drag the two points; one undo step per drag.
pragma ComponentBehavior: Bound
import QtQuick
import Vedit.Theme
import Vedit.UI

Item {
    id: editor

    required property Inspector inspector
    readonly property var curve: inspector.values["kf.curve"] ?? [0, 0, 1, 1]
    readonly property real pad: Theme.editor.handleSize

    function toX(value) { return pad + value * (width - 2 * pad) }
    function toY(value) { return pad + (1.25 - value) / 1.5 * (height - 2 * pad) }
    function fromX(x) { return Math.max(0, Math.min(1, (x - pad) / (width - 2 * pad))) }
    function fromY(y) { return Math.max(-0.25, Math.min(1.25, 1.25 - (y - pad) / (height - 2 * pad) * 1.5)) }

    objectName: "curveEditor"
    implicitHeight: Theme.editor.assetTileWidth * 1.5
    onCurveChanged: plot.requestPaint()
    onWidthChanged: plot.requestPaint()
    Accessible.role: Accessible.Graphic
    Accessible.name: qsTr("Movement curve")

    Rectangle {
        anchors.fill: parent
        radius: Theme.shape.small
        color: Theme.color.surfaceContainerHighest
    }
    Canvas {
        id: plot
        anchors.fill: parent
        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            const c = editor.curve
            // Levers from the ends to the control points, then the curve.
            ctx.strokeStyle = Theme.color.outline
            ctx.lineWidth = Theme.editor.hairline
            ctx.beginPath()
            ctx.moveTo(editor.toX(0), editor.toY(0))
            ctx.lineTo(editor.toX(c[0]), editor.toY(c[1]))
            ctx.moveTo(editor.toX(1), editor.toY(1))
            ctx.lineTo(editor.toX(c[2]), editor.toY(c[3]))
            ctx.stroke()
            ctx.strokeStyle = Theme.color.primary
            ctx.lineWidth = Theme.editor.selectionBorder
            ctx.beginPath()
            ctx.moveTo(editor.toX(0), editor.toY(0))
            ctx.bezierCurveTo(editor.toX(c[0]), editor.toY(c[1]), editor.toX(c[2]), editor.toY(c[3]),
                              editor.toX(1), editor.toY(1))
            ctx.stroke()
        }
    }
    Repeater {
        model: 2
        delegate: Rectangle {
            id: point
            required property int index
            objectName: "curvePoint" + index
            width: Theme.editor.handleSize
            height: Theme.editor.handleSize
            radius: width / 2
            x: editor.toX(editor.curve[2 * index]) - width / 2
            y: editor.toY(editor.curve[2 * index + 1]) - height / 2
            color: Theme.color.primary
            MouseArea {
                anchors.fill: parent
                anchors.margins: -Theme.space.xs
                cursorShape: Qt.SizeAllCursor
                onPositionChanged: (mouse) => {
                    if (!pressed)
                        return
                    const p = mapToItem(editor, mouse.x, mouse.y)
                    const c = editor.curve.slice()
                    c[2 * point.index] = editor.fromX(p.x)
                    c[2 * point.index + 1] = editor.fromY(p.y)
                    editor.inspector.setKeyframeCurve(c[0], c[1], c[2], c[3])
                }
                onReleased: editor.inspector.endGesture()
            }
        }
    }
}
