// Material 3 small top app bar: leading slot, title (editable project name elsewhere), trailing actions.
import QtQuick
import QtQuick.Layouts
import Velacut.Theme

Rectangle {
    id: root

    property string title
    property alias leading: leadingSlot.data
    property alias trailing: trailingSlot.data
    // Tonal elevation when content scrolls under the bar.
    property bool scrolled: false

    implicitHeight: 64
    // Opaque until content scrolls under the bar; then the "tonal" surface becomes a frosted
    // material, lite (92%, see style/Dialog.qml): the macOS feel, without a backdrop blur
    // (impossible without shaders). Still >90% opaque: the title keeps its contrast.
    color: scrolled ? Theme.alpha(Theme.color.surfaceContainer, 0.92) : Theme.color.surface
    Behavior on color { ColorAnimation { duration: Theme.motion.short4 } }
    // The glass edge under the bar, fading in with the frosted surface.
    Rectangle {
        anchors { bottom: parent.bottom; left: parent.left; right: parent.right }
        height: Theme.editor.hairline
        color: Theme.alpha(Theme.color.outlineVariant, 0.14)
        opacity: root.scrolled ? 1 : 0
        visible: opacity > 0
        Behavior on opacity { NumberAnimation { duration: Theme.motion.short4 } }
    }

    Accessible.role: Accessible.ToolBar

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 4
        anchors.rightMargin: 12
        spacing: 4
        Row {
            id: leadingSlot
            Layout.alignment: Qt.AlignVCenter
        }
        TypeText {
            Layout.fillWidth: true
            Layout.leftMargin: 12
            text: root.title
            role: "titleLarge"
            elide: Text.ElideRight
        }
        Row {
            id: trailingSlot
            spacing: 8
            Layout.alignment: Qt.AlignVCenter
        }
    }
}
