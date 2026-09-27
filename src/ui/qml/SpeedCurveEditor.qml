// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Vedit.Theme
import Vedit.UI

Item {
    id: editor

    required property Inspector inspector
    readonly property var rawPoints: inspector.values["speed.curvePoints"] ?? []
    readonly property var points: {
        if (rawPoints && rawPoints.length >= 2) {
            return rawPoints
        }
        return [[0.0, 1.0], [1.0, 1.0]]
    }

    readonly property real pad: Theme.editor.handleSize
    property int activeIndex: -1

    function toX(u) {
        return pad + Math.max(0, Math.min(1, u)) * (width - 2 * pad)
    }

    function toY(speed) {
        // Logarithmic scale: 0.1x (bottom) to 10.0x (top), 1.0x at 50% height
        const s = Math.max(0.1, Math.min(10.0, speed))
        const norm = (Math.log10(s) + 1.0) / 2.0
        return height - pad - norm * (height - 2 * pad)
    }

    function fromX(px) {
        return Math.max(0, Math.min(1, (px - pad) / Math.max(1, width - 2 * pad)))
    }

    function fromY(py) {
        const norm = Math.max(0, Math.min(1, (height - pad - py) / Math.max(1, height - 2 * pad)))
        return Math.max(0.1, Math.min(10.0, Math.pow(10.0, 2.0 * norm - 1.0)))
    }

    function evaluateSpline(pts, u) {
        if (!pts || pts.length === 0) return 1.0
        if (pts.length === 1) return pts[0][1]
        if (u <= pts[0][0]) return pts[0][1]
        if (u >= pts[pts.length - 1][0]) return pts[pts.length - 1][1]

        let i = 0
        while (i < pts.length - 1 && pts[i + 1][0] < u) {
            i++
        }
        const x0 = pts[i][0], x1 = pts[i + 1][0]
        const y0 = pts[i][1], y1 = pts[i + 1][1]
        const h = x1 - x0
        if (h <= 1e-6) return y0

        const t = (u - x0) / h
        const delta = (y1 - y0) / h
        let d0 = delta, d1 = delta
        if (i > 0) {
            const hPrev = x0 - pts[i - 1][0]
            const dPrev = (y0 - pts[i - 1][1]) / (hPrev > 1e-6 ? hPrev : 1e-6)
            if (dPrev * delta > 0) {
                d0 = 2.0 / (1.0 / dPrev + 1.0 / delta)
            } else {
                d0 = 0
            }
        }
        if (i < pts.length - 2) {
            const hNext = pts[i + 2][0] - x1
            const dNext = (pts[i + 2][1] - y1) / (hNext > 1e-6 ? hNext : 1e-6)
            if (dNext * delta > 0) {
                d1 = 2.0 / (1.0 / delta + 1.0 / dNext)
            } else {
                d1 = 0
            }
        }
        const t2 = t * t, t3 = t2 * t
        const h00 = 2 * t3 - 3 * t2 + 1
        const h10 = t3 - 2 * t2 + t
        const h01 = -2 * t3 + 3 * t2
        const h11 = t3 - t2
        return Math.max(0.1, Math.min(10.0, h00 * y0 + h10 * h * d0 + h01 * y1 + h11 * h * d1))
    }

    objectName: "speedCurveEditor"
    implicitHeight: 180
    onPointsChanged: canvas.requestPaint()
    onWidthChanged: canvas.requestPaint()
    onHeightChanged: canvas.requestPaint()
    Accessible.role: Accessible.Graphic
    Accessible.name: qsTr("Curva di velocità")

    Rectangle {
        anchors.fill: parent
        radius: Theme.shape.small
        color: Theme.color.surfaceContainerHighest
        clip: true

        // Grid lines for reference speeds (0.25x, 0.5x, 1x, 2x, 4x, 8x)
        Canvas {
            id: canvas
            anchors.fill: parent
            onPaint: {
                const ctx = getContext("2d")
                ctx.reset()

                // Baseline 1.0x (normal speed)
                const y1x = editor.toY(1.0)
                ctx.strokeStyle = Theme.color.outlineVariant
                ctx.lineWidth = 1
                ctx.setLineDash([4, 4])
                ctx.beginPath()
                ctx.moveTo(editor.pad, y1x)
                ctx.lineTo(width - editor.pad, y1x)
                ctx.stroke()
                ctx.setLineDash([])

                // Secondary reference lines at 0.5x and 2.0x
                ctx.strokeStyle = Theme.color.surfaceContainer
                ctx.lineWidth = 0.5
                for (const refSpeed of [0.25, 0.5, 2.0, 4.0, 8.0]) {
                    const y = editor.toY(refSpeed)
                    ctx.beginPath()
                    ctx.moveTo(editor.pad, y)
                    ctx.lineTo(width - editor.pad, y)
                    ctx.stroke()
                }

                // Shaded area under the curve
                const pts = editor.points
                if (pts.length >= 2) {
                    ctx.fillStyle = Qt.rgba(Theme.color.primary.r, Theme.color.primary.g, Theme.color.primary.b, 0.12)
                    ctx.beginPath()
                    ctx.moveTo(editor.toX(0), height - editor.pad)
                    const steps = Math.max(50, Math.round(width / 4))
                    for (let s = 0; s <= steps; s++) {
                        const u = s / steps
                        const spd = editor.evaluateSpline(pts, u)
                        ctx.lineTo(editor.toX(u), editor.toY(spd))
                    }
                    ctx.lineTo(editor.toX(1.0), height - editor.pad)
                    ctx.closePath()
                    ctx.fill()

                    // The speed curve line
                    ctx.strokeStyle = Theme.color.primary
                    ctx.lineWidth = Theme.editor.selectionBorder
                    ctx.beginPath()
                    for (let s = 0; s <= steps; s++) {
                        const u = s / steps
                        const spd = editor.evaluateSpline(pts, u)
                        const px = editor.toX(u)
                        const py = editor.toY(spd)
                        if (s === 0) {
                            ctx.moveTo(px, py)
                        } else {
                            ctx.lineTo(px, py)
                        }
                    }
                    ctx.stroke()
                }
            }
        }

        // Labels for reference lines
        Text {
            x: 6
            y: editor.toY(1.0) - font.pixelSize - 2
            text: "1.0×"
            font.pixelSize: 10
            color: Theme.color.onSurfaceVariant
        }
        Text {
            x: 6
            y: editor.toY(4.0) - font.pixelSize - 2
            text: "4.0×"
            font.pixelSize: 9
            color: Theme.color.outline
        }
        Text {
            x: 6
            y: editor.toY(0.25) - font.pixelSize - 2
            text: "0.25×"
            font.pixelSize: 9
            color: Theme.color.outline
        }

        // Control point handles
        Repeater {
            model: editor.points.length
            delegate: Rectangle {
                id: handle
                required property int index
                readonly property var pt: editor.points[index] ?? [0, 1]
                readonly property real pX: pt[0]
                readonly property real pY: pt[1]

                objectName: "speedCurvePoint" + index
                width: Theme.editor.handleSize + 2
                height: Theme.editor.handleSize + 2
                radius: width / 2
                x: editor.toX(pX) - width / 2
                y: editor.toY(pY) - height / 2
                color: editor.activeIndex === index ? Theme.color.tertiary : Theme.color.primary
                border.color: Theme.color.surface
                border.width: 2

                // Tooltip showing current speed value on drag/hover
                Rectangle {
                    visible: editor.activeIndex === handle.index
                    anchors.bottom: parent.top
                    anchors.bottomMargin: 4
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: valLabel.implicitWidth + 8
                    height: valLabel.implicitHeight + 4
                    radius: 4
                    color: Theme.color.inverseSurface

                    Text {
                        id: valLabel
                        anchors.centerIn: parent
                        text: (Math.round(handle.pY * 10) / 10).toFixed(1) + "×"
                        font.pixelSize: 10
                        font.bold: true
                        color: Theme.color.inverseOnSurface
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    anchors.margins: -Theme.space.xs
                    cursorShape: Qt.SizeAllCursor
                    acceptedButtons: Qt.LeftButton | Qt.RightButton

                    onPressed: (mouse) => {
                        if (mouse.button === Qt.RightButton) {
                            // Right-click removes intermediate point (SPEC §5.5)
                            if (handle.index > 0 && handle.index < editor.points.length - 1 && editor.points.length > 2) {
                                const newPts = []
                                for (let k = 0; k < editor.points.length; k++) {
                                    if (k !== handle.index) {
                                        newPts.push(editor.points[k])
                                    }
                                }
                                editor.inspector.set("speed.curvePoints", newPts)
                                editor.inspector.endGesture()
                            }
                            return
                        }
                        editor.activeIndex = handle.index
                    }

                    onPositionChanged: (mouse) => {
                        if (!pressed || mouse.buttons !== Qt.LeftButton) return
                        const p = mapToItem(editor, mouse.x, mouse.y)
                        const newSpeed = editor.fromY(p.y)
                        let newU = editor.fromX(p.x)

                        // Boundary constraints: first point fixed at u=0, last at u=1
                        if (handle.index === 0) {
                            newU = 0.0
                        } else if (handle.index === editor.points.length - 1) {
                            newU = 1.0
                        } else {
                            // Intermediate point: maintain ordering between neighbors with min separation
                            const prevU = editor.points[handle.index - 1][0] + 0.02
                            const nextU = editor.points[handle.index + 1][0] - 0.02
                            newU = Math.max(prevU, Math.min(nextU, newU))
                        }

                        const newPts = []
                        for (let k = 0; k < editor.points.length; k++) {
                            if (k === handle.index) {
                                newPts.push([newU, Math.round(newSpeed * 100) / 100])
                            } else {
                                newPts.push(editor.points[k])
                            }
                        }
                        editor.inspector.set("speed.curvePoints", newPts)
                    }

                    onReleased: {
                        editor.activeIndex = -1
                        editor.inspector.endGesture()
                    }
                }
            }
        }

        // Click to add a new point anywhere on the curve
        MouseArea {
            anchors.fill: parent
            z: -1
            onDoubleClicked: (mouse) => {
                const clickU = editor.fromX(mouse.x)
                if (clickU <= 0.02 || clickU >= 0.98) return
                const clickSpeed = editor.fromY(mouse.y)
                const newPts = []
                let inserted = false
                for (let k = 0; k < editor.points.length; k++) {
                    const pt = editor.points[k]
                    if (!inserted && pt[0] > clickU) {
                        newPts.push([clickU, Math.round(clickSpeed * 100) / 100])
                        inserted = true
                    }
                    newPts.push(pt)
                }
                if (!inserted) {
                    newPts.push([clickU, Math.round(clickSpeed * 100) / 100])
                }
                editor.inspector.set("speed.curvePoints", newPts)
                editor.inspector.endGesture()
            }
        }
    }
}
