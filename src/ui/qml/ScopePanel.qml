// SPDX-License-Identifier: GPL-3.0-or-later
// Video scopes panel (SPEC §5.10): histogram, waveform and vectorscope view.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Vedit.Components
import Vedit.Theme
import Vedit.UI

Rectangle {
    id: scopePanel

    required property var sink
    signal closeRequested()

    color: Theme.color.surfaceContainerHigh
    radius: Theme.shape.medium
    border.color: Theme.color.outlineVariant
    border.width: 1

    property int activeMode: ScopeView.Histogram

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.space.sm
        spacing: Theme.space.xs

        RowLayout {
            Layout.fillWidth: true
            Label {
                text: qsTr("Scopes")
                role: "titleSmall"
            }
            Item { Layout.fillWidth: true }
            SegmentedButton {
                id: modeSelector
                model: [
                    { text: qsTr("Histogram") },
                    { text: qsTr("Waveform") },
                    { text: qsTr("Vectorscope") }
                ]
                currentIndex: scopePanel.activeMode
                onActivated: (index) => scopePanel.activeMode = index
            }
            IconButton {
                iconName: "close"
                label: qsTr("Close scopes")
                onClicked: scopePanel.closeRequested()
            }
        }

        ScopeView {
            id: scope
            objectName: "scopeView"
            Layout.fillWidth: true
            Layout.fillHeight: true
            sink: scopePanel.sink
            mode: scopePanel.activeMode
        }
    }
}
