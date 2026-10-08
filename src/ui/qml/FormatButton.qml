// The format of the canvas (16:9, 9:16, 1:1…): one click to change it (SPEC 0bis rule 1), under the player (SPEC §4).
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import Velacut.UI

Button {
    id: button

    required property Editor editor

    objectName: "formatButton"
    variant: "outlined"
    iconName: "aspect_ratio"
    text: editor.formatText
    Accessible.name: qsTr("Format: %1").arg(editor.formatText)
    ToolTip.visible: hovered
    ToolTip.text: qsTr("Change the format of the video")
    onClicked: formatMenu.popup()

    Menu {
        id: formatMenu
        Repeater {
            // Values of CanvasPreset (core/project/Sequence.h).
            model: [{ text: qsTr("16:9 — YouTube"), preset: 0, icon: "crop_16_9" },
                    { text: qsTr("9:16 — TikTok, Reels, Shorts"), preset: 1, icon: "crop_portrait" },
                    { text: qsTr("1:1 — Square"), preset: 2, icon: "crop_square" },
                    { text: qsTr("4:5 — Instagram post"), preset: 3, icon: "crop_portrait" },
                    { text: qsTr("3:4 — Portrait"), preset: 5, icon: "crop_3_2" },
                    { text: qsTr("21:9 — Cinema"), preset: 4, icon: "panorama" }]
            delegate: MenuItem {
                required property var modelData
                objectName: "format_" + modelData.preset
                iconName: modelData.icon
                text: modelData.text
                checkable: true
                checked: button.editor.canvasPreset === modelData.preset
                onTriggered: button.editor.setCanvasPreset(modelData.preset)
            }
        }
        MenuSeparator {}
        // One click (SPEC 0bis rule 9): the new format, and each video follows its subject in it.
        Repeater {
            model: [{ text: qsTr("Adapt to 9:16 — follow the subject"), preset: 1 },
                    { text: qsTr("Adapt to 1:1 — follow the subject"), preset: 2 },
                    { text: qsTr("Adapt to 4:5 — follow the subject"), preset: 3 }]
            delegate: MenuItem {
                required property var modelData
                objectName: "reframe_" + modelData.preset
                iconName: "center_focus_strong"
                text: modelData.text
                enabled: !button.editor.ai.busy
                onTriggered: button.editor.ai.autoReframe(modelData.preset)
            }
        }
    }
}
