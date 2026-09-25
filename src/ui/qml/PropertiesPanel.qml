// Properties of the selected clip, on the right (SPEC §4): tabs Text · Video · Audio · Speed · Adjust, the
// essential controls first, "Advanced" closed, "Reset" and "Apply to all" in every section (SPEC 0bis rules 5–8).
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Vedit.Components
import Vedit.Theme
import Vedit.UI

Rectangle {
    id: panel

    required property Editor editor
    readonly property Inspector inspector: editor.inspector
    readonly property var values: inspector.values
    readonly property var sections: inspector.sections

    // The tabs the selected clip has, each with the page it shows.
    readonly property var tabs: {
        const list = []
        if (sections.includes("transition"))
            list.push({ text: qsTr("Transition"), page: "transition" })
        if (sections.includes("text"))
            list.push({ text: qsTr("Text"), page: "text" })
        if (sections.includes("video"))
            list.push({ text: inspector.kind === Inspector.Text ? qsTr("Position") : qsTr("Video"), page: "video" })
        if (sections.includes("audio"))
            list.push({ text: qsTr("Audio"), page: "audio" })
        if (sections.includes("speed"))
            list.push({ text: qsTr("Speed"), page: "speed" })
        if (sections.includes("animation"))
            list.push({ text: qsTr("Animation"), page: "animation" })
        if (sections.includes("adjust"))
            list.push({ text: qsTr("Adjust"), page: "adjust" })
        return list
    }
    // The page chosen by the user, kept while the selection changes when the new clip has it too.
    property string wantedPage: "video"
    readonly property string page: {
        for (const tab of tabs)
            if (tab.page === wantedPage)
                return wantedPage
        return tabs.length > 0 ? tabs[0].page : ""
    }
    function showPage(name) { wantedPage = name }

    color: Theme.color.surfaceContainerLow

    // Nothing selected: say how to get here.
    ColumnLayout {
        anchors.centerIn: parent
        width: parent.width - 2 * Theme.space.xl
        visible: !panel.inspector.active
        spacing: Theme.space.sm
        Icon {
            Layout.alignment: Qt.AlignHCenter
            name: "touch_app"
            color: Theme.color.onSurfaceVariant
        }
        Label {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            role: "bodyMedium"
            color: Theme.color.onSurfaceVariant
            text: qsTr("Select a clip on the timeline to change it here.")
        }
    }

    ColumnLayout {
        anchors.fill: parent
        visible: panel.inspector.active
        spacing: 0

        Tabs {
            id: tabBar
            objectName: "propertiesTabs"
            Layout.fillWidth: true
            model: panel.tabs
            currentIndex: panel.tabs.findIndex(tab => tab.page === panel.page)
            onActivated: (index) => panel.showPage(panel.tabs[index].page)
        }
        Label {
            Layout.fillWidth: true
            Layout.margins: Theme.space.md
            Layout.bottomMargin: 0
            visible: panel.inspector.selectedCount > 1
            wrapMode: Text.WordWrap
            role: "bodySmall"
            color: Theme.color.onSurfaceVariant
            text: qsTr("%n clip(s) selected: changes apply to all of them.", "", panel.inspector.selectedCount)
        }

        ScrollView {
            id: scroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: availableWidth
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

            ColumnLayout {
                width: scroll.availableWidth - 2 * Theme.space.md
                x: Theme.space.md
                spacing: Theme.space.md

                Item { implicitHeight: Theme.space.xs }

                // ---- Transition --------------------------------------------------------------------------------
                ColumnLayout {
                    Layout.fillWidth: true
                    visible: panel.page === "transition"
                    spacing: Theme.space.md

                    Label {
                        Layout.fillWidth: true
                        role: "titleSmall"
                        text: panel.values["transition.name"] ?? ""
                    }
                    PropertySlider {
                        inspector: panel.inspector
                        key: "transition.duration"
                        label: qsTr("Duration")
                        from: 0.1
                        to: Math.max(0.2, panel.values["transition.maxDuration"] ?? 1)
                        neutral: 0.5
                        stepSize: 0.1
                        format: v => qsTr("%1 s").arg(v.toLocaleString(Qt.locale(), "f", 1))
                    }
                    Button {
                        objectName: "transitionPanelApplyAll"
                        Layout.fillWidth: true
                        variant: "tonal"
                        text: qsTr("Apply to all cuts")
                        onClicked: panel.inspector.applyToAll("transition")
                    }
                    Button {
                        Layout.fillWidth: true
                        variant: "text"
                        iconName: "delete"
                        text: qsTr("Remove transition")
                        onClicked: panel.inspector.removeTransition()
                    }
                }

                // ---- Text ------------------------------------------------------------------------------------
                ColumnLayout {
                    Layout.fillWidth: true
                    visible: panel.page === "text"
                    spacing: Theme.space.md

                    TextArea {
                        id: content
                        objectName: "textContent"
                        Layout.fillWidth: true
                        label: qsTr("Text")
                        placeholderText: qsTr("Write here")
                        // Follows the clip unless the user is typing in it.
                        Binding on text {
                            value: panel.values["text.content"] ?? ""
                            when: !content.activeFocus
                        }
                        onTextChanged: if (activeFocus && text !== panel.values["text.content"]) panel.inspector.set("text.content", text)
                        onActiveFocusChanged: if (!activeFocus) panel.inspector.endGesture()
                    }
                    PropertySection {
                        inspector: panel.inspector
                        section: "text"
                        title: qsTr("Style")
                        applyToAllText: qsTr("Apply style to all texts")

                        ComboBox {
                            objectName: "fontFamily"
                            Layout.fillWidth: true
                            model: Qt.fontFamilies()
                            currentIndex: model.indexOf(panel.values["text.font"] ?? "")
                            displayText: panel.values["text.font"] ?? ""
                            Accessible.name: qsTr("Font")
                            onActivated: (index) => panel.inspector.set("text.font", model[index])
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            IconButton {
                                iconName: "format_bold"
                                label: qsTr("Bold")
                                checkable: true
                                checked: panel.values["text.bold"] ?? false
                                onClicked: panel.inspector.set("text.bold", checked)
                            }
                            IconButton {
                                iconName: "format_italic"
                                label: qsTr("Italic")
                                checkable: true
                                checked: panel.values["text.italic"] ?? false
                                onClicked: panel.inspector.set("text.italic", checked)
                            }
                            IconButton {
                                iconName: "format_underlined"
                                label: qsTr("Underline")
                                checkable: true
                                checked: panel.values["text.underline"] ?? false
                                onClicked: panel.inspector.set("text.underline", checked)
                            }
                            Item { Layout.fillWidth: true }
                            Repeater {
                                model: [{ icon: "format_align_left", text: qsTr("Align left") },
                                        { icon: "format_align_center", text: qsTr("Centre") },
                                        { icon: "format_align_right", text: qsTr("Align right") }]
                                delegate: IconButton {
                                    required property var modelData
                                    required property int index
                                    iconName: modelData.icon
                                    label: modelData.text
                                    checkable: true
                                    checked: (panel.values["text.align"] ?? 1) === index
                                    onClicked: panel.inspector.set("text.align", index)
                                }
                            }
                        }
                        PropertySlider {
                            inspector: panel.inspector
                            key: "text.size"
                            label: qsTr("Size")
                            from: 0.02
                            to: 0.3
                            neutral: 0.07
                            format: v => Math.round(v * 1000) / 10
                        }
                        Label { text: qsTr("Colour"); role: "bodyMedium" }
                        ColorSwatches {
                            Layout.fillWidth: true
                            inspector: panel.inspector
                            label: qsTr("Text colour")
                            current: panel.values["text.color"] ?? "white"
                            onPicked: (value) => panel.inspector.set("text.color", value)
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            Label { Layout.fillWidth: true; text: qsTr("Outline"); role: "bodyMedium" }
                            Switch {
                                objectName: "textStroke"
                                checked: panel.values["text.stroke"] ?? false
                                Accessible.name: qsTr("Outline")
                                onToggled: panel.inspector.set("text.stroke", checked)
                            }
                        }
                        ColorSwatches {
                            Layout.fillWidth: true
                            visible: panel.values["text.stroke"] ?? false
                            inspector: panel.inspector
                            label: qsTr("Outline colour")
                            current: panel.values["text.strokeColor"] ?? "black"
                            onPicked: (value) => panel.inspector.set("text.strokeColor", value)
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            Label { Layout.fillWidth: true; text: qsTr("Shadow"); role: "bodyMedium" }
                            Switch {
                                checked: panel.values["text.shadow"] ?? false
                                Accessible.name: qsTr("Shadow")
                                onToggled: panel.inspector.set("text.shadow", checked)
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            Label { Layout.fillWidth: true; text: qsTr("Background"); role: "bodyMedium" }
                            Switch {
                                checked: panel.values["text.background"] ?? false
                                Accessible.name: qsTr("Text background")
                                onToggled: panel.inspector.set("text.background", checked)
                            }
                        }
                        ColorSwatches {
                            Layout.fillWidth: true
                            visible: panel.values["text.background"] ?? false
                            inspector: panel.inspector
                            label: qsTr("Background colour")
                            current: panel.values["text.backgroundColor"] ?? "black"
                            onPicked: (value) => panel.inspector.set("text.backgroundColor", value)
                        }

                        advanced: [
                            PropertySlider {
                                visible: panel.values["text.stroke"] ?? false
                                inspector: panel.inspector
                                key: "text.strokeWidth"
                                label: qsTr("Outline thickness")
                                from: 0.02
                                to: 0.3
                                neutral: 0.08
                                format: v => Math.round(v * 100)
                            },
                            PropertySlider {
                                inspector: panel.inspector
                                key: "text.letterSpacing"
                                label: qsTr("Letter spacing")
                                from: -0.2
                                to: 1
                                format: v => Math.round(v * 100)
                            },
                            PropertySlider {
                                inspector: panel.inspector
                                key: "text.lineHeight"
                                label: qsTr("Line spacing")
                                from: 0.6
                                to: 3
                                neutral: 1.2
                            }
                        ]
                    }
                }

                // ---- Video: placement and background ---------------------------------------------------------
                ColumnLayout {
                    Layout.fillWidth: true
                    visible: panel.page === "video"
                    spacing: Theme.space.md

                    PropertySection {
                        inspector: panel.inspector
                        section: "video"
                        title: qsTr("Position and size")

                        // Keyframes: from one to the next, and how the movement goes from the one at the playhead.
                        ColumnLayout {
                            Layout.fillWidth: true
                            visible: panel.inspector.keyframes.length > 0
                            spacing: Theme.space.xs
                            RowLayout {
                                Layout.fillWidth: true
                                Label {
                                    Layout.fillWidth: true
                                    role: "bodyMedium"
                                    text: qsTr("%n keyframe(s)", "", panel.inspector.keyframes.length)
                                }
                                IconButton {
                                    objectName: "previousKeyframe"
                                    iconName: "chevron_left"
                                    label: qsTr("Previous keyframe")
                                    onClicked: panel.inspector.jumpKeyframe(-1)
                                }
                                IconButton {
                                    objectName: "nextKeyframe"
                                    iconName: "chevron_right"
                                    label: qsTr("Next keyframe")
                                    onClicked: panel.inspector.jumpKeyframe(1)
                                }
                            }
                            Label {
                                visible: (panel.values["kf.easing"] ?? "") !== ""
                                role: "bodySmall"
                                color: Theme.color.onSurfaceVariant
                                text: qsTr("Movement to the next keyframe")
                            }
                            Flow {
                                Layout.fillWidth: true
                                visible: (panel.values["kf.easing"] ?? "") !== ""
                                spacing: Theme.space.xs
                                Repeater {
                                    model: [{ id: "linear", text: qsTr("Steady") }, { id: "easeIn", text: qsTr("Speed up") },
                                            { id: "easeOut", text: qsTr("Slow down") }, { id: "easeInOut", text: qsTr("Smooth") },
                                            { id: "hold", text: qsTr("Jump") }]
                                    delegate: Chip {
                                        required property var modelData
                                        objectName: "easing_" + modelData.id
                                        text: modelData.text
                                        checkable: false
                                        checked: (panel.values["kf.easing"] ?? "") === modelData.id
                                        onClicked: panel.inspector.setKeyframeEasing(modelData.id)
                                    }
                                }
                            }
                        }

                        SegmentedButton {
                            Layout.fillWidth: true
                            visible: panel.inspector.kind === Inspector.Video || panel.inspector.kind === Inspector.Image
                            model: [{ text: qsTr("Whole picture") }, { text: qsTr("Fill") }]
                            currentIndex: panel.values["fit"] ?? 0
                            onActivated: (index) => panel.inspector.set("fit", index)
                        }
                        PropertySlider {
                            inspector: panel.inspector
                            key: "scale"
                            keyframeKey: "scale"
                            label: qsTr("Size")
                            from: 0.1
                            to: 5
                            neutral: 1
                            logarithmic: true
                            format: v => Math.round(v * 100) + " %"
                        }
                        PropertySlider {
                            inspector: panel.inspector
                            key: "x"
                            keyframeKey: "position"
                            label: qsTr("Horizontal position")
                            from: -1
                            to: 1
                            format: v => Math.round(v * panel.editor.canvasSize.width)
                        }
                        PropertySlider {
                            inspector: panel.inspector
                            key: "y"
                            label: qsTr("Vertical position")
                            from: -1
                            to: 1
                            format: v => Math.round(v * panel.editor.canvasSize.height)
                        }
                        PropertySlider {
                            inspector: panel.inspector
                            key: "rotation"
                            keyframeKey: "rotation"
                            label: qsTr("Rotation")
                            from: -180
                            to: 180
                            stepSize: 1
                            format: v => Math.round(v) + "°"
                        }
                        PropertySlider {
                            inspector: panel.inspector
                            key: "opacity"
                            keyframeKey: "opacity"
                            label: qsTr("Opacity")
                            neutral: 1
                            format: v => Math.round(v * 100) + " %"
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            Label { Layout.fillWidth: true; text: qsTr("Mirror"); role: "bodyMedium" }
                            IconButton {
                                iconName: "swap_horiz"
                                label: qsTr("Mirror left–right")
                                checkable: true
                                checked: panel.values["flipH"] ?? false
                                onClicked: panel.inspector.set("flipH", checked)
                            }
                            IconButton {
                                iconName: "swap_vert"
                                label: qsTr("Mirror top–bottom")
                                checkable: true
                                checked: panel.values["flipV"] ?? false
                                onClicked: panel.inspector.set("flipV", checked)
                            }
                        }
                    }

                    PropertySection {
                        visible: panel.sections.includes("background")
                        inspector: panel.inspector
                        section: "background"
                        title: qsTr("Background")
                        applyToAllText: qsTr("Use for the whole video")

                        Label {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            role: "bodySmall"
                            color: Theme.color.onSurfaceVariant
                            text: qsTr("Fills the canvas where the clip does not reach.")
                        }
                        SegmentedButton {
                            Layout.fillWidth: true
                            model: [{ text: qsTr("Colour") }, { text: qsTr("Blur") }]
                            currentIndex: panel.values["background.type"] ?? 0
                            onActivated: (index) => panel.inspector.set("background.type", index)
                        }
                        ColorSwatches {
                            Layout.fillWidth: true
                            visible: (panel.values["background.type"] ?? 0) === 0
                            inspector: panel.inspector
                            label: qsTr("Background colour")
                            current: panel.values["background.color"] ?? "black"
                            onPicked: (value) => panel.inspector.set("background.color", value)
                        }
                        PropertySlider {
                            visible: (panel.values["background.type"] ?? 0) === 1
                            inspector: panel.inspector
                            key: "background.blur"
                            label: qsTr("Blur")
                            neutral: 0.6
                            format: v => Math.round(v * 100)
                        }
                    }
                }

                // ---- Audio -----------------------------------------------------------------------------------
                ColumnLayout {
                    Layout.fillWidth: true
                    visible: panel.page === "audio"
                    spacing: Theme.space.md

                    PropertySection {
                        inspector: panel.inspector
                        section: "audio"
                        title: qsTr("Volume")

                        PropertySlider {
                            inspector: panel.inspector
                            key: "volume"
                            label: qsTr("Volume")
                            from: -60
                            to: 20
                            stepSize: 0.5
                            format: v => (v > 0 ? "+" : "") + v.toLocaleString(Qt.locale(), "f", 1) + " dB"
                        }
                        PropertySlider {
                            inspector: panel.inspector
                            key: "fadeIn"
                            label: qsTr("Fade in")
                            to: Math.max(0.1, Math.min(10, panel.inspector.durationSeconds / 2))
                            stepSize: 0.1
                            format: v => qsTr("%1 s").arg(v.toLocaleString(Qt.locale(), "f", 1))
                        }
                        PropertySlider {
                            inspector: panel.inspector
                            key: "fadeOut"
                            label: qsTr("Fade out")
                            to: Math.max(0.1, Math.min(10, panel.inspector.durationSeconds / 2))
                            stepSize: 0.1
                            format: v => qsTr("%1 s").arg(v.toLocaleString(Qt.locale(), "f", 1))
                        }
                    }
                    Button {
                        Layout.fillWidth: true
                        visible: panel.inspector.kind === Inspector.Audio
                        variant: "tonal"
                        iconName: "auto_fix_high"
                        text: qsTr("Auto enhance")
                        onClicked: panel.inspector.autoEnhance()
                    }
                }

                // ---- Speed -----------------------------------------------------------------------------------
                ColumnLayout {
                    Layout.fillWidth: true
                    visible: panel.page === "speed"
                    spacing: Theme.space.md

                    PropertySection {
                        inspector: panel.inspector
                        section: "speed"
                        title: qsTr("Speed")
                        applyToAll: false

                        PropertySlider {
                            inspector: panel.inspector
                            key: "speed"
                            label: qsTr("Speed")
                            from: 0.1
                            to: 100
                            neutral: 1
                            logarithmic: true
                            format: v => qsTr("%1×").arg((Math.round(v * 10) / 10).toLocaleString(Qt.locale()))
                        }
                        Flow {
                            Layout.fillWidth: true
                            spacing: Theme.space.xs
                            Repeater {
                                model: [0.25, 0.5, 1, 2, 4]
                                delegate: Chip {
                                    required property real modelData
                                    text: qsTr("%1×").arg(modelData.toLocaleString(Qt.locale()))
                                    checkable: false // shows the current speed; a click sets it
                                    checked: Math.abs((panel.values["speed"] ?? 1) - modelData) < 0.001
                                    onClicked: {
                                        panel.inspector.set("speed", modelData)
                                        panel.inspector.endGesture()
                                    }
                                }
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            Label { Layout.fillWidth: true; text: qsTr("Keep the voice natural"); role: "bodyMedium"; wrapMode: Text.WordWrap }
                            Switch {
                                checked: panel.values["preservePitch"] ?? true
                                Accessible.name: qsTr("Keep the voice natural")
                                onToggled: panel.inspector.set("preservePitch", checked)
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            Label { Layout.fillWidth: true; text: qsTr("Play backwards"); role: "bodyMedium" }
                            Switch {
                                objectName: "reverseSwitch"
                                checked: panel.values["reversed"] ?? false
                                Accessible.name: qsTr("Play backwards")
                                onToggled: panel.inspector.set("reversed", checked)
                            }
                        }
                        // The preview of a reversed clip needs a backwards copy, made in background.
                        ColumnLayout {
                            Layout.fillWidth: true
                            visible: panel.editor.player.preparingReverse
                            spacing: Theme.space.xs
                            Label {
                                text: qsTr("Preparing the backwards preview…")
                                role: "bodySmall"
                                color: Theme.color.onSurfaceVariant
                            }
                            ProgressBar {
                                Layout.fillWidth: true
                                value: panel.editor.player.reverseProgress
                            }
                        }
                    }
                }

                // ---- Animation: the entry, exit and loop animations and their length ------------------------------
                ColumnLayout {
                    Layout.fillWidth: true
                    visible: panel.page === "animation"
                    spacing: Theme.space.md

                    PropertySection {
                        inspector: panel.inspector
                        section: "animation"
                        title: qsTr("Animations")
                        applyToAll: false

                        Repeater {
                            model: [{ kind: "in", title: qsTr("Entry") }, { kind: "out", title: qsTr("Exit") },
                                    { kind: "loop", title: qsTr("Loop") }]
                            delegate: ColumnLayout {
                                id: animationRow
                                required property var modelData
                                readonly property string assetId: panel.values["animation." + modelData.kind] ?? ""
                                Layout.fillWidth: true
                                spacing: 0
                                RowLayout {
                                    Layout.fillWidth: true
                                    Label {
                                        Layout.fillWidth: true
                                        role: "bodyMedium"
                                        elide: Text.ElideRight
                                        text: animationRow.assetId !== ""
                                              ? qsTr("%1: %2").arg(animationRow.modelData.title).arg(panel.values["animation." + animationRow.modelData.kind + ".name"])
                                              : qsTr("%1: none").arg(animationRow.modelData.title)
                                        color: animationRow.assetId !== "" ? Theme.color.onSurface : Theme.color.onSurfaceVariant
                                    }
                                    IconButton {
                                        visible: animationRow.assetId !== ""
                                        iconName: "close"
                                        label: qsTr("Remove")
                                        onClicked: panel.inspector.toggleAnimation(animationRow.assetId)
                                    }
                                }
                                PropertySlider {
                                    visible: animationRow.assetId !== ""
                                    inspector: panel.inspector
                                    key: "animation." + animationRow.modelData.kind + ".duration"
                                    label: animationRow.modelData.kind === "loop" ? qsTr("One cycle") : qsTr("Length")
                                    from: 0.1
                                    to: Math.max(0.2, Math.min(5, panel.inspector.durationSeconds))
                                    neutral: 0.5
                                    stepSize: 0.1
                                    format: v => qsTr("%1 s").arg(v.toLocaleString(Qt.locale(), "f", 1))
                                }
                            }
                        }
                        Button {
                            Layout.fillWidth: true
                            variant: "tonal"
                            iconName: "animation"
                            text: qsTr("Choose an animation")
                            onClicked: panel.editor.libraryRequested("animations")
                        }
                    }
                }

                // ---- Adjust: auto enhance, filter, adjustments ----------------------------------------------
                ColumnLayout {
                    Layout.fillWidth: true
                    visible: panel.page === "adjust"
                    spacing: Theme.space.md

                    Button {
                        objectName: "autoEnhanceButton"
                        Layout.fillWidth: true
                        variant: "tonal"
                        iconName: "auto_fix_high"
                        text: qsTr("Auto enhance")
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("Corrects light, colour and volume: you can change the result below")
                        onClicked: panel.inspector.autoEnhance()
                    }

                    PropertySection {
                        inspector: panel.inspector
                        section: "filter"
                        title: (panel.values["filter"] ?? "") !== "" ? qsTr("Filter: %1").arg(panel.values["filter.name"]) : qsTr("Filter")
                        applyToAll: (panel.values["filter"] ?? "") !== ""

                        Label {
                            Layout.fillWidth: true
                            visible: (panel.values["filter"] ?? "") === ""
                            wrapMode: Text.WordWrap
                            role: "bodySmall"
                            color: Theme.color.onSurfaceVariant
                            text: qsTr("Choose a filter in the Filters library on the left: hover to preview, click to apply.")
                        }
                        PropertySlider {
                            visible: (panel.values["filter"] ?? "") !== ""
                            inspector: panel.inspector
                            key: "filter.intensity"
                            label: qsTr("Intensity")
                            neutral: 1
                            format: v => Math.round(v * 100)
                        }
                    }

                    PropertySection {
                        id: adjustSection
                        inspector: panel.inspector
                        section: "adjust"
                        title: qsTr("Adjust")

                        Repeater {
                            model: panel.inspector.adjustParams.filter(p => !p.advanced)
                            delegate: PropertySlider {
                                required property var modelData
                                inspector: panel.inspector
                                key: "adjust." + modelData.name
                                label: modelData.label
                                from: modelData.min
                                to: modelData.max
                                neutral: modelData["default"]
                                format: v => Math.round(v * 100)
                            }
                        }
                        advanced: Repeater {
                            model: panel.inspector.adjustParams.filter(p => p.advanced)
                            delegate: PropertySlider {
                                required property var modelData
                                inspector: panel.inspector
                                key: "adjust." + modelData.name
                                label: modelData.label
                                from: modelData.min
                                to: modelData.max
                                neutral: modelData["default"]
                                format: v => Math.round(v * 100)
                            }
                        }
                    }
                }

                Item { implicitHeight: Theme.space.md }
            }
        }
    }
}
