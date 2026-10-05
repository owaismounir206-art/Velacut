// Properties of the selected clip, on the right (SPEC §4): tabs Text · Video · Audio · Speed · Adjust, the
// essential controls first, "Advanced" closed, "Reset" and "Apply to all" in every section (SPEC 0bis rules 5–8).
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import Vedit.Components
import Vedit.Theme
import Vedit.UI

Rectangle {
    id: panel

    required property Editor editor
    readonly property Inspector inspector: editor.inspector

    FileDialog {
        id: lutDialog
        title: qsTr("Choose 3D LUT (.cube)")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("Cube LUT (*.cube)"), qsTr("All files (*)")]
        onAccepted: {
            if (selectedFile) {
                let path = selectedFile.toString()
                if (path.startsWith("file://")) {
                    path = path.substring(7)
                }
                panel.inspector.set("lut.path", path)
            }
        }
    }
    readonly property var values: inspector.values
    readonly property var sections: inspector.sections

    // The tabs the selected clip has, each with the page it shows.
    readonly property var tabs: {
        const list = []
        if (sections.includes("transition"))
            list.push({ text: qsTr("Transition"), page: "transition" })
        if (sections.includes("caption"))
            list.push({ text: qsTr("Captions"), page: "caption" })
        if (sections.includes("text"))
            list.push({ text: qsTr("Text"), page: "text" })
        if (sections.includes("sticker"))
            list.push({ text: values["sticker.visualizer"] ? qsTr("Visualizer")
                            : values["sticker.graphic"] ? qsTr("Element") : qsTr("Sticker"), page: "sticker" })
        if (sections.includes("video"))
            list.push({ text: inspector.kind === Inspector.Text || inspector.kind === Inspector.Sticker ? qsTr("Position")
                                                                                                       : qsTr("Video"), page: "video" })
        if (sections.includes("audio"))
            list.push({ text: qsTr("Audio"), page: "audio" })
        if (sections.includes("speed"))
            list.push({ text: qsTr("Speed"), page: "speed" })
        if (sections.includes("animation"))
            list.push({ text: qsTr("Animation"), page: "animation" })
        if (sections.includes("cutout"))
            list.push({ text: qsTr("Cutout"), page: "cutout" })
        if (sections.includes("effects") && (values["effects"] ?? []).length > 0)
            list.push({ text: qsTr("Effects"), page: "effects" })
        if (sections.includes("adjust"))
            list.push({ text: qsTr("Adjust"), page: "adjust" })
        return list
    }
    // The page chosen by the user, kept while the selection changes when the new clip has it too.
    // It applies to clips of the kind it was chosen for (a text or a caption opens on its words, a video on Video).
    property string wantedPage: "video"
    property int wantedKind: Inspector.Video
    readonly property string page: {
        if (inspector.kind === wantedKind)
            for (const tab of tabs)
                if (tab.page === wantedPage)
                    return wantedPage
        return tabs.length > 0 ? tabs[0].page : ""
    }
    function showPage(name) {
        wantedPage = name
        wantedKind = inspector.kind
    }

    // On the Cutout page with a mask, gestures on the player move and resize the mask (not the clip).
    readonly property bool maskEditing: page === "cutout" && (values["mask.shape"] ?? -1) >= 0
    onMaskEditingChanged: if (inspector.canvasMode !== "pick") inspector.canvasMode = maskEditing ? "mask" : ""

    color: "transparent" // the properties panel's surface

    // Nothing selected: the project itself (as desktop editors show the draft's details), its format one click away
    // (SPEC 0bis rule 1), and how to get to the properties of a clip.
    ColumnLayout {
        objectName: "projectDetails"
        anchors.fill: parent
        anchors.margins: Theme.space.md
        anchors.topMargin: 0
        visible: !panel.inspector.active
        spacing: Theme.space.sm

        Item {
            Layout.fillWidth: true
            implicitHeight: Theme.editor.panelHeaderHeight
            Label {
                anchors.verticalCenter: parent.verticalCenter
                role: "titleSmall"
                text: qsTr("Project")
            }
        }
        Repeater {
            model: [{ label: qsTr("Name"), value: panel.editor.name },
                    { label: qsTr("Size"), value: qsTr("%1 × %2").arg(panel.editor.canvasSize.width).arg(panel.editor.canvasSize.height) },
                    { label: qsTr("Frame rate"), value: qsTr("%1 fps").arg(Math.round(panel.editor.frameRate * 100) / 100) },
                    { label: qsTr("Duration"), value: panel.editor.player.timecode(panel.editor.timeline.duration) }]
            delegate: RowLayout {
                id: detail
                required property var modelData
                Layout.fillWidth: true
                spacing: Theme.space.md
                Label {
                    Layout.preferredWidth: Theme.editor.valueWidth * 1.8
                    role: "bodyMedium"
                    color: Theme.color.onSurfaceVariant
                    text: detail.modelData.label
                }
                Label {
                    Layout.fillWidth: true
                    role: "bodyMedium"
                    elide: Text.ElideRight
                    font.features: { "tnum": 1 }
                    text: detail.modelData.value
                }
            }
        }
        Label {
            Layout.topMargin: Theme.space.sm
            role: "labelLarge"
            text: qsTr("Format")
        }
        Flow {
            Layout.fillWidth: true
            spacing: Theme.space.xs
            Repeater {
                // Values of CanvasPreset (core/project/Sequence.h).
                model: [{ text: "16:9", preset: 0 }, { text: "9:16", preset: 1 }, { text: "1:1", preset: 2 },
                        { text: "4:5", preset: 3 }, { text: "3:4", preset: 5 }, { text: "21:9", preset: 4 }]
                delegate: Chip {
                    required property var modelData
                    objectName: "projectFormat_" + modelData.preset
                    text: modelData.text
                    checkable: false
                    checked: panel.editor.canvasPreset === modelData.preset
                    onClicked: panel.editor.setCanvasPreset(modelData.preset)
                }
            }
        }
        Item { Layout.fillHeight: true }
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.space.sm
            Icon {
                name: "touch_app"
                size: Theme.editor.toolIconSize
                color: Theme.color.onSurfaceVariant
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                role: "bodySmall"
                color: Theme.color.onSurfaceVariant
                text: qsTr("Select a clip on the timeline to change it here.")
            }
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

                // ---- Caption line --------------------------------------------------------------------------
                ColumnLayout {
                    id: captionPage
                    readonly property Captions captions: panel.editor.captions
                    readonly property var style: captions.style
                    readonly property int line: captions.lines.findIndex(l => l.clipId === panel.inspector.clipId)

                    component StyleSlider: ColumnLayout {
                        id: styleSlider
                        required property string key
                        property string label
                        property real from: 0
                        property real to: 1
                        property real stepSize: 0
                        property var format: v => Math.round(v * 100) + " %"
                        Layout.fillWidth: true
                        spacing: 0
                        RowLayout {
                            Layout.fillWidth: true
                            Label { Layout.fillWidth: true; text: styleSlider.label; role: "bodyMedium" }
                            Label {
                                role: "labelMedium"
                                font.features: { "tnum": 1 }
                                color: Theme.color.onSurfaceVariant
                                text: styleSlider.format(slider.value)
                            }
                        }
                        Slider {
                            id: slider
                            objectName: "captionStyle_" + styleSlider.key
                            Layout.fillWidth: true
                            from: styleSlider.from
                            to: styleSlider.to
                            stepSize: styleSlider.stepSize
                            value: captionPage.style[styleSlider.key] ?? 0
                            Accessible.name: styleSlider.label
                            onMoved: captionPage.captions.setStyleValue(styleSlider.key, value)
                            onPressedChanged: if (!pressed) captionPage.captions.endGesture()
                        }
                    }
                    component StyleSwitch: RowLayout {
                        id: styleSwitch
                        required property string key
                        property string label
                        Layout.fillWidth: true
                        Label { Layout.fillWidth: true; text: styleSwitch.label; role: "bodyMedium" }
                        Switch {
                            objectName: "captionStyle_" + styleSwitch.key
                            checked: captionPage.style[styleSwitch.key] ?? false
                            Accessible.name: styleSwitch.label
                            onToggled: captionPage.captions.setStyleValue(styleSwitch.key, checked)
                        }
                    }

                    Layout.fillWidth: true
                    visible: panel.page === "caption"
                    spacing: Theme.space.md

                    TextArea {
                        id: captionText
                        objectName: "captionText"
                        Layout.fillWidth: true
                        label: qsTr("Line")
                        placeholderText: qsTr("Write here")
                        enabled: !captionPage.captions.locked
                        Binding on text {
                            value: captionPage.line >= 0 ? captionPage.captions.lines[captionPage.line].text : ""
                            when: !captionText.activeFocus
                        }
                        onActiveFocusChanged: if (!activeFocus && captionPage.line >= 0) captionPage.captions.setLineText(captionPage.line, text)
                        Keys.onReturnPressed: (event) => {
                            if (event.modifiers & Qt.ShiftModifier) {
                                event.accepted = false
                                return
                            }
                            captionPage.captions.setLineText(captionPage.line, text)
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.space.xs
                        enabled: !captionPage.captions.locked
                        Button {
                            objectName: "captionSplitLine"
                            variant: "tonal"
                            iconName: "call_split"
                            text: qsTr("Split")
                            onClicked: captionPage.captions.splitLine(captionPage.line)
                        }
                        Button {
                            objectName: "captionJoinLine"
                            variant: "tonal"
                            iconName: "merge"
                            enabled: captionPage.line >= 0 && captionPage.line < captionPage.captions.lines.length - 1
                            text: qsTr("Join with next")
                            onClicked: captionPage.captions.joinWithNext(captionPage.line)
                        }
                    }

                    Label {
                        Layout.fillWidth: true
                        role: "titleSmall"
                        text: qsTr("Look of all the captions")
                    }
                    Button {
                        objectName: "captionChooseStyle"
                        Layout.fillWidth: true
                        variant: "outlined"
                        iconName: "format_color_text"
                        text: qsTr("Choose a style")
                        onClicked: panel.editor.libraryRequested("captions")
                    }
                    StyleSlider {
                        key: "wordsPerLine"
                        label: qsTr("Words at a time")
                        from: 0
                        to: 8
                        stepSize: 1
                        format: v => v === 0 ? qsTr("Whole line") : Math.round(v)
                    }
                    StyleSlider {
                        key: "position"
                        label: qsTr("Height")
                        from: 0.45
                        to: -0.45
                        format: v => Math.round((0.5 - v) * 100) + " %"
                    }
                    StyleSlider {
                        key: "size"
                        label: qsTr("Size")
                        from: 0.03
                        to: 0.12
                        format: v => Math.round(v * 1000) / 10
                    }
                    Label { text: qsTr("Word being said"); role: "bodyMedium" }
                    ComboBox {
                        objectName: "captionHighlight"
                        Layout.fillWidth: true
                        model: [qsTr("Not highlighted"), qsTr("Coloured"), qsTr("Bigger"), qsTr("In a box"), qsTr("Karaoke fill")]
                        currentIndex: captionPage.style.highlight ?? 0
                        displayText: model[currentIndex] ?? ""
                        Accessible.name: qsTr("Word being said")
                        onActivated: (index) => captionPage.captions.setStyleValue("highlight", index)
                    }
                    ColorSwatches {
                        Layout.fillWidth: true
                        visible: (captionPage.style.highlight ?? 0) !== 0
                        inspector: panel.inspector
                        label: qsTr("Highlight colour")
                        current: captionPage.style.highlightColor ?? "yellow"
                        onPicked: (value) => captionPage.captions.setStyleValue("highlightColor", value)
                    }
                    Label { text: qsTr("Entrance"); role: "bodyMedium" }
                    ComboBox {
                        objectName: "captionAnimation"
                        Layout.fillWidth: true
                        model: [qsTr("None"), qsTr("Pop"), qsTr("Fade"), qsTr("Bounce")]
                        currentIndex: captionPage.style.animation ?? 0
                        displayText: model[currentIndex] ?? ""
                        Accessible.name: qsTr("Entrance")
                        onActivated: (index) => captionPage.captions.setStyleValue("animation", index)
                    }
                    Label { text: qsTr("Colour"); role: "bodyMedium" }
                    ColorSwatches {
                        Layout.fillWidth: true
                        inspector: panel.inspector
                        label: qsTr("Text colour")
                        current: captionPage.style.color ?? "white"
                        onPicked: (value) => captionPage.captions.setStyleValue("color", value)
                    }
                    StyleSwitch { key: "bold"; label: qsTr("Bold") }
                    StyleSwitch { key: "uppercase"; label: qsTr("Capital letters") }
                    StyleSwitch { key: "stroke"; label: qsTr("Outline") }
                    StyleSwitch { key: "background"; label: qsTr("Background") }
                    ColorSwatches {
                        Layout.fillWidth: true
                        visible: captionPage.style.background ?? false
                        inspector: panel.inspector
                        label: qsTr("Background colour")
                        current: captionPage.style.backgroundColor ?? "black"
                        onPicked: (value) => captionPage.captions.setStyleValue("backgroundColor", value)
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
                        Label {
                            visible: panel.values["text.background"] ?? false
                            text: qsTr("Shape")
                            role: "bodyMedium"
                        }
                        ComboBox {
                            visible: panel.values["text.background"] ?? false
                            Layout.fillWidth: true
                            model: [
                                qsTr("Rectangle"),
                                qsTr("Speech (Round)"),
                                qsTr("Speech (Square)"),
                                qsTr("Thought Cloud"),
                                qsTr("Comic Shout"),
                                qsTr("Callout HUD"),
                                qsTr("Lower Third Bar"),
                                qsTr("Lower Third Two-Tone"),
                                qsTr("Badge / Pill")
                            ]
                            currentIndex: panel.values["text.bubbleShape"] ?? 0
                            displayText: model[currentIndex] ?? ""
                            Accessible.name: qsTr("Bubble shape")
                            onActivated: (index) => panel.inspector.set("text.bubbleShape", index)
                        }
                        Label {
                            visible: (panel.values["text.background"] ?? false) && (panel.values["text.bubbleShape"] >= 1 && panel.values["text.bubbleShape"] <= 3)
                            text: qsTr("Tail position")
                            role: "bodyMedium"
                        }
                        ComboBox {
                            visible: (panel.values["text.background"] ?? false) && (panel.values["text.bubbleShape"] >= 1 && panel.values["text.bubbleShape"] <= 3)
                            Layout.fillWidth: true
                            model: [
                                qsTr("None"),
                                qsTr("Bottom Left"),
                                qsTr("Bottom Centre"),
                                qsTr("Bottom Right"),
                                qsTr("Top Left"),
                                qsTr("Top Right"),
                                qsTr("Left"),
                                qsTr("Right")
                            ]
                            currentIndex: panel.values["text.bubbleTail"] ?? 0
                            displayText: model[currentIndex] ?? ""
                            Accessible.name: qsTr("Tail position")
                            onActivated: (index) => panel.inspector.set("text.bubbleTail", index)
                        }
                        Label {
                            visible: (panel.values["text.background"] ?? false) && (panel.values["text.bubbleShape"] >= 5 && panel.values["text.bubbleShape"] <= 7)
                            text: qsTr("Accent colour")
                            role: "bodyMedium"
                        }
                        ColorSwatches {
                            Layout.fillWidth: true
                            visible: (panel.values["text.background"] ?? false) && (panel.values["text.bubbleShape"] >= 5 && panel.values["text.bubbleShape"] <= 7)
                            inspector: panel.inspector
                            label: qsTr("Accent colour")
                            current: panel.values["text.accentColor"] ?? "#ffb400"
                            onPicked: (value) => panel.inspector.set("text.accentColor", value)
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
                                visible: (panel.values["text.background"] ?? false) && (panel.values["text.bubbleShape"] >= 1 && panel.values["text.bubbleShape"] <= 3)
                                inspector: panel.inspector
                                key: "text.bubbleTailSize"
                                label: qsTr("Tail size")
                                from: 0.1
                                to: 1.0
                                neutral: 0.4
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

                    PropertySection {
                        inspector: panel.inspector
                        section: "textAnimation"
                        title: qsTr("Text animation")
                        applyToAllText: qsTr("Apply animation to all texts")

                        RowLayout {
                            Layout.fillWidth: true
                            Label { Layout.fillWidth: true; text: qsTr("Enable animation"); role: "bodyMedium" }
                            Switch {
                                objectName: "textAnimationEnabled"
                                checked: panel.values["text.animation.enabled"] ?? false
                                Accessible.name: qsTr("Enable animation")
                                onToggled: panel.inspector.set("text.animation.enabled", checked)
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            visible: panel.values["text.animation.enabled"] ?? false
                            spacing: Theme.space.sm

                            Label { text: qsTr("Type"); role: "bodyMedium" }
                            ComboBox {
                                objectName: "textAnimationType"
                                Layout.fillWidth: true
                                model: [
                                    qsTr("None"),
                                    qsTr("Typewriter"),
                                    qsTr("Fade in"),
                                    qsTr("Slide up"),
                                    qsTr("Slide down"),
                                    qsTr("Bounce"),
                                    qsTr("Pop in"),
                                    qsTr("Wave"),
                                    qsTr("Glitch"),
                                    qsTr("Blur")
                                ]
                                currentIndex: panel.values["text.animation.type"] ?? 1
                                displayText: model[currentIndex] ?? ""
                                Accessible.name: qsTr("Animation type")
                                onActivated: (index) => panel.inspector.set("text.animation.type", index)
                            }

                            Label { text: qsTr("Scope"); role: "bodyMedium" }
                            ComboBox {
                                objectName: "textAnimationScope"
                                Layout.fillWidth: true
                                model: [
                                    qsTr("Character"),
                                    qsTr("Word"),
                                    qsTr("Line"),
                                    qsTr("All")
                                ]
                                currentIndex: panel.values["text.animation.scope"] ?? 0
                                displayText: model[currentIndex] ?? ""
                                Accessible.name: qsTr("Animation scope")
                                onActivated: (index) => panel.inspector.set("text.animation.scope", index)
                            }

                            PropertySlider {
                                inspector: panel.inspector
                                key: "text.animation.duration"
                                label: qsTr("Duration")
                                from: 0.2
                                to: 10.0
                                neutral: 1.5
                                format: v => (Math.round(v * 10) / 10).toFixed(1) + " s"
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                visible: (panel.values["text.animation.type"] ?? 1) === 1
                                Label { Layout.fillWidth: true; text: qsTr("Blinking cursor"); role: "bodyMedium" }
                                Switch {
                                    checked: panel.values["text.animation.cursor"] ?? true
                                    Accessible.name: qsTr("Blinking cursor")
                                    onToggled: panel.inspector.set("text.animation.cursor", checked)
                                }
                            }
                        }
                    }
                }

                // ---- Video: placement and background ---------------------------------------------------------
                ColumnLayout {
                    Layout.fillWidth: true
                    visible: panel.page === "video"
                    spacing: Theme.space.md

                    // Stabilize (SPEC 0bis rule 9): one click measures the shake, then how steady is a slider.
                    ColumnLayout {
                        Layout.fillWidth: true
                        visible: panel.inspector.kind === Inspector.Video
                        spacing: Theme.space.xs
                        Button {
                            objectName: "stabilizeButton"
                            Layout.fillWidth: true
                            visible: !(panel.values["stabilize.on"] ?? false)
                            enabled: !panel.editor.ai.busy
                            variant: "tonal"
                            iconName: "vibration"
                            text: qsTr("Stabilize")
                            onClicked: panel.editor.ai.stabilize()
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            visible: panel.values["stabilize.on"] ?? false
                            PropertySlider {
                                Layout.fillWidth: true
                                inspector: panel.inspector
                                key: "stabilize.strength"
                                label: qsTr("Steadiness")
                                from: 0
                                to: 1
                                neutral: 0.6
                            }
                            IconButton {
                                objectName: "stabilizeRemove"
                                iconName: "close"
                                label: qsTr("Remove stabilization")
                                onClicked: panel.inspector.set("stabilize.on", false)
                            }
                        }
                    }

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
                                visible: panel.values["kf.here"] ?? false
                                role: "bodySmall"
                                color: Theme.color.onSurfaceVariant
                                text: qsTr("Movement to the next keyframe")
                            }
                            Flow {
                                Layout.fillWidth: true
                                visible: panel.values["kf.here"] ?? false
                                spacing: Theme.space.xs
                                Repeater {
                                    model: [{ id: "linear", text: qsTr("Steady") }, { id: "easeIn", text: qsTr("Speed up") },
                                            { id: "easeOut", text: qsTr("Slow down") }, { id: "easeInOut", text: qsTr("Smooth") },
                                            { id: "hold", text: qsTr("Jump") }, { id: "custom", text: qsTr("Custom") }]
                                    delegate: Chip {
                                        required property var modelData
                                        objectName: "easing_" + modelData.id
                                        text: modelData.text
                                        checkable: false
                                        checked: (panel.values["kf.easing"] ?? "") === modelData.id
                                        onClicked: {
                                            if (modelData.id === "custom") {
                                                // Starts from the curve shown, then drag its points.
                                                const c = panel.values["kf.curve"] ?? [0, 0, 1, 1]
                                                panel.inspector.setKeyframeCurve(c[0], c[1], c[2], c[3])
                                                panel.inspector.endGesture()
                                            } else {
                                                panel.inspector.setKeyframeEasing(modelData.id)
                                            }
                                        }
                                    }
                                }
                            }
                            CurveEditor {
                                Layout.fillWidth: true
                                visible: (panel.values["kf.here"] ?? false) && panel.values["kf.easing"] !== "hold"
                                inspector: panel.inspector
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
                            visible: panel.inspector.kind !== Inspector.Text
                            Label { Layout.fillWidth: true; text: qsTr("Blend"); role: "bodyMedium" }
                            ComboBox {
                                objectName: "blendMode"
                                // In the order of BlendMode (core/project/Clip.h).
                                model: [qsTr("Normal"), qsTr("Lighten"), qsTr("Screen"), qsTr("Multiply"), qsTr("Overlay"),
                                        qsTr("Soft light"), qsTr("Hard light"), qsTr("Difference"), qsTr("Darken"),
                                        qsTr("Colour"), qsTr("Luminosity"), qsTr("Add"), qsTr("Colour dodge"),
                                        qsTr("Colour burn"), qsTr("Exclusion"), qsTr("Hue"), qsTr("Saturation")]
                                currentIndex: panel.values["blend"] ?? 0
                                Accessible.name: qsTr("Blend mode")
                                onActivated: (index) => panel.inspector.set("blend", index)
                            }
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

                // ---- Sticker ---------------------------------------------------------------------------------
                ColumnLayout {
                    Layout.fillWidth: true
                    visible: panel.page === "sticker"
                    spacing: Theme.space.md

                    PropertySection {
                        id: stickerSection
                        inspector: panel.inspector
                        section: "sticker"
                        title: panel.values["sticker.visualizer"] ? qsTr("Visualizer")
                             : panel.values["sticker.graphic"] ? qsTr("Element") : qsTr("Colour")
                        readonly property bool graphic: panel.values["sticker.graphic"] ?? false
                        readonly property int graphicKind: panel.values["sticker.graphicKind"] ?? -1
                        readonly property bool counter: graphic && graphicKind === 0
                        readonly property bool drawn: graphic && graphicKind >= 4

                        // Graphic element: numbers, colours, line, drawing time.
                        RowLayout {
                            Layout.fillWidth: true
                            visible: stickerSection.counter
                            spacing: Theme.space.sm
                            TextField {
                                objectName: "graphicFrom"
                                Layout.fillWidth: true
                                Layout.preferredWidth: 1 // equal halves
                                label: qsTr("From")
                                text: (panel.values["sticker.from"] ?? 0).toString()
                                inputMethodHints: Qt.ImhFormattedNumbersOnly
                                onEditingFinished: panel.inspector.set("sticker.from", Number(text.replace(",", ".")))
                            }
                            TextField {
                                objectName: "graphicTo"
                                Layout.fillWidth: true
                                Layout.preferredWidth: 1 // equal halves
                                label: qsTr("To")
                                text: (panel.values["sticker.to"] ?? 100).toString()
                                inputMethodHints: Qt.ImhFormattedNumbersOnly
                                onEditingFinished: panel.inspector.set("sticker.to", Number(text.replace(",", ".")))
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            visible: stickerSection.counter
                            spacing: Theme.space.sm
                            TextField {
                                Layout.fillWidth: true
                                Layout.preferredWidth: 1 // equal halves
                                label: qsTr("Before")
                                text: panel.values["sticker.prefix"] ?? ""
                                onEditingFinished: panel.inspector.set("sticker.prefix", text)
                            }
                            TextField {
                                Layout.fillWidth: true
                                Layout.preferredWidth: 1 // equal halves
                                label: qsTr("After")
                                text: panel.values["sticker.suffix"] ?? ""
                                onEditingFinished: panel.inspector.set("sticker.suffix", text)
                            }
                        }
                        PropertySlider {
                            visible: stickerSection.counter
                            inspector: panel.inspector
                            key: "sticker.decimals"
                            label: qsTr("Decimals")
                            to: 4
                            stepSize: 1
                            format: v => Math.round(v).toLocaleString(Qt.locale(), "f", 0)
                        }
                        ColorSwatches {
                            Layout.fillWidth: true
                            visible: stickerSection.graphic
                            inspector: panel.inspector
                            label: qsTr("Colour")
                            current: panel.values["sticker.color"] ?? "white"
                            onPicked: (value) => panel.inspector.set("sticker.color", value)
                        }
                        ColorSwatches {
                            Layout.fillWidth: true
                            visible: stickerSection.graphic && !stickerSection.drawn
                            inspector: panel.inspector
                            label: stickerSection.graphicKind === 3 ? qsTr("Track colour") : qsTr("Outline colour")
                            current: panel.values["sticker.color2"] ?? "black"
                            onPicked: (value) => panel.inspector.set("sticker.color2", value)
                        }
                        PropertySlider {
                            visible: stickerSection.graphic
                            inspector: panel.inspector
                            key: "sticker.thickness"
                            label: stickerSection.drawn || stickerSection.graphicKind === 3 ? qsTr("Thickness") : qsTr("Size")
                            neutral: 0.5
                            format: v => Math.round(v * 100) + " %"
                        }
                        PropertySlider {
                            visible: stickerSection.drawn
                            inspector: panel.inspector
                            key: "sticker.drawSeconds"
                            label: qsTr("Drawing time")
                            from: 0.1
                            to: 3
                            neutral: 0.6
                            stepSize: 0.1
                            format: v => qsTr("%1 s").arg(v.toLocaleString(Qt.locale(), "f", 1))
                        }

                        // Visualizer: the shape, its colours and how it reacts to the music.
                        SegmentedButton {
                            objectName: "visualizerStyle"
                            Layout.fillWidth: true
                            visible: panel.values["sticker.visualizer"] ?? false
                            model: [{ text: qsTr("Bars") }, { text: qsTr("Spectrum") }, { text: qsTr("Wave") },
                                    { text: qsTr("Circle") }]
                            currentIndex: panel.values["sticker.style"] ?? 0
                            onActivated: (index) => panel.inspector.set("sticker.style", index)
                        }
                        ColorSwatches {
                            Layout.fillWidth: true
                            visible: panel.values["sticker.visualizer"] ?? false
                            inspector: panel.inspector
                            label: qsTr("Main colour")
                            current: panel.values["sticker.primaryColor"] ?? "white"
                            onPicked: (value) => panel.inspector.set("sticker.primaryColor", value)
                        }
                        ColorSwatches {
                            Layout.fillWidth: true
                            visible: panel.values["sticker.visualizer"] ?? false
                            inspector: panel.inspector
                            label: qsTr("Second colour")
                            current: panel.values["sticker.secondaryColor"] ?? "white"
                            onPicked: (value) => panel.inspector.set("sticker.secondaryColor", value)
                        }
                        PropertySlider {
                            visible: panel.values["sticker.visualizer"] ?? false
                            inspector: panel.inspector
                            key: "sticker.sensitivity"
                            label: qsTr("Sensitivity")
                            from: 0.2
                            to: 4
                            neutral: 1
                            stepSize: 0.1
                            format: v => "×" + v.toLocaleString(Qt.locale(), "f", 1)
                        }
                        PropertySlider {
                            visible: panel.values["sticker.visualizer"] ?? false
                            inspector: panel.inspector
                            key: "sticker.smoothing"
                            label: qsTr("Smoothness")
                            neutral: 0.5
                            format: v => Math.round(v * 100) + " %"
                        }
                        PropertySlider {
                            visible: panel.values["sticker.visualizer"] ?? false
                            inspector: panel.inspector
                            key: "sticker.barCount"
                            label: qsTr("Bars")
                            from: 8
                            to: 96
                            neutral: 32
                            stepSize: 1
                            format: v => Math.round(v).toLocaleString(Qt.locale(), "f", 0)
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            visible: panel.values["sticker.visualizer"] ?? false
                            Label { Layout.fillWidth: true; text: qsTr("Mirrored"); role: "bodyMedium" }
                            Switch {
                                checked: panel.values["sticker.mirror"] ?? false
                                Accessible.name: qsTr("Mirrored")
                                onToggled: panel.inspector.set("sticker.mirror", checked)
                            }
                        }

                        // Picture: an optional colour over it, and the speed of an animated one.
                        RowLayout {
                            Layout.fillWidth: true
                            visible: !(panel.values["sticker.visualizer"] ?? false) && !stickerSection.graphic
                            Label { Layout.fillWidth: true; text: qsTr("Recolour"); role: "bodyMedium" }
                            Switch {
                                objectName: "stickerTinted"
                                checked: panel.values["sticker.tinted"] ?? false
                                Accessible.name: qsTr("Recolour")
                                onToggled: panel.inspector.set("sticker.tinted", checked)
                            }
                        }
                        ColorSwatches {
                            Layout.fillWidth: true
                            visible: !(panel.values["sticker.visualizer"] ?? false) && !stickerSection.graphic &&
                                     (panel.values["sticker.tinted"] ?? false)
                            inspector: panel.inspector
                            label: qsTr("Colour")
                            current: panel.values["sticker.tint"] ?? "white"
                            onPicked: (value) => panel.inspector.set("sticker.tint", value)
                        }
                        PropertySlider {
                            visible: panel.values["sticker.animated"] ?? false
                            inspector: panel.inspector
                            key: "sticker.speed"
                            label: qsTr("Animation speed")
                            from: 0.25
                            to: 4
                            neutral: 1
                            logarithmic: true
                            format: v => "×" + v.toLocaleString(Qt.locale(), "f", 2)
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            visible: panel.values["sticker.animated"] ?? false
                            Label { Layout.fillWidth: true; text: qsTr("Repeat"); role: "bodyMedium" }
                            Switch {
                                checked: panel.values["sticker.loop"] ?? true
                                Accessible.name: qsTr("Repeat the animation")
                                onToggled: panel.inspector.set("sticker.loop", checked)
                            }
                        }
                    }
                }

                // ---- Effects ---------------------------------------------------------------------------------
                ColumnLayout {
                    Layout.fillWidth: true
                    visible: panel.page === "effects"
                    spacing: Theme.space.lg

                    Repeater {
                        model: panel.values["effects"] ?? []
                        delegate: ColumnLayout {
                            id: effectItem
                            required property var modelData
                            Layout.fillWidth: true
                            spacing: Theme.space.xs

                            RowLayout {
                                Layout.fillWidth: true
                                Label {
                                    Layout.fillWidth: true
                                    text: effectItem.modelData.name
                                    role: "titleSmall"
                                    elide: Text.ElideRight
                                }
                                IconButton {
                                    objectName: "removeEffect_" + effectItem.modelData.index
                                    iconName: "delete"
                                    label: qsTr("Remove the effect")
                                    onClicked: panel.inspector.removeEffectAt(effectItem.modelData.index)
                                }
                            }
                            EffectSlider {
                                inspector: panel.inspector
                                index: effectItem.modelData.index
                                name: "mix"
                                label: qsTr("Strength")
                                value: effectItem.modelData.mix
                                neutral: 1
                            }
                            Repeater {
                                model: effectItem.modelData.controls
                                delegate: Loader {
                                    id: control
                                    required property var modelData
                                    Layout.fillWidth: true
                                    sourceComponent: control.modelData.color ? colorControl : sliderControl
                                    Component {
                                        id: sliderControl
                                        EffectSlider {
                                            inspector: panel.inspector
                                            index: effectItem.modelData.index
                                            name: control.modelData.name
                                            label: control.modelData.label
                                            value: control.modelData.value
                                            from: control.modelData.from
                                            to: control.modelData.to
                                            neutral: control.modelData.neutral
                                        }
                                    }
                                    Component {
                                        id: colorControl
                                        ColorSwatches {
                                            inspector: panel.inspector
                                            label: control.modelData.label
                                            current: control.modelData.value
                                            onPicked: (value) => panel.inspector.setEffectParam(effectItem.modelData.index,
                                                                                               control.modelData.name, value)
                                        }
                                    }
                                }
                            }
                        }
                    }
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        role: "bodySmall"
                        color: Theme.color.onSurfaceVariant
                        text: qsTr("Add more from the Effects library: they stack in this order.")
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
                            keyframeKey: "volume"
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

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.space.sm
                            Button {
                                objectName: "normalizeLoudnessButton"
                                Layout.fillWidth: true
                                variant: "tonal"
                                iconName: "graphic_eq"
                                text: qsTr("Normalize (-14 LUFS)")
                                onClicked: panel.inspector.normalizeLoudness(-14.0)
                            }
                            Button {
                                objectName: "autoDuckingButton"
                                Layout.fillWidth: true
                                variant: "tonal"
                                iconName: "volume_down"
                                text: qsTr("Auto ducking")
                                onClicked: panel.inspector.autoDuck(-12.0)
                            }
                        }
                    }

                    PropertySection {
                        inspector: panel.inspector
                        section: "audio"
                        title: qsTr("Voice & Noise")

                        RowLayout {
                            Layout.fillWidth: true
                            Label { Layout.fillWidth: true; text: qsTr("Noise reduction"); role: "bodyMedium" }
                            Switch {
                                objectName: "denoiseSwitch"
                                checked: panel.values["audio.denoise"] ?? false
                                Accessible.name: qsTr("Noise reduction")
                                onToggled: panel.inspector.set("audio.denoise", checked)
                            }
                        }
                        PropertySlider {
                            visible: panel.values["audio.denoise"] ?? false
                            inspector: panel.inspector
                            key: "audio.denoiseAmount"
                            label: qsTr("Noise reduction amount")
                            from: 0
                            to: 1
                            neutral: 0.5
                            stepSize: 0.01
                            format: v => Math.round(v * 100) + "%"
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            Label { Layout.fillWidth: true; text: qsTr("Voice effect"); role: "bodyMedium" }
                            ComboBox {
                                objectName: "voiceEffectCombo"
                                model: [
                                    { text: qsTr("None"), value: "none" },
                                    { text: qsTr("Enhance voice"), value: "enhance" },
                                    { text: qsTr("Deep voice"), value: "deep" },
                                    { text: qsTr("Chipmunk"), value: "chipmunk" },
                                    { text: qsTr("Robot"), value: "robot" },
                                    { text: qsTr("Radio"), value: "radio" },
                                    { text: qsTr("Megaphone"), value: "megaphone" },
                                    { text: qsTr("Echo"), value: "echo" }
                                ]
                                textRole: "text"
                                valueRole: "value"
                                currentIndex: {
                                    const eff = panel.values["audio.voiceEffect"] ?? "none"
                                    for (let i = 0; i < count; ++i) {
                                        if (model[i].value === eff) return i
                                    }
                                    return 0
                                }
                                Accessible.name: qsTr("Voice effect")
                                onActivated: (index) => panel.inspector.set("audio.voiceEffect", model[index].value)
                            }
                        }

                        Button {
                            objectName: "enhanceVoiceButton"
                            Layout.fillWidth: true
                            variant: "filled"
                            iconName: "auto_fix_high"
                            text: qsTr("Enhance voice")
                            onClicked: {
                                panel.inspector.set("audio.voiceEffect", "enhance")
                                panel.inspector.set("audio.denoise", true)
                            }
                        }
                    }

                    PropertySection {
                        inspector: panel.inspector
                        section: "audio"
                        title: qsTr("Equalizer & Dynamics")

                        PropertySlider {
                            inspector: panel.inspector
                            key: "audio.eq.low"
                            label: qsTr("Bass (100 Hz)")
                            from: -12
                            to: 12
                            neutral: 0
                            stepSize: 0.5
                            format: v => (v > 0 ? "+" : "") + v.toLocaleString(Qt.locale(), "f", 1) + " dB"
                        }
                        PropertySlider {
                            inspector: panel.inspector
                            key: "audio.eq.mid"
                            label: qsTr("Mids (1 kHz)")
                            from: -12
                            to: 12
                            neutral: 0
                            stepSize: 0.5
                            format: v => (v > 0 ? "+" : "") + v.toLocaleString(Qt.locale(), "f", 1) + " dB"
                        }
                        PropertySlider {
                            inspector: panel.inspector
                            key: "audio.eq.high"
                            label: qsTr("Treble (10 kHz)")
                            from: -12
                            to: 12
                            neutral: 0
                            stepSize: 0.5
                            format: v => (v > 0 ? "+" : "") + v.toLocaleString(Qt.locale(), "f", 1) + " dB"
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            Label { Layout.fillWidth: true; text: qsTr("Compressor"); role: "bodyMedium" }
                            Switch {
                                objectName: "compressorSwitch"
                                checked: panel.values["audio.compressor.enabled"] ?? false
                                Accessible.name: qsTr("Compressor")
                                onToggled: panel.inspector.set("audio.compressor.enabled", checked)
                            }
                        }
                        PropertySlider {
                            visible: panel.values["audio.compressor.enabled"] ?? false
                            inspector: panel.inspector
                            key: "audio.compressor.threshold"
                            label: qsTr("Threshold")
                            from: -40
                            to: 0
                            neutral: -18
                            stepSize: 0.5
                            format: v => v.toLocaleString(Qt.locale(), "f", 1) + " dB"
                        }
                        PropertySlider {
                            visible: panel.values["audio.compressor.enabled"] ?? false
                            inspector: panel.inspector
                            key: "audio.compressor.ratio"
                            label: qsTr("Ratio")
                            from: 1
                            to: 20
                            neutral: 4
                            stepSize: 0.5
                            format: v => v.toLocaleString(Qt.locale(), "f", 1) + ":1"
                        }
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

                        SegmentedButton {
                            Layout.fillWidth: true
                            model: [{ text: qsTr("Constant") }, { text: qsTr("Curve") }]
                            currentIndex: (panel.values["speed.isCurve"] ?? false) ? 1 : 0
                            onActivated: (index) => panel.inspector.set("speed.isCurve", index === 1)
                        }

                        // Constant speed controls
                        ColumnLayout {
                            Layout.fillWidth: true
                            visible: !(panel.values["speed.isCurve"] ?? false)
                            spacing: Theme.space.sm

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
                        }

                        // Speed ramp curve controls (SPEC §5.5)
                        ColumnLayout {
                            Layout.fillWidth: true
                            visible: panel.values["speed.isCurve"] ?? false
                            spacing: Theme.space.sm

                            Label {
                                text: qsTr("Speed presets")
                                role: "bodySmall"
                                color: Theme.color.onSurfaceVariant
                            }

                            Flow {
                                Layout.fillWidth: true
                                spacing: Theme.space.xs
                                Repeater {
                                    model: panel.inspector.speedPresets
                                    delegate: Chip {
                                        required property var modelData
                                        objectName: "speedPreset_" + modelData.id
                                        text: modelData.label
                                        checkable: false
                                        checked: (panel.values["speed.curvePreset"] ?? "") === modelData.id
                                        onClicked: {
                                            panel.inspector.set("speed.curvePreset", modelData.id)
                                            panel.inspector.endGesture()
                                        }
                                    }
                                }
                            }

                            SpeedCurveEditor {
                                Layout.fillWidth: true
                                inspector: panel.inspector
                            }

                            Label {
                                Layout.fillWidth: true
                                text: qsTr("Drag the points to change the speed. Double click to add a point, right click to remove one.")
                                role: "bodySmall"
                                color: Theme.color.outline
                                wrapMode: Text.WordWrap
                            }
                        }

                        // Motion blur (SPEC §5.5)
                        RowLayout {
                            Layout.fillWidth: true
                            Label { Layout.fillWidth: true; text: qsTr("Motion blur"); role: "bodyMedium" }
                            Switch {
                                objectName: "motionBlurSwitch"
                                checked: panel.values["speed.motionBlur"] ?? false
                                Accessible.name: qsTr("Motion blur")
                                onToggled: panel.inspector.set("speed.motionBlur", checked)
                            }
                        }
                        PropertySlider {
                            visible: panel.values["speed.motionBlur"] ?? false
                            inspector: panel.inspector
                            key: "speed.motionBlurIntensity"
                            label: qsTr("Motion blur amount")
                            from: 0.0
                            to: 1.0
                            format: v => Math.round(v * 100) + " %"
                        }

                        // Slower than 1×: new frames computed between the real ones instead of repeating them.
                        RowLayout {
                            Layout.fillWidth: true
                            visible: (panel.values["speed"] ?? 1) < 1 && !(panel.values["reversed"] ?? false)
                                     && !(panel.values["speed.isCurve"] ?? false) && panel.inspector.kind === Inspector.Video
                            Label {
                                Layout.fillWidth: true
                                text: qsTr("Smooth slow motion")
                                role: "bodyMedium"
                                wrapMode: Text.WordWrap
                            }
                            Switch {
                                objectName: "smoothSwitch"
                                checked: panel.values["smooth"] ?? false
                                Accessible.name: qsTr("Smooth slow motion")
                                onToggled: panel.inspector.set("smooth", checked)
                            }
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            visible: panel.editor.player.preparingSmooth
                            spacing: Theme.space.xs
                            Label {
                                text: qsTr("Computing the new frames… %1%").arg(Math.round(panel.editor.player.smoothProgress * 100))
                                role: "bodySmall"
                                color: Theme.color.onSurfaceVariant
                            }
                            ProgressBar {
                                Layout.fillWidth: true
                                value: panel.editor.player.smoothProgress
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

                // ---- Cutout: a mask, and a colour made transparent (green screen) ------------------------------
                ColumnLayout {
                    Layout.fillWidth: true
                    visible: panel.page === "cutout"
                    spacing: Theme.space.md

                    // Remove background (SPEC 0bis rules 3 and 9): the subject alone, no green screen needed.
                    Button {
                        objectName: "removeBackgroundButton"
                        Layout.fillWidth: true
                        visible: panel.inspector.kind === Inspector.Video && !(panel.values["cutout"] ?? false)
                        enabled: !panel.editor.ai.busy
                        variant: "tonal"
                        iconName: "person_remove"
                        text: qsTr("Remove background")
                        onClicked: panel.editor.ai.removeBackground()
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        visible: panel.values["cutout"] ?? false
                        Label { Layout.fillWidth: true; text: qsTr("Background removed"); role: "bodyMedium" }
                        Switch {
                            objectName: "cutoutSwitch"
                            checked: true
                            Accessible.name: qsTr("Background removed")
                            onToggled: if (!checked) panel.inspector.set("cutout", false)
                        }
                    }

                    PropertySection {
                        inspector: panel.inspector
                        section: "cutout"
                        title: qsTr("Mask")
                        applyToAll: false

                        Flow {
                            Layout.fillWidth: true
                            spacing: Theme.space.xs
                            Repeater {
                                model: [{ shape: -1, text: qsTr("None") }, { shape: 0, text: qsTr("Line") },
                                        { shape: 1, text: qsTr("Band") }, { shape: 2, text: qsTr("Circle") },
                                        { shape: 3, text: qsTr("Rectangle") }, { shape: 4, text: qsTr("Heart") },
                                        { shape: 5, text: qsTr("Star") }]
                                delegate: Chip {
                                    required property var modelData
                                    objectName: "maskShape_" + modelData.shape
                                    text: modelData.text
                                    checkable: false
                                    checked: (panel.values["mask.shape"] ?? -1) === modelData.shape
                                    onClicked: panel.inspector.set("mask.shape", modelData.shape)
                                }
                            }
                        }
                        Label {
                            Layout.fillWidth: true
                            visible: panel.maskEditing
                            wrapMode: Text.WordWrap
                            role: "bodySmall"
                            color: Theme.color.onSurfaceVariant
                            text: qsTr("Drag the mask on the player to move it, its corner to resize it.")
                        }
                        PropertySlider {
                            visible: panel.maskEditing
                            inspector: panel.inspector
                            key: "mask.feather"
                            label: qsTr("Soft edge")
                            format: v => Math.round(v * 100)
                        }
                        PropertySlider {
                            visible: panel.maskEditing && panel.values["mask.shape"] === 3
                            inspector: panel.inspector
                            key: "mask.roundness"
                            label: qsTr("Rounded corners")
                            format: v => Math.round(v * 100)
                        }
                        PropertySlider {
                            visible: panel.maskEditing
                            inspector: panel.inspector
                            key: "mask.rotation"
                            label: qsTr("Rotation")
                            from: -180
                            to: 180
                            stepSize: 1
                            format: v => Math.round(v) + "°"
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            visible: panel.maskEditing
                            Label { Layout.fillWidth: true; text: qsTr("Invert"); role: "bodyMedium" }
                            Switch {
                                checked: panel.values["mask.invert"] ?? false
                                Accessible.name: qsTr("Invert the mask")
                                onToggled: panel.inspector.set("mask.invert", checked)
                            }
                        }

                        // Chroma key
                        RowLayout {
                            Layout.fillWidth: true
                            Label {
                                Layout.fillWidth: true
                                text: qsTr("Remove a colour (green screen)")
                                role: "titleSmall"
                                wrapMode: Text.WordWrap
                            }
                            Switch {
                                objectName: "chromaSwitch"
                                checked: panel.values["chroma.enabled"] ?? false
                                Accessible.name: qsTr("Remove a colour")
                                onToggled: panel.inspector.set("chroma.enabled", checked)
                            }
                        }
                        Button {
                            objectName: "pickKeyColour"
                            Layout.fillWidth: true
                            variant: panel.inspector.canvasMode === "pick" ? "filled" : "tonal"
                            iconName: "colorize"
                            text: panel.inspector.canvasMode === "pick" ? qsTr("Click the colour on the player…")
                                                                       : qsTr("Pick the colour on the player")
                            onClicked: panel.inspector.canvasMode = panel.inspector.canvasMode === "pick"
                                       ? (panel.maskEditing ? "mask" : "") : "pick"
                        }
                        ColorSwatches {
                            Layout.fillWidth: true
                            visible: panel.values["chroma.enabled"] ?? false
                            inspector: panel.inspector
                            label: qsTr("Colour to remove")
                            current: panel.values["chroma.color"] ?? "lime"
                            onPicked: (value) => panel.inspector.set("chroma.color", value)
                        }
                        PropertySlider {
                            visible: panel.values["chroma.enabled"] ?? false
                            inspector: panel.inspector
                            key: "chroma.similarity"
                            label: qsTr("Strength")
                            neutral: 0.4
                            format: v => Math.round(v * 100)
                        }
                        PropertySlider {
                            visible: panel.values["chroma.enabled"] ?? false
                            inspector: panel.inspector
                            key: "chroma.smoothness"
                            label: qsTr("Soft edge")
                            neutral: 0.1
                            format: v => Math.round(v * 100)
                        }
                        PropertySlider {
                            visible: panel.values["chroma.enabled"] ?? false
                            inspector: panel.inspector
                            key: "chroma.spill"
                            label: qsTr("Remove the coloured glow")
                            neutral: 0.5
                            format: v => Math.round(v * 100)
                        }
                    }
                }

                // ---- Adjust: auto enhance, filter, adjustments ----------------------------------------------
                ColumnLayout {
                    Layout.fillWidth: true
                    visible: panel.page === "adjust"
                    spacing: Theme.space.md

                    RowLayout {
                        Layout.fillWidth: true
                        visible: panel.inspector.kind !== Inspector.Adjustment
                        spacing: Theme.space.sm

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
                        IconButton {
                            objectName: "autoWhiteBalanceButton"
                            iconName: "wb_auto"
                            label: qsTr("Auto white balance")
                            onClicked: panel.inspector.autoWhiteBalance()
                        }
                        IconButton {
                            objectName: "matchColorButton"
                            iconName: "palette"
                            label: qsTr("Match colour")
                            onClicked: panel.inspector.matchColor()
                        }
                    }

                    PropertySection {
                        inspector: panel.inspector
                        section: "deflicker"
                        title: qsTr("Deflicker")

                        RowLayout {
                            Layout.fillWidth: true
                            Label {
                                Layout.fillWidth: true
                                text: qsTr("Remove flicker (artificial lights & timelapse)")
                                role: "bodyMedium"
                                wrapMode: Text.WordWrap
                            }
                            Switch {
                                objectName: "deflickerSwitch"
                                checked: panel.values["deflicker.enabled"] ?? false
                                onToggled: panel.inspector.set("deflicker.enabled", checked)
                            }
                        }
                        PropertySlider {
                            visible: panel.values["deflicker.enabled"] ?? false
                            inspector: panel.inspector
                            key: "deflicker.size"
                            label: qsTr("Window size (frames)")
                            from: 2
                            to: 30
                            stepSize: 1
                            neutral: 5
                            format: v => Math.round(v)
                        }
                    }

                    PropertySection {
                        inspector: panel.inspector
                        section: "filter"
                        title: (panel.values["filter"] ?? "") !== "" ? qsTr("Filter: %1").arg(panel.values["filter.name"]) : qsTr("Filter and LUT")
                        applyToAll: (panel.values["filter"] ?? "") !== "" || (panel.values["lut.path"] ?? "") !== ""

                        Label {
                            Layout.fillWidth: true
                            visible: (panel.values["filter"] ?? "") === "" && (panel.values["lut.path"] ?? "") === ""
                            wrapMode: Text.WordWrap
                            role: "bodySmall"
                            color: Theme.color.onSurfaceVariant
                            text: qsTr("Choose a filter in the Filters library on the left: hover to preview, click to apply.")
                        }
                        PropertySlider {
                            visible: (panel.values["filter"] ?? "") !== ""
                            inspector: panel.inspector
                            key: "filter.intensity"
                            label: qsTr("Filter intensity")
                            neutral: 1
                            format: v => Math.round(v * 100)
                        }

                        // LUT .cube
                        RowLayout {
                            Layout.fillWidth: true
                            Label {
                                Layout.fillWidth: true
                                text: (panel.values["lut.path"] ?? "") !== ""
                                      ? qsTr("LUT: %1").arg(panel.values["lut.name"])
                                      : qsTr("3D LUT (.cube)")
                                role: "bodyMedium"
                                elide: Text.ElideRight
                            }
                            Button {
                                objectName: "chooseLutButton"
                                variant: (panel.values["lut.path"] ?? "") !== "" ? "tonal" : "outlined"
                                text: (panel.values["lut.path"] ?? "") !== "" ? qsTr("Change…") : qsTr("Import…")
                                iconName: "folder_open"
                                onClicked: lutDialog.open()
                            }
                            IconButton {
                                visible: (panel.values["lut.path"] ?? "") !== ""
                                iconName: "close"
                                label: qsTr("Remove LUT")
                                onClicked: panel.inspector.set("lut.path", "")
                            }
                        }
                        PropertySlider {
                            visible: (panel.values["lut.path"] ?? "") !== ""
                            inspector: panel.inspector
                            key: "lut.intensity"
                            label: qsTr("LUT intensity")
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

                    PropertySection {
                        id: wheelsSection
                        inspector: panel.inspector
                        section: "grade"
                        title: qsTr("Color wheels")

                        property string activeWheel: "shadows"

                        SegmentedButton {
                            Layout.fillWidth: true
                            model: [
                                { text: qsTr("Shadows") },
                                { text: qsTr("Midtones") },
                                { text: qsTr("Highlights") }
                            ]
                            currentIndex: wheelsSection.activeWheel === "midtones" ? 1
                                        : wheelsSection.activeWheel === "highlights" ? 2 : 0
                            onActivated: (index) => {
                                const w = ["shadows", "midtones", "highlights"]
                                wheelsSection.activeWheel = w[index]
                            }
                        }

                        ColorWheel {
                            Layout.fillWidth: true
                            inspector: panel.inspector
                            zone: wheelsSection.activeWheel
                            title: wheelsSection.activeWheel === "shadows" ? qsTr("Shadows (Lift)")
                                 : wheelsSection.activeWheel === "midtones" ? qsTr("Midtones (Gamma)")
                                 : qsTr("Highlights (Gain)")
                        }
                    }

                    PropertySection {
                        id: hslSection
                        inspector: panel.inspector
                        section: "grade"
                        title: qsTr("HSL (8 ranges)")

                        property string activeRange: "red"

                        readonly property var ranges: [
                            { name: "red", label: qsTr("Red"), color: "#ff4444" },
                            { name: "orange", label: qsTr("Orange"), color: "#ff8833" },
                            { name: "yellow", label: qsTr("Yellow"), color: "#ffee33" },
                            { name: "green", label: qsTr("Green"), color: "#44cc44" },
                            { name: "aqua", label: qsTr("Aqua"), color: "#33ddcc" },
                            { name: "blue", label: qsTr("Blue"), color: "#3377ff" },
                            { name: "purple", label: qsTr("Purple"), color: "#9944ee" },
                            { name: "magenta", label: qsTr("Magenta"), color: "#ee3399" }
                        ]

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.space.xs
                            Repeater {
                                model: hslSection.ranges
                                delegate: Rectangle {
                                    id: chip
                                    required property var modelData
                                    width: 24
                                    height: 24
                                    radius: 12
                                    color: modelData.color
                                    border.color: hslSection.activeRange === modelData.name ? Theme.color.primary : "transparent"
                                    border.width: 2.5
                                    scale: hslSection.activeRange === modelData.name ? 1.15 : 1.0

                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: hslSection.activeRange = chip.modelData.name
                                    }
                                }
                            }
                        }

                        PropertySlider {
                            inspector: panel.inspector
                            key: "grade.hsl." + hslSection.activeRange + ".hue"
                            label: qsTr("Hue")
                            from: -1.0
                            to: 1.0
                            neutral: 0.0
                            format: v => Math.round(v * 100)
                        }
                        PropertySlider {
                            inspector: panel.inspector
                            key: "grade.hsl." + hslSection.activeRange + ".saturation"
                            label: qsTr("Saturation")
                            from: -1.0
                            to: 1.0
                            neutral: 0.0
                            format: v => Math.round(v * 100)
                        }
                        PropertySlider {
                            inspector: panel.inspector
                            key: "grade.hsl." + hslSection.activeRange + ".lightness"
                            label: qsTr("Lightness")
                            from: -1.0
                            to: 1.0
                            neutral: 0.0
                            format: v => Math.round(v * 100)
                        }
                    }

                    PropertySection {
                        inspector: panel.inspector
                        section: "grade"
                        title: qsTr("Color curves")

                        ColorCurvesEditor {
                            Layout.fillWidth: true
                            inspector: panel.inspector
                        }
                    }
                }

                Item { implicitHeight: Theme.space.md }
            }
        }
    }
}
