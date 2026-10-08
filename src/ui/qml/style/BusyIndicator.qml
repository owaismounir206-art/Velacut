// Material 3 circular progress indicator (indeterminate), drawn with a rotating arc (no shaders).
import QtQuick
import QtQuick.Shapes
import QtQuick.Templates as T
import Velacut.Components
import Velacut.Theme

T.BusyIndicator {
    id: control

    // 0..1 for a determinate indicator; negative = indeterminate.
    property real progress: -1

    implicitWidth: 48
    implicitHeight: 48

    Accessible.role: Accessible.ProgressBar

    contentItem: Item {
        visible: control.running || control.progress >= 0
        Shape {
            id: shape
            anchors.fill: parent
            anchors.margins: 4
            RotationAnimator on rotation {
                running: control.progress < 0 && control.running && control.visible
                from: 0
                to: 360
                duration: Theme.motion.essentialLong * 3
                loops: Animation.Infinite
            }
            ShapePath {
                strokeWidth: 4
                strokeColor: Theme.color.primary
                fillColor: "transparent"
                capStyle: ShapePath.RoundCap
                PathAngleArc {
                    centerX: shape.width / 2
                    centerY: shape.height / 2
                    radiusX: shape.width / 2 - 2
                    radiusY: shape.height / 2 - 2
                    startAngle: -90
                    sweepAngle: control.progress >= 0 ? 360 * control.progress : 270
                }
            }
        }
    }
}
