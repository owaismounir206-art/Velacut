// The keyboard focus ring, macOS-style: a 2 dp primary border with a 2 dp gap around the control's
// own shape — sharper than the M3 focus state layer (10%) for people who drive the interface with
// the keyboard. It shows only on `visualFocus` (keyboard focus): mouse focus keeps the light M3
// state layer, which already tells the focused control. Put it inside the control's background
// Rectangle (it reads its radius).
import QtQuick
import Velacut.Theme

Rectangle {
    // The focused control (`var`: it must simply have the focus properties — T.TextField
    // derives from TextInput, not from QQuickControl, so a typed property would refuse it).
    required property var control

    // Keyboard focus only: Control exposes visualFocus, TextField (not a Control) falls back
    // to the focus reason. The `=== true` comparison is safe while `control` is still null
    // during construction, and when a type has no visualFocus at all.
    visible: control && control.enabled
             && (control.visualFocus === true
                 || (control.activeFocus && control.focusReason === Qt.TabFocusReason))
    anchors.fill: parent
    anchors.margins: -Theme.space.xs // the border + the gap outside the shape
    radius: parent.radius + Theme.space.xs
    color: "transparent"
    border.width: Theme.space.xxs
    border.color: Theme.color.primary
}
