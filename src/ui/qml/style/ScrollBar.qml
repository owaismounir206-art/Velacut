// macOS-style scroll bar: a slim pill over an invisible track, widening while pointed at or
// dragged, and fading out after a moment of inactivity (kept while pressed, hovered or moving).
// Like every style file, it applies to plain `ScrollBar {}` uses everywhere (docs/DESIGN_SYSTEM.md §6).
import QtQuick
import QtQuick.Templates as T
import Velacut.Theme

T.ScrollBar {
    id: control

    // The pill: 6 dp thick, 8 dp while pointed at or dragged. Only the thickness is animated
    // (the length is managed by the control); one or two bars per view, so this is cheap.
    readonly property real pillThickness: control.interactive && (control.hovered || control.pressed)
                                           ? Theme.space.sm : Theme.space.xxs + Theme.space.xs
    // Hidden after inactivity (macOS overlay bars): shown again as soon as anything moves.
    property bool hidden: true

    implicitWidth: control.horizontal ? Math.max(implicitBackgroundWidth, implicitContentWidth)
                                      : control.pillThickness
    implicitHeight: control.horizontal ? control.pillThickness
                                       : Math.max(implicitBackgroundHeight, implicitContentHeight)

    background: null // no visible track: content shows through, like an overlay bar

    contentItem: Rectangle {
        radius: Theme.shape.full
        color: Theme.color.onSurfaceVariant
        // The thumb is always clearly visible while the bar is out (hand-picked alphas, like
        // Shadow.qml: no M3 token fits a thumb over arbitrary content).
        opacity: control.hidden ? 0 : control.pressed || control.hovered ? 0.55 : 0.35
        implicitWidth: control.pillThickness
        implicitHeight: control.pillThickness
        Behavior on implicitWidth { NumberAnimation { duration: Theme.motion.short2; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.motion.standard } }
        Behavior on implicitHeight { NumberAnimation { duration: Theme.motion.short2; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.motion.standard } }
    }

    opacity: hidden ? 0 : 1
    Behavior on opacity { NumberAnimation { duration: Theme.motion.short3 } }

    Timer {
        id: hideTimer
        interval: Theme.editor.autoHideDelay
        onTriggered: control.hidden = true
    }

    // Out while anything moves (macOS): dragged, hovered, or in use (`active` covers the
    // attached view scrolling, including the wheel); a programmatic scroll shows in the
    // position change and is hidden again after the delay.
    onActiveChanged: updateHidden()
    onPressedChanged: updateHidden()
    onHoveredChanged: updateHidden()
    onPositionChanged: updateHidden()
    function updateHidden() {
        if (hovered || pressed || active) {
            control.hidden = false
        } else if (visible) {
            hideTimer.restart()
        }
    }

    Component.onCompleted: updateHidden()
}
