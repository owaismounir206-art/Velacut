// Material 3 state layer (hover 8%, focus 10%, pressed 10%, dragged 16%) with a ripple that grows from the
// press point to the exact shape of the control (no shaders: works with the software backend too).
import QtQuick
import Vedit.Theme

Item {
    id: root

    property color color: Theme.color.onSurface
    property real radius: 0
    property bool hovered: false
    property bool pressed: false
    property bool focused: false
    property bool dragged: false
    property bool active: true
    // Press position in local coordinates (for the ripple origin).
    property point pressPoint: Qt.point(width / 2, height / 2)

    anchors.fill: parent

    Rectangle {
        anchors.fill: parent
        radius: root.radius
        color: root.color
        opacity: !root.active ? 0
               : root.dragged ? Theme.state.dragged
               : root.pressed && ripple.width === 0 ? Theme.state.pressed
               : root.focused ? Theme.state.focus
               : root.hovered ? Theme.state.hover : 0
        Behavior on opacity { NumberAnimation { duration: Theme.motion.short2 } }
    }

    Item {
        anchors.fill: parent
        clip: true
        visible: !Theme.motion.reduced

        Rectangle {
            id: ripple
            color: root.color
            opacity: 0
            width: 0
            height: 0
            radius: Math.min(root.radius, width / 2, height / 2)
        }
    }

    ParallelAnimation {
        id: grow
        NumberAnimation { target: ripple; property: "x"; to: 0; duration: Theme.motion.medium4; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.motion.emphasizedDecelerate }
        NumberAnimation { target: ripple; property: "y"; to: 0; duration: Theme.motion.medium4; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.motion.emphasizedDecelerate }
        NumberAnimation { target: ripple; property: "width"; to: root.width; duration: Theme.motion.medium4; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.motion.emphasizedDecelerate }
        NumberAnimation { target: ripple; property: "height"; to: root.height; duration: Theme.motion.medium4; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.motion.emphasizedDecelerate }
    }
    NumberAnimation {
        id: fade
        target: ripple; property: "opacity"; to: 0; duration: Theme.motion.medium2
        onFinished: { ripple.width = 0; ripple.height = 0 }
    }

    onPressedChanged: {
        if (Theme.motion.reduced || !active)
            return
        if (pressed) {
            fade.stop()
            ripple.x = pressPoint.x
            ripple.y = pressPoint.y
            ripple.width = 0
            ripple.height = 0
            ripple.opacity = Theme.state.pressed
            grow.restart()
        } else {
            fade.restart()
        }
    }
}
