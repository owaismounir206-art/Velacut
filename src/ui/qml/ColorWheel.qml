// SPDX-License-Identifier: GPL-3.0-or-later
// Interactive color wheel for lift, gamma, gain (SPEC §5.10): hue/saturation disc and level slider.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Velacut.Components
import Velacut.Theme
import Velacut.UI

ColumnLayout {
    id: wheel

    required property Inspector inspector
    required property string zone // "shadows", "midtones", "highlights"
    property string title: ""

    readonly property real red: inspector.values["grade." + zone + ".r"] ?? 0
    readonly property real green: inspector.values["grade." + zone + ".g"] ?? 0
    readonly property real blue: inspector.values["grade." + zone + ".b"] ?? 0
    readonly property real level: inspector.values["grade." + zone + ".level"] ?? 0

    spacing: Theme.space.sm

    RowLayout {
        Layout.fillWidth: true
        Label {
            Layout.fillWidth: true
            text: wheel.title
            role: "titleSmall"
        }
        IconButton {
            iconName: "restart_alt"
            label: qsTr("Reset wheel")
            onClicked: {
                wheel.inspector.set("grade." + wheel.zone + ".r", 0)
                wheel.inspector.set("grade." + wheel.zone + ".g", 0)
                wheel.inspector.set("grade." + wheel.zone + ".b", 0)
                wheel.inspector.set("grade." + wheel.zone + ".level", 0)
                wheel.inspector.endGesture()
            }
        }
    }

    Item {
        id: discContainer
        Layout.alignment: Qt.AlignHCenter
        implicitWidth: 160
        implicitHeight: 160

        Canvas {
            id: discCanvas
            anchors.fill: parent
            onPaint: {
                const ctx = getContext("2d")
                ctx.reset()
                const cx = width / 2
                const cy = height / 2
                const radius = Math.min(cx, cy) - 4

                // Draw gradient hue wheel
                for (let angle = 0; angle < 360; angle += 2) {
                    const rad1 = angle * Math.PI / 180
                    const rad2 = (angle + 2.5) * Math.PI / 180
                    ctx.beginPath()
                    ctx.moveTo(cx, cy)
                    ctx.arc(cx, cy, radius, rad1, rad2)
                    ctx.closePath()
                    const grad = ctx.createRadialGradient(cx, cy, 0, cx, cy, radius)
                    grad.addColorStop(0, "#808080")
                    grad.addColorStop(1, "hsl(" + angle + ", 80%, 50%)")
                    ctx.fillStyle = grad
                    ctx.fill()
                }

                // Crosshairs
                ctx.strokeStyle = "rgba(255, 255, 255, 0.25)"
                ctx.lineWidth = 1
                ctx.beginPath()
                ctx.moveTo(cx - radius, cy)
                ctx.lineTo(cx + radius, cy)
                ctx.moveTo(cx, cy - radius)
                ctx.lineTo(cx, cy + radius)
                ctx.stroke()
            }
        }

        // Draggable handle
        Rectangle {
            id: handle
            width: 14
            height: 14
            radius: 7
            color: "white"
            border.color: "black"
            border.width: 1.5

            // Position from r, g, b offset:
            // R is at 0 deg, G at 120 deg, B at 240 deg
            readonly property real cx: discContainer.width / 2
            readonly property real cy: discContainer.height / 2
            readonly property real wheelRadius: Math.min(cx, cy) - 4

            // Map (r, g, b) offset into (dx, dy)
            readonly property real dx: (wheel.red - (wheel.green + wheel.blue) * 0.5) * wheelRadius
            readonly property real dy: ((wheel.green - wheel.blue) * Math.sqrt(3) / 2.0) * -wheelRadius

            x: cx + dx - width / 2
            y: cy + dy - height / 2

            MouseArea {
                anchors.fill: parent
                anchors.margins: -Theme.space.sm
                cursorShape: Qt.PointingHandCursor

                onPositionChanged: (mouse) => {
                    if (!pressed) return
                    const p = mapToItem(discContainer, mouse.x, mouse.y)
                    const cx = discContainer.width / 2
                    const cy = discContainer.height / 2
                    const maxR = Math.min(cx, cy) - 4
                    let vx = (p.x - cx) / maxR
                    let vy = (p.y - cy) / maxR
                    const dist = Math.sqrt(vx * vx + vy * vy)
                    if (dist > 1.0) {
                        vx /= dist
                        vy /= dist
                    }
                    // Invert (vx, vy) back to r, g, b offsets
                    // vy points down in screen coords, so up is negative y
                    const angle = Math.atan2(-vy, vx)
                    const sat = Math.min(1.0, dist)
                    // RGB projection:
                    const r = sat * Math.cos(angle)
                    const g = sat * Math.cos(angle - 2 * Math.PI / 3)
                    const b = sat * Math.cos(angle - 4 * Math.PI / 3)

                    wheel.inspector.set("grade." + wheel.zone + ".r", Math.round(r * 100) / 100)
                    wheel.inspector.set("grade." + wheel.zone + ".g", Math.round(g * 100) / 100)
                    wheel.inspector.set("grade." + wheel.zone + ".b", Math.round(b * 100) / 100)
                }
                onReleased: wheel.inspector.endGesture()
            }
        }
    }

    PropertySlider {
        Layout.fillWidth: true
        inspector: wheel.inspector
        key: "grade." + wheel.zone + ".level"
        label: qsTr("Level")
        from: -1.0
        to: 1.0
        neutral: 0.0
        format: v => Math.round(v * 100)
    }
}
