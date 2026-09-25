// A group of properties: title, "Reset" once something changed (SPEC 0bis rule 8) and "Apply to all" (rule 5).
// Controls marked `advanced` go in a closed "Advanced" part (rule 7).
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Vedit.Components
import Vedit.Theme
import Vedit.UI

ColumnLayout {
    id: root

    required property Inspector inspector
    property string section
    property string title
    property bool applyToAll: true
    property string applyToAllText: qsTr("Apply to all")
    default property alias content: body.data
    property alias advanced: advancedBody.data
    property bool advancedOpen: false

    Layout.fillWidth: true
    spacing: Theme.space.sm
    objectName: "section_" + section

    RowLayout {
        Layout.fillWidth: true
        Label {
            Layout.fillWidth: true
            text: root.title
            role: "titleSmall"
        }
        Button {
            objectName: "reset_" + root.section
            variant: "text"
            iconName: "restart_alt"
            text: qsTr("Reset")
            visible: root.inspector.modifiedSections.includes(root.section)
            onClicked: root.inspector.reset(root.section)
        }
    }
    ColumnLayout {
        id: body
        Layout.fillWidth: true
        spacing: Theme.space.sm
    }
    Button {
        visible: advancedBody.children.length > 0
        variant: "text"
        iconName: root.advancedOpen ? "expand_less" : "expand_more"
        text: qsTr("Advanced")
        onClicked: root.advancedOpen = !root.advancedOpen
    }
    ColumnLayout {
        id: advancedBody
        Layout.fillWidth: true
        visible: root.advancedOpen
        spacing: Theme.space.sm
    }
    Button {
        objectName: "applyAll_" + root.section
        visible: root.applyToAll
        Layout.fillWidth: true
        variant: "tonal"
        text: root.applyToAllText
        onClicked: root.inspector.applyToAll(root.section)
    }
    Divider {
        Layout.fillWidth: true
        Layout.topMargin: Theme.space.sm
    }
}
