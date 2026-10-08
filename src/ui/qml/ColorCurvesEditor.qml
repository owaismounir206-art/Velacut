// SPDX-License-Identifier: GPL-3.0-or-later
// Interactive RGB and channel curve editor (SPEC §5.10): click to add point, drag to adjust, double-click to remove.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Velacut.Components
import Velacut.Theme
import Velacut.UI

ColumnLayout {
    id: curveEditor

    required property Inspector inspector
    property string activeChannel: "master" // "master", "r", "g", "b"

    readonly property var points: {
        const raw = inspector.values["grade.curve." + activeChannel]
        if (Array.isArray(raw) && raw.length > 0)
            return raw
        return [[0.0, 0.0], [1.0, 1.0]]
    }

    spacing: Theme.space.sm

    RowLayout {
        Layout.fillWidth: true
        SegmentedButton {
            Layout.fillWidth: true
            model: [
                { text: qsTr("RGB") },
                { text: qsTr("Red") },
                { text: qsTr("Green") },
                { text: qsTr("Blue") }
            ]
            currentIndex: curveEditor.activeChannel === "r" ? 1
                        : curveEditor.activeChannel === "g" ? 2
                        : curveEditor.activeChannel === "b" ? 3 : 0
            onActivated: (index) => {
                const chs = ["master", "r", "g", "b"]
                curveEditor.activeChannel = chs[index]
            }
        }
        IconButton {
            iconName: "restart_alt"
            label: qsTr("Reset curve")
            onClicked: {
                curveEditor.inspector.set("grade.curve." + curveEditor.activeChannel, [])
                curveEditor.inspector.endGesture()
            }
        }
    }

    Item {
        id: graphArea
        Layout.fillWidth: true
        implicitHeight: 180

        readonly property real pad: 12
        function toX(v) { return pad + v * (width - 2 * pad) }
        function toY(v) { return pad + (1.0 - v) * (height - 2 * pad) }
        function fromX(px) { return Math.max(0, Math.min(1, (px - pad) / (width - 2 * pad))) }
        function fromY(py) { return Math.max(0, Math.min(1, 1.0 - (py - pad) / (height - 2 * pad))) }

        Rectangle {
            anchors.fill: parent
            radius: Theme.shape.small
            color: "#18181c"
            border.color: Theme.color.outlineVariant
            border.width: 1
        }

        Canvas {
            id: curveCanvas
            anchors.fill: parent
            onPaint: {
                const ctx = getContext("2d")
                ctx.reset()

                // Diagonal reference
                ctx.strokeStyle = "rgba(255, 255, 255, 0.15)"
                ctx.lineWidth = 1
                ctx.setLineDash([4, 4])
                ctx.beginPath()
                ctx.moveTo(graphArea.toX(0), graphArea.toY(0))
                ctx.lineTo(graphArea.toX(1), graphArea.toY(1))
                ctx.stroke()
                ctx.setLineDash([])

                // Channel curve color
                let col = "#ffffff"
                if (curveEditor.activeChannel === "r") col = "#ff5555"
                else if (curveEditor.activeChannel === "g") col = "#55ff55"
                else if (curveEditor.activeChannel === "b") col = "#5588ff"

                ctx.strokeStyle = col
                ctx.lineWidth = 2
                ctx.beginPath()

                const pts = curveEditor.points.slice().sort((a, b) => a[0] - b[0])
                if (pts.length === 0) return

                ctx.moveTo(graphArea.toX(pts[0][0]), graphArea.toY(pts[0][1]))
                for (let i = 1; i < pts.length; ++i) {
                    // Draw smooth curve segments
                    const p0 = pts[Math.max(0, i - 1)]
                    const p1 = pts[i]
                    const midX = (p0[0] + p1[0]) / 2.0
                    ctx.bezierCurveTo(graphArea.toX(midX), graphArea.toY(p0[1]),
                                      graphArea.toX(midX), graphArea.toY(p1[1]),
                                      graphArea.toX(p1[0]), graphArea.toY(p1[1]))
                }
                ctx.stroke()
            }
        }

        Connections {
            target: curveEditor
            function onPointsChanged() { curveCanvas.requestPaint() }
            function onActiveChannelChanged() { curveCanvas.requestPaint() }
        }
        onWidthChanged: curveCanvas.requestPaint()

        // Click on background to add a point
        MouseArea {
            anchors.fill: parent
            onClicked: (mouse) => {
                const nx = graphArea.fromX(mouse.x)
                const ny = graphArea.fromY(mouse.y)
                const list = curveEditor.points.slice()
                list.push([Math.round(nx * 100) / 100, Math.round(ny * 100) / 100])
                list.sort((a, b) => a[0] - b[0])
                curveEditor.inspector.set("grade.curve." + curveEditor.activeChannel, list)
                curveEditor.inspector.endGesture()
            }
        }

        // Draggable control points
        Repeater {
            model: curveEditor.points
            delegate: Rectangle {
                id: ptDot
                required property var modelData
                required property int index

                readonly property real px: modelData[0]
                readonly property real py: modelData[1]

                x: graphArea.toX(px) - width / 2
                y: graphArea.toY(py) - height / 2
                width: 12
                height: 12
                radius: 6
                color: {
                    if (curveEditor.activeChannel === "r") return "#ff5555"
                    if (curveEditor.activeChannel === "g") return "#55ff55"
                    if (curveEditor.activeChannel === "b") return "#5588ff"
                    return "#ffffff"
                }
                border.color: "#000000"
                border.width: 1.5

                MouseArea {
                    anchors.fill: parent
                    anchors.margins: -4
                    cursorShape: Qt.PointingHandCursor
                    preventStealing: true

                    onDoubleClicked: {
                        const list = curveEditor.points.slice()
                        if (list.length > 2) {
                            list.splice(ptDot.index, 1)
                            curveEditor.inspector.set("grade.curve." + curveEditor.activeChannel, list)
                            curveEditor.inspector.endGesture()
                        }
                    }

                    onPositionChanged: (mouse) => {
                        if (!pressed) return
                        const p = mapToItem(graphArea, mouse.x, mouse.y)
                        let nx = graphArea.fromX(p.x)
                        let ny = graphArea.fromY(p.y)
                        // If end points (first or last), constrain x to 0 or 1
                        if (ptDot.index === 0 && ptDot.px <= 0.05) nx = 0.0
                        if (ptDot.index === curveEditor.points.length - 1 && ptDot.px >= 0.95) nx = 1.0

                        const list = curveEditor.points.slice()
                        list[ptDot.index] = [Math.round(nx * 100) / 100, Math.round(ny * 100) / 100]
                        list.sort((a, b) => a[0] - b[0])
                        curveEditor.inspector.set("grade.curve." + curveEditor.activeChannel, list)
                    }

                    onReleased: curveEditor.inspector.endGesture()
                }
            }
        }
    }
}
