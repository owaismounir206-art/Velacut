// The application window: the home screen with the drafts, or the editor of the open project (SPEC 0bis rules 1–2).
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material as M
import QtQuick.Layouts
import Vedit.Components
import Vedit.Theme
import Vedit.UI

ApplicationWindow {
    id: window

    width: 1440
    height: 900
    minimumWidth: 960
    minimumHeight: 600
    visible: true
    title: App.editor ? qsTr("%1 — vedit").arg(App.editor.name) : "vedit"
    color: Theme.color.surface

    // Colors of the Qt Material controls this style does not redefine.
    M.Material.theme: Theme.dark ? M.Material.Dark : M.Material.Light
    M.Material.accent: Theme.color.primary
    M.Material.primary: Theme.color.primary
    M.Material.background: Theme.color.surface
    M.Material.foreground: Theme.color.onSurface

    function showMessage(text, undoable) {
        if (undoable && App.editor) {
            const editor = App.editor
            snackbar.show(text, qsTr("Undo"), () => editor.undo())
        } else {
            snackbar.show(text, "")
        }
    }

    Connections {
        target: App
        function onMessage(text) { window.showMessage(text, false) }
    }

    Loader {
        anchors.fill: parent
        sourceComponent: App.editor ? editorScreen : homeScreen
    }
    Component {
        id: homeScreen
        HomeScreen {
            onInfoRequested: infoDialog.open()
            onPreferencesRequested: preferences.open()
        }
    }
    Component {
        id: editorScreen
        EditorScreen {
            editor: App.editor
            onMessage: (text, undoable) => window.showMessage(text, undoable)
            onInfoRequested: infoDialog.open()
            onPreferencesRequested: preferences.open()
            onAiModelsRequested: preferences.openSection(preferences.aiModelsSection)
        }
    }

    // Preferences: from the home screen, the editor's menu, or Ctrl+, anywhere.
    PreferencesDialog {
        id: preferences
        objectName: "preferencesDialog"
        anchors.centerIn: parent
        width: Math.min(window.width - 2 * Theme.space.xl, Theme.editor.dialogWidth * 1.4)
        height: Math.min(window.height - 2 * Theme.space.xl, Theme.editor.dialogWidth * 1.25)
        // A component or a model may have been installed meanwhile.
        onClosed: if (App.editor) App.editor.ai.refreshSpeech()
    }
    Shortcut { sequence: "Ctrl+,"; onActivated: preferences.open() }

    // Leaving the window or the app: everything is saved already, this writes the last second of changes.
    onActiveChanged: if (!active && App.editor) App.editor.saveNow()

    Snackbar {
        id: snackbar
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Theme.space.xxxl
        z: 100
    }

    Dialog {
        id: infoDialog
        title: qsTr("System information")
        iconName: "info"
        anchors.centerIn: parent
        width: Math.min(window.width - 2 * Theme.space.xl, Theme.editor.dialogWidth)
        standardButtons: Dialog.Close

        ColumnLayout {
            width: parent.width
            spacing: Theme.space.md
            ScrollView {
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(window.height / 2, info.implicitHeight)
                clip: true
                Label {
                    id: info
                    width: infoDialog.availableWidth
                    role: "bodySmall"
                    color: Theme.color.onSurfaceVariant
                    wrapMode: Text.WrapAnywhere
                    textFormat: Text.PlainText
                    text: App.systemInformation
                }
            }
            Button {
                variant: "tonal"
                iconName: "content_copy"
                text: qsTr("Copy system information")
                onClicked: {
                    App.copySystemInformation()
                    snackbar.show(qsTr("System information copied"), "")
                }
            }
        }
    }
}
