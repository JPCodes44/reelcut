import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Settings for whatever is being worked on, one tab per job.
Rectangle {
    id: root
    color: "#141417"

    property int selectedClip: -1
    property int selectedOverlay: -1
    property int selectedEffect: -1
    // A layer is selected: the Clip tab shows it instead of a clip.
    property int selectedLayer: -1
    property real playhead: 0
    // A text overlay is selected: text effects apply to it instead of adding new text.
    property bool textSelected: false
    property bool clipWheelsOpen: true
    // Width of the label column in the text cards, so their controls line up.
    readonly property int labelWidth: 72
    property bool lookWheelsOpen: true

    // A heading that opens and closes the section under it.
    component SectionToggle: AbstractButton {
        id: toggle
        property bool open: true
        Layout.fillWidth: true
        Layout.topMargin: 8
        implicitHeight: 32
        contentItem: RowLayout {
            Label { text: toggle.open ? "▾" : "▸"; color: "#9a9aa2" }
            Label { text: toggle.text; font.bold: true; Layout.fillWidth: true }
        }
        background: Rectangle { color: toggle.hovered ? "#24242a" : "transparent"; radius: 6 }
    }
    property alias currentIndex: tabs.currentIndex
    readonly property var clipData: selectedClip >= 0 && selectedClip < backend.clips.length ? backend.clips[selectedClip] : null
    readonly property var layerData: selectedLayer >= 0 && selectedLayer < backend.layers.length ? backend.layers[selectedLayer] : null
    readonly property var effectData: selectedEffect >= 0 && selectedEffect < backend.effects.length ? backend.effects[selectedEffect] : null
    signal overlaySelected(int index)
    signal effectSelected(int index)
    signal seekRequested(real t)

    function seconds(value) { return value.toLocaleString(Qt.locale(), 'f', 2) + " s" }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        TabBar {
            id: tabs
            Layout.fillWidth: true
            Material.background: "#141417"
            TabButton { text: qsTr("Clip") }
            TabButton { text: qsTr("Captions") }
            TabButton { text: qsTr("Text") }
            TabButton { text: qsTr("Look") }
            TabButton { text: qsTr("Audio") }
            TabButton { text: qsTr("Effects") }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: tabs.currentIndex

            // ---- Clip ----
            ScrollView {
                contentWidth: availableWidth
                ColumnLayout {
                    width: parent.width
                    spacing: 12
                    Item { Layout.preferredHeight: 4 }

                    Label {
                        Layout.leftMargin: 16
                        Layout.rightMargin: 16
                        Layout.fillWidth: true
                        visible: root.clipData === null && root.layerData === null
                        text: backend.clips.length === 0 ? qsTr("Add clips to start.") : qsTr("Click a clip in the timeline to edit it.")
                        color: "#9a9aa2"
                        wrapMode: Text.Wrap
                    }

                    // ---- Layer ----
                    ColumnLayout {
                        objectName: "layerPanel"
                        visible: root.layerData !== null
                        Layout.fillWidth: true
                        Layout.leftMargin: 16
                        Layout.rightMargin: 16
                        spacing: 12

                        Label {
                            Layout.fillWidth: true
                            text: root.layerData ? qsTr("Layer %1 · %2").arg(root.selectedLayer + 1).arg(root.layerData.name) : ""
                            font.bold: true
                            elide: Text.ElideMiddle
                        }
                        Label {
                            Layout.fillWidth: true
                            text: !root.layerData ? ""
                                  : qsTr("Shows %1 → %2 over the video. Drag its box in the preview to move it; drag a corner to resize.")
                                    .arg(root.seconds(root.layerData.start)).arg(root.seconds(root.layerData.end))
                            color: "#9a9aa2"
                            font.pixelSize: 12
                            wrapMode: Text.Wrap
                        }
                        ValueSlider {
                            label: qsTr("Size")
                            from: 0.1; to: 1; resetValue: 1
                            value: root.layerData ? root.layerData.scale : 1
                            valueText: Math.round(value * 100) + "%"
                            onMoved: function(v) { backend.setLayerValue(root.selectedLayer, "scale", v) }
                        }
                        ValueSlider {
                            label: qsTr("Position left / right")
                            from: 0; to: 1; resetValue: 0.5
                            value: root.layerData ? root.layerData.x : 0.5
                            onMoved: function(v) { backend.setLayerValue(root.selectedLayer, "x", v) }
                        }
                        ValueSlider {
                            label: qsTr("Position up / down")
                            from: 0; to: 1; resetValue: 0.5
                            value: root.layerData ? root.layerData.y : 0.5
                            onMoved: function(v) { backend.setLayerValue(root.selectedLayer, "y", v) }
                        }
                        ValueSlider {
                            label: qsTr("Zoom")
                            from: 1; to: 3; resetValue: 1
                            value: root.layerData ? root.layerData.zoom : 1
                            valueText: value.toLocaleString(Qt.locale(), 'f', 2) + "×"
                            onMoved: function(v) { backend.setLayerValue(root.selectedLayer, "zoom", v) }
                        }
                        ValueSlider {
                            label: qsTr("Pan left / right")
                            value: root.layerData ? root.layerData.panX : 0
                            onMoved: function(v) { backend.setLayerValue(root.selectedLayer, "panX", v) }
                        }
                        ValueSlider {
                            label: qsTr("Pan up / down")
                            value: root.layerData ? root.layerData.panY : 0
                            onMoved: function(v) { backend.setLayerValue(root.selectedLayer, "panY", v) }
                        }
                        Button {
                            Layout.fillWidth: true
                            text: qsTr("Move to main track")
                            onClicked: {
                                const at = backend.moveLayerToTrack(root.selectedLayer, root.layerData.start)
                                if (at >= 0) win.select("clip", at)
                            }
                        }
                        Button {
                            Layout.fillWidth: true
                            text: qsTr("Delete layer")
                            flat: true
                            Material.foreground: "#ff8a80"
                            onClicked: backend.removeLayer(root.selectedLayer)
                        }
                    }

                    ColumnLayout {
                        visible: root.clipData !== null && root.layerData === null
                        Layout.fillWidth: true
                        Layout.leftMargin: 16
                        Layout.rightMargin: 16
                        spacing: 12

                        Label {
                            Layout.fillWidth: true
                            text: root.clipData ? root.clipData.name : ""
                            font.bold: true
                            elide: Text.ElideMiddle
                        }
                        Label {
                            text: !root.clipData ? "" : root.clipData.still ? qsTr("Photo, %1x%2").arg(root.clipData.width).arg(root.clipData.height)
                                  : qsTr("%1 → %2, plays %3").arg(root.seconds(root.clipData.in)).arg(root.seconds(root.clipData.out)).arg(root.seconds(root.clipData.duration))
                            Layout.fillWidth: true
                            color: "#9a9aa2"
                            font.pixelSize: 12
                            wrapMode: Text.Wrap
                        }

                        ValueSlider {
                            visible: root.clipData !== null && root.clipData.still
                            label: qsTr("Duration")
                            from: 0.5; to: 10; resetValue: 2.5; stepSize: 0.1
                            value: root.clipData ? root.clipData.duration : 2.5
                            valueText: value.toLocaleString(Qt.locale(), 'f', 1) + " s"
                            onMoved: function(v) { backend.setClipValue(root.selectedClip, "duration", v) }
                        }
                        RowLayout {
                            visible: root.clipData !== null && root.clipData.still
                            Layout.fillWidth: true
                            spacing: 10
                            Label { text: qsTr("Motion"); Layout.fillWidth: true; Layout.minimumWidth: 48; elide: Text.ElideRight }
                            ComboBox {
                                id: motionBox
                                Layout.fillWidth: true
                                Layout.preferredWidth: 200
                                Layout.maximumWidth: 200
                                Layout.minimumWidth: 112
                                textRole: "label"
                                valueRole: "id"
                                model: [{ id: "none", label: qsTr("Still") }, { id: "zoom-in", label: qsTr("Zoom in") },
                                        { id: "zoom-out", label: qsTr("Zoom out") }, { id: "pan-left", label: qsTr("Pan left") },
                                        { id: "pan-right", label: qsTr("Pan right") }]
                                currentIndex: root.clipData ? indexOfValue(root.clipData.motion) : 0
                                onActivated: backend.setClipMotion(root.selectedClip, currentValue)
                            }
                        }
                        // The transition from this clip into the next one.
                        RowLayout {
                            visible: root.clipData !== null && root.selectedClip < backend.clips.length - 1
                            Layout.fillWidth: true
                            spacing: 10
                            Label { text: qsTr("Into next clip"); Layout.fillWidth: true; Layout.minimumWidth: 48; elide: Text.ElideRight }
                            ComboBox {
                                objectName: "transitionBox"
                                Layout.fillWidth: true
                                Layout.preferredWidth: 200
                                Layout.maximumWidth: 200
                                Layout.minimumWidth: 112
                                textRole: "label"
                                valueRole: "id"
                                model: [{ id: "", label: qsTr("Cut") }].concat(backend.transitions)
                                currentIndex: root.clipData ? Math.max(0, indexOfValue(root.clipData.transition)) : 0
                                onActivated: backend.setClipTransition(root.selectedClip, currentValue)
                            }
                        }
                        // Body effect: treats the person and the background differently.
                        RowLayout {
                            visible: root.clipData !== null
                            Layout.fillWidth: true
                            spacing: 10
                            Label { text: qsTr("Body effect"); Layout.fillWidth: true; Layout.minimumWidth: 48; elide: Text.ElideRight }
                            ComboBox {
                                objectName: "bodyBox"
                                Layout.fillWidth: true
                                Layout.preferredWidth: 200
                                Layout.maximumWidth: 200
                                Layout.minimumWidth: 112
                                textRole: "label"
                                valueRole: "id"
                                model: [{ id: "", label: qsTr("None") }].concat(backend.bodyKinds)
                                currentIndex: root.clipData ? Math.max(0, indexOfValue(root.clipData.body)) : 0
                                onActivated: backend.setClipBody(root.selectedClip, currentValue)
                            }
                        }
                        ValueSlider {
                            visible: root.clipData !== null && root.clipData.body.length > 0
                            label: qsTr("Body effect strength")
                            from: 0; to: 1; resetValue: 0.8
                            value: root.clipData ? root.clipData.bodyAmount : 0
                            onMoved: function(v) { backend.setClipValue(root.selectedClip, "bodyAmount", v) }
                        }
                        Label {
                            visible: root.clipData !== null && root.clipData.body.length > 0 && root.clipData.mask === ""
                            Layout.fillWidth: true
                            text: backend.busy ? backend.busyText : qsTr("Not ready yet: Reelcut finds the person before exporting.")
                            color: "#9a9aa2"
                            wrapMode: Text.Wrap
                            font.pixelSize: 12
                        }
                        ValueSlider {
                            visible: root.clipData !== null && root.clipData.transition.length > 0
                                     && root.selectedClip < backend.clips.length - 1
                            label: qsTr("Transition length")
                            from: 0.1; to: 2; resetValue: 0.5; stepSize: 0.05
                            value: root.clipData ? root.clipData.transitionSeconds : 0.5
                            // The overlap can be shorter than asked when a clip is short.
                            valueText: root.clipData ? root.clipData.overlap.toLocaleString(Qt.locale(), 'f', 2) + " s" : ""
                            onMoved: function(v) { backend.setClipValue(root.selectedClip, "transitionSeconds", v) }
                        }
                        ValueSlider {
                            visible: root.clipData === null || !root.clipData.still
                            label: qsTr("Speed")
                            from: 0.25; to: 4; resetValue: 1; stepSize: 0.05
                            value: root.clipData ? root.clipData.speed : 1
                            valueText: value.toLocaleString(Qt.locale(), 'f', 2) + "×"
                            onMoved: function(v) { backend.setClipValue(root.selectedClip, "speed", v) }
                        }
                        ValueSlider {
                            label: qsTr("Zoom")
                            from: 1; to: 3; resetValue: 1
                            value: root.clipData ? root.clipData.zoom : 1
                            valueText: value.toLocaleString(Qt.locale(), 'f', 2) + "×"
                            onMoved: function(v) { backend.setClipValue(root.selectedClip, "zoom", v) }
                        }
                        ValueSlider {
                            label: qsTr("Pan left / right")
                            value: root.clipData ? root.clipData.panX : 0
                            onMoved: function(v) { backend.setClipValue(root.selectedClip, "panX", v) }
                        }
                        ValueSlider {
                            label: qsTr("Pan up / down")
                            value: root.clipData ? root.clipData.panY : 0
                            onMoved: function(v) { backend.setClipValue(root.selectedClip, "panY", v) }
                        }

                        // DaVinci-style primaries for this clip only.
                        SectionToggle {
                            objectName: "clipWheelsToggle"
                            text: qsTr("Color wheels (this clip)")
                            open: root.clipWheelsOpen
                            onToggled: root.clipWheelsOpen = !root.clipWheelsOpen
                        }
                        GradePanel {
                            objectName: "clipGrade"
                            visible: root.clipWheelsOpen && root.clipData !== null
                            index: root.selectedClip
                            grade: root.clipData ? root.clipData.grade : ({})
                        }

                        GridLayout {
                            Layout.fillWidth: true
                            columns: 2
                            columnSpacing: 8
                            rowSpacing: 8
                            Button { Layout.fillWidth: true; text: qsTr("Split at playhead"); onClicked: backend.split(root.playhead) }
                            Button {
                                Layout.fillWidth: true
                                text: qsTr("Cut silences")
                                visible: root.clipData === null || !root.clipData.still
                                enabled: root.clipData !== null && root.clipData.audio && !backend.busy
                                onClicked: backend.cutSilences(root.selectedClip)
                            }
                            Button {
                                Layout.fillWidth: true
                                text: qsTr("Move earlier")
                                enabled: root.selectedClip > 0
                                onClicked: { backend.moveClip(root.selectedClip, root.selectedClip - 1); win.selectedClip-- }
                            }
                            Button {
                                Layout.fillWidth: true
                                text: qsTr("Move later")
                                enabled: root.selectedClip >= 0 && root.selectedClip < backend.clips.length - 1
                                onClicked: { backend.moveClip(root.selectedClip, root.selectedClip + 1); win.selectedClip++ }
                            }
                        }
                        Button {
                            Layout.fillWidth: true
                            text: qsTr("Delete clip")
                            flat: true
                            Material.foreground: "#ff8a80"
                            onClicked: backend.removeClip(root.selectedClip)
                        }
                    }

                    Button {
                        Layout.leftMargin: 16
                        Layout.rightMargin: 16
                        Layout.fillWidth: true
                        text: qsTr("Add Videos or Photos")
                        onClicked: backend.addMedia()
                    }
                    Item { Layout.preferredHeight: 4 }  // bottom breathing room when scrolled
                }
            }

            // ---- Captions ----
            ScrollView {
                contentWidth: availableWidth
                ColumnLayout {
                    width: parent.width
                    spacing: 12
                    Item { Layout.preferredHeight: 4 }

                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.leftMargin: 16
                        Layout.rightMargin: 16
                        spacing: 12
                        visible: backend.wordCount === 0
                        Label {
                            Layout.fillWidth: true
                            text: qsTr("Turn what's said in the clips into captions, on this computer.")
                            wrapMode: Text.Wrap
                        }
                        Label {
                            Layout.fillWidth: true
                            visible: backend.whisperState !== "ready"
                            text: backend.whisperState === "no-whisper"
                                  ? qsTr("Needs whisper.cpp: omarchy-pkg-add whisper-cpp")
                                  : qsTr("The first time downloads a 148 MB speech model.")
                            color: backend.whisperState === "no-whisper" ? "#ff8a65" : "#9a9aa2"
                            wrapMode: Text.Wrap
                            font.pixelSize: 12
                        }
                        Button {
                            Layout.fillWidth: true
                            text: qsTr("Generate Captions")
                            highlighted: true
                            Material.foreground: theme.accentForeground
                            enabled: backend.clips.length > 0 && !backend.busy
                            onClicked: backend.transcribe()
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.leftMargin: 16
                        Layout.rightMargin: 16
                        spacing: 12
                        visible: backend.wordCount > 0

                        Label { text: qsTr("Style"); font.bold: true }
                        GridLayout {
                            Layout.fillWidth: true
                            columns: 2
                            columnSpacing: 8
                            rowSpacing: 8
                            Repeater {
                                model: [{ id: "highlight", label: qsTr("Highlight") }, { id: "pop", label: qsTr("Pop") },
                                        { id: "bold", label: qsTr("Bold") }, { id: "box", label: qsTr("Box") }]
                                delegate: Button {
                                    required property var modelData
                                    Layout.fillWidth: true
                                    text: modelData.label
                                    checkable: true
                                    checked: backend.captionStyle === modelData.id
                                    highlighted: checked
                                    Material.foreground: checked ? theme.accentForeground : undefined
                                    onClicked: backend.setCaptionStyle(modelData.id)
                                }
                            }
                        }
                        ValueSlider {
                            label: qsTr("Size")
                            from: 0.5; to: 2; resetValue: 1
                            value: backend.captionSize
                            valueText: Math.round(value * 100) + "%"
                            onMoved: function(v) { backend.setCaptionValue("scale", v) }
                        }
                        ValueSlider {
                            label: qsTr("Up / down")
                            from: 0.05; to: 0.95
                            customReset: true
                            onReset: backend.resetCaptionPosition()
                            value: backend.captionY
                            valueText: Math.round(value * 100) + "%"
                            onMoved: function(v) { backend.setCaptionValue("y", v) }
                        }
                        ValueSlider {
                            label: qsTr("Left / right")
                            from: 0; to: 1
                            customReset: true
                            onReset: backend.resetCaptionPosition()
                            value: backend.captionX
                            valueText: Math.round(value * 100) + "%"
                            onMoved: function(v) { backend.setCaptionValue("x", v) }
                        }
                        // Captions follow the platform's spot until moved; this
                        // puts moved captions back on it.
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            Label {
                                Layout.fillWidth: true
                                Layout.alignment: Qt.AlignVCenter
                                wrapMode: Text.WordWrap
                                color: "#9a9aa2"
                                text: backend.captionAuto
                                      ? qsTr("Placed for %1. Moves with the platform.").arg(backend.platformInfo.label)
                                      : qsTr("Placed by hand.")
                            }
                            Button {
                                objectName: "captionResetPosition"
                                visible: !backend.captionAuto
                                text: qsTr("Place for %1").arg(backend.platformInfo.label)
                                flat: true
                                Layout.minimumWidth: implicitWidth
                                onClicked: backend.resetCaptionPosition()
                            }
                        }
                        Label {
                            Layout.fillWidth: true
                            text: qsTr("Or drag the captions in the preview, and pull a corner to resize.")
                            color: "#9a9aa2"
                            font.pixelSize: 12
                            wrapMode: Text.Wrap
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            Label { text: qsTr("%n words", "", backend.wordCount); Layout.fillWidth: true }
                            Label {
                                Layout.fillWidth: true
                                horizontalAlignment: Text.AlignRight
                                elide: Text.ElideRight
                                text: qsTr("Click a word to fix it")
                                color: "#9a9aa2"
                                font.pixelSize: 12
                            }
                        }

                        // The transcript, line by line; a word becomes editable on click.
                        Repeater {
                            model: backend.captionLines
                            delegate: Flow {
                                required property var modelData
                                Layout.fillWidth: true
                                spacing: 4
                                Repeater {
                                    model: parent.modelData.words
                                    delegate: Item {
                                        id: wordItem
                                        required property var modelData
                                        property bool editing: false
                                        width: editing ? Math.max(70, editor.implicitWidth + 16) : chip.implicitWidth
                                        height: 30
                                        Button {
                                            id: chip
                                            visible: !wordItem.editing
                                            flat: true
                                            padding: 4
                                            topInset: 0
                                            bottomInset: 0
                                            height: 30
                                            text: wordItem.modelData.text
                                            font.capitalization: Font.MixedCase
                                            highlighted: root.playhead >= wordItem.modelData.start && root.playhead < wordItem.modelData.end
                                            onClicked: {
                                                root.seekRequested(wordItem.modelData.start)
                                                wordItem.editing = true
                                                editor.text = wordItem.modelData.text
                                                editor.forceActiveFocus()
                                                editor.selectAll()
                                            }
                                        }
                                        TextField {
                                            id: editor
                                            visible: wordItem.editing
                                            anchors.fill: parent
                                            topPadding: 2
                                            bottomPadding: 2
                                            function commit() {
                                                if (!wordItem.editing) return
                                                wordItem.editing = false
                                                if (text !== wordItem.modelData.text) {
                                                    backend.checkpoint()
                                                    backend.setWordText(wordItem.modelData.index, text)
                                                }
                                            }
                                            onAccepted: commit()
                                            onActiveFocusChanged: if (!activeFocus) commit()
                                            Keys.onEscapePressed: { wordItem.editing = false }
                                        }
                                    }
                                }
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            Button { Layout.fillWidth: true; text: qsTr("Redo Captions"); enabled: !backend.busy; onClicked: backend.transcribe() }
                            Button { Layout.fillWidth: true; text: qsTr("Remove"); flat: true; onClicked: backend.clearCaptions() }
                        }
                    }
                    Item { Layout.preferredHeight: 4 }
                }
            }

            // ---- Text ----
            ScrollView {
                contentWidth: availableWidth
                ColumnLayout {
                    width: parent.width
                    spacing: 12
                    Item { Layout.preferredHeight: 4 }

                    Button {
                        Layout.leftMargin: 16
                        Layout.rightMargin: 16
                        Layout.fillWidth: true
                        text: qsTr("Add Text at Playhead")
                        enabled: backend.clips.length > 0
                        onClicked: root.overlaySelected(backend.addOverlay(root.playhead))
                    }

                    Repeater {
                        model: backend.overlays.length
                        delegate: Rectangle {
                            id: card
                            // Items stay put while their entry changes, so a drag survives edits.
                            readonly property var modelData: backend.overlays[index] || ({})
                            required property int index
                            readonly property bool selected: index === root.selectedOverlay
                            Layout.fillWidth: true
                            Layout.leftMargin: 16
                            Layout.rightMargin: 16
                            implicitHeight: body.implicitHeight + 24
                            radius: theme.radius
                            color: "#1c1c21"
                            border.color: selected ? theme.accent : "#2a2a2f"
                            TapHandler { onTapped: root.overlaySelected(card.index) }

                            ColumnLayout {
                                id: body
                                anchors.fill: parent
                                anchors.margins: 12
                                spacing: 8
                                // Multi-line: Enter starts a new line of the text.
                                TextArea {
                                    objectName: "overlayTextField" + card.index
                                    Layout.fillWidth: true
                                    placeholderText: qsTr("Text (Enter for a new line)")
                                    wrapMode: TextEdit.Wrap
                                    text: card.modelData.text
                                    onActiveFocusChanged: if (activeFocus) { backend.checkpoint(); root.overlaySelected(card.index) }
                                    // Only typing writes back; the binding above sets the same text again.
                                    onTextChanged: if (activeFocus && text !== card.modelData.text)
                                                       backend.setOverlayValue(card.index, "text", text)
                                }
                                // Each row: a label in a fixed column, the control filling the rest.
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 10
                                    Label { text: qsTr("Starts"); Layout.preferredWidth: root.labelWidth }
                                    SpinBox {
                                        id: startBox
                                        Layout.fillWidth: true
                                        from: 0; to: Math.ceil(backend.duration * 10); editable: true
                                        value: Math.round(card.modelData.start * 10)
                                        textFromValue: function(v) { return (v / 10).toLocaleString(Qt.locale(), 'f', 1) }
                                        valueFromText: function(t) { return Math.round(Number.fromLocaleString(Qt.locale(), t) * 10) }
                                        onValueModified: { backend.checkpoint(); backend.setOverlayValue(card.index, "start", value / 10) }
                                    }
                                }
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 10
                                    Label { text: qsTr("Ends"); Layout.preferredWidth: root.labelWidth }
                                    SpinBox {
                                        Layout.fillWidth: true
                                        from: 1; to: Math.ceil(backend.duration * 10) + 10; editable: true
                                        value: Math.round(card.modelData.end * 10)
                                        textFromValue: function(v) { return (v / 10).toLocaleString(Qt.locale(), 'f', 1) }
                                        valueFromText: function(t) { return Math.round(Number.fromLocaleString(Qt.locale(), t) * 10) }
                                        onValueModified: { backend.checkpoint(); backend.setOverlayValue(card.index, "end", value / 10) }
                                    }
                                }
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 10
                                    Label { text: qsTr("Style"); Layout.preferredWidth: root.labelWidth }
                                    ComboBox {
                                        objectName: "overlayStyle" + card.index
                                        Layout.fillWidth: true
                                        textRole: "label"
                                        valueRole: "id"
                                        model: [{ id: "plain", label: qsTr("Plain") }, { id: "fisheye", label: qsTr("Fisheye") }]
                                        // count: re-run once the model is in, or it stays blank
                                        currentIndex: count > 0 ? indexOfValue(card.modelData.style) : -1
                                        onActivated: { backend.checkpoint(); backend.setOverlayValue(card.index, "style", currentValue) }
                                    }
                                }
                                ValueSlider {
                                    visible: card.modelData.style === "fisheye"
                                    label: qsTr("Bulge")
                                    from: 0; to: 1; resetValue: 0.6
                                    value: card.modelData.bulge === undefined ? 0.6 : card.modelData.bulge
                                    valueText: Math.round(value * 100) + "%"
                                    onMoved: function(v) { backend.setOverlayValue(card.index, "bulge", v) }
                                }
                                // Type: font (every installed family, each shown in itself), bold, italic.
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 10
                                    Label { text: qsTr("Font"); Layout.preferredWidth: root.labelWidth }
                                    // Type to filter the fonts as you go (src/FontPicker.qml).
                                    FontPicker {
                                        objectName: "overlayFont" + card.index
                                        value: card.modelData.font || ""
                                        defaultFamily: card.modelData.defaultFont || ""
                                        onPicked: function(family) {
                                            backend.checkpoint()
                                            backend.setOverlayValue(card.index, "font", family)
                                        }
                                    }
                                }
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 10
                                    Label { text: qsTr("Weight"); Layout.preferredWidth: root.labelWidth }
                                    Button {
                                        objectName: "overlayBold" + card.index
                                        Layout.fillWidth: true
                                        Layout.preferredWidth: 1
                                        text: qsTr("Bold")
                                        font.bold: true
                                        flat: !checked
                                        highlighted: checked
                                        checkable: true
                                        checked: card.modelData.isBold === true
                                        onClicked: { backend.checkpoint(); backend.setOverlayValue(card.index, "bold", checked) }
                                        ToolTip.visible: hovered
                                        ToolTip.text: qsTr("Bold")
                                    }
                                    Button {
                                        objectName: "overlayItalic" + card.index
                                        Layout.fillWidth: true
                                        Layout.preferredWidth: 1
                                        text: qsTr("Italic")
                                        font.italic: true
                                        flat: !checked
                                        highlighted: checked
                                        checkable: true
                                        checked: card.modelData.italic === true
                                        onClicked: { backend.checkpoint(); backend.setOverlayValue(card.index, "italic", checked) }
                                        ToolTip.visible: hovered
                                        ToolTip.text: qsTr("Italic")
                                    }
                                }
                                // Double-click a label to go back to the style's default.
                                ValueSlider {
                                    label: qsTr("Letter thickness")
                                    from: 0; to: 20; stepSize: 0.5; resetValue: 0
                                    value: card.modelData.thickness || 0
                                    valueText: value.toFixed(1) + " px"
                                    onMoved: function(v) { backend.setOverlayValue(card.index, "thickness", v) }
                                }
                                ValueSlider {
                                    label: qsTr("Outline thickness")
                                    from: 0; to: 30; stepSize: 0.5; resetValue: -1
                                    value: card.modelData.outlineWidthValue || 0
                                    valueText: value.toFixed(1) + " px" + (card.modelData.outlineWidth < 0 ? qsTr(" (default)") : "")
                                    onMoved: function(v) {
                                        // A thickness needs a color to show: a transparent outline goes back to the default black.
                                        if (Qt.color(card.modelData.outlineColor).a === 0)
                                            backend.setOverlayValue(card.index, "outline", "default")
                                        backend.setOverlayValue(card.index, "outlineWidth", v)
                                    }
                                }
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 10
                                    ValueSlider {
                                        Layout.fillWidth: true
                                        enabled: !card.modelData.noShadow
                                        label: qsTr("Shadow distance")
                                        from: 0; to: 30; stepSize: 0.5; resetValue: -1
                                        value: card.modelData.shadowDistanceValue || 0
                                        valueText: value.toFixed(1) + " px" + (card.modelData.shadowDistance < 0 ? qsTr(" (default)") : "")
                                        onMoved: function(v) { backend.setOverlayValue(card.index, "shadowDistance", v) }
                                    }
                                    CheckBox {
                                        objectName: "overlayNoShadow" + card.index
                                        Layout.alignment: Qt.AlignBottom
                                        text: qsTr("No shadow")
                                        checked: card.modelData.noShadow === true
                                        onClicked: { backend.checkpoint(); backend.setOverlayValue(card.index, "noShadow", checked) }
                                    }
                                }
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 10
                                    ValueSlider {
                                        Layout.fillWidth: true
                                        enabled: !card.modelData.spacingDefault
                                        label: qsTr("Letter spacing")
                                        from: -20; to: 60; stepSize: 0.5; resetValue: 0
                                        value: card.modelData.letterSpacing || 0
                                        valueText: card.modelData.spacingDefault ? qsTr("default") : value.toFixed(1) + " px"
                                        onMoved: function(v) { backend.setOverlayValue(card.index, "letterSpacing", v) }
                                    }
                                    // Ticked: the font's own spacing. Unticked: the slider's, as last set.
                                    CheckBox {
                                        objectName: "overlaySpacingDefault" + card.index
                                        Layout.alignment: Qt.AlignBottom
                                        text: qsTr("Default")
                                        checked: card.modelData.spacingDefault !== false
                                        onClicked: { backend.checkpoint(); backend.setOverlayValue(card.index, "spacingDefault", checked) }
                                    }
                                }
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 10
                                    Label { text: qsTr("Entrance"); Layout.preferredWidth: root.labelWidth }
                                    ComboBox {
                                        Layout.fillWidth: true
                                        textRole: "label"
                                        valueRole: "id"
                                        model: [{ id: "pop", label: qsTr("Pop") }, { id: "fade", label: qsTr("Fade") },
                                                { id: "slide", label: qsTr("Slide up") }, { id: "none", label: qsTr("None") }]
                                        Component.onCompleted: currentIndex = indexOfValue(card.modelData.animation)
                                        onActivated: { backend.checkpoint(); backend.setOverlayValue(card.index, "animation", currentValue) }
                                    }
                                }
                                // Colors: each can be the style's default, transparent, or any color.
                                Repeater {
                                    model: [["fill", qsTr("Fill")], ["outline", qsTr("Outline")],
                                            ["shadow", qsTr("Shadow")], ["box", qsTr("Box")]]
                                    delegate: ColorPicker {
                                        required property var modelData
                                        label: modelData[1]
                                        value: card.modelData[modelData[0]] || ""
                                        shown: card.modelData[modelData[0] + "Color"] || "#00000000"
                                        onPicked: function(v) { backend.setOverlayValue(card.index, modelData[0], v) }
                                    }
                                }
                                ValueSlider {
                                    label: qsTr("Size")
                                    from: 40; to: 200; resetValue: 96; stepSize: 1
                                    value: card.modelData.size
                                    valueText: Math.round(value) + ""
                                    onMoved: function(v) { backend.setOverlayValue(card.index, "size", Math.round(v)) }
                                }
                                ValueSlider {
                                    label: qsTr("Left / right")
                                    from: 0; to: 1; resetValue: 0.5
                                    value: card.modelData.x
                                    valueText: Math.round(value * 100) + "%"
                                    onMoved: function(v) { backend.setOverlayValue(card.index, "x", v) }
                                }
                                ValueSlider {
                                    label: qsTr("Up / down")
                                    from: 0.05; to: 0.95; resetValue: 0.2
                                    value: card.modelData.y
                                    valueText: Math.round(value * 100) + "%"
                                    onMoved: function(v) { backend.setOverlayValue(card.index, "y", v) }
                                }
                                Button {
                                    Layout.fillWidth: true
                                    text: qsTr("Delete")
                                    flat: true
                                    Material.foreground: "#ff8a80"
                                    onClicked: backend.removeOverlay(card.index)
                                }
                            }
                        }
                    }
                    Item { Layout.preferredHeight: 4 }
                }
            }

            // ---- Look ----
            ScrollView {
                contentWidth: availableWidth
                ColumnLayout {
                    width: parent.width
                    spacing: 12
                    Item { Layout.preferredHeight: 4 }

                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.leftMargin: 16
                        Layout.rightMargin: 16
                        spacing: 12

                        Label { text: qsTr("Filters"); font.bold: true }
                        GridLayout {
                            Layout.fillWidth: true
                            columns: 4
                            columnSpacing: 8
                            rowSpacing: 8
                            Repeater {
                                model: [{ id: "", label: qsTr("None"), swatch: ["#7a7a80", "#7a7a80", "#7a7a80"] }].concat(backend.filters)
                                delegate: Rectangle {
                                    id: tile
                                    required property var modelData
                                    readonly property bool chosen: backend.look.filter === modelData.id
                                    objectName: "filter-" + (modelData.id || "none")
                                    Layout.fillWidth: true
                                    implicitHeight: 58
                                    radius: Math.min(6, theme.radius)
                                    color: "#18181c"
                                    border.width: chosen ? 2 : 1
                                    border.color: chosen ? theme.accent : "#2a2a2f"
                                    Accessible.role: Accessible.Button
                                    Accessible.name: modelData.label
                                    // Skin, sky and foliage, as this look turns them.
                                    ColumnLayout {
                                        anchors.fill: parent
                                        anchors.margins: 6
                                        spacing: 4
                                        RowLayout {
                                            Layout.fillWidth: true
                                            Layout.preferredHeight: 22
                                            spacing: 0
                                            Repeater {
                                                model: tile.modelData.swatch
                                                delegate: Rectangle {
                                                    required property var modelData
                                                    Layout.fillWidth: true
                                                    Layout.fillHeight: true
                                                    color: modelData
                                                }
                                            }
                                        }
                                        Item { Layout.fillHeight: true }
                                        Label {
                                            Layout.fillWidth: true
                                            text: tile.modelData.label
                                            elide: Text.ElideRight
                                            font.pixelSize: 11
                                            color: tile.chosen ? "white" : "#c8c8cf"
                                        }
                                    }
                                    TapHandler { onTapped: backend.setFilter(tile.modelData.id) }
                                }
                            }
                        }
                        ValueSlider {
                            label: qsTr("Filter strength")
                            visible: backend.look.filter.length > 0
                            from: 0; to: 1; resetValue: 1
                            value: backend.look.filterAmount
                            onMoved: function(v) { backend.setLookValue("filterAmount", v) }
                        }

                        Label { text: qsTr("Presets"); font.bold: true; Layout.topMargin: 8 }
                        GridLayout {
                            Layout.fillWidth: true
                            columns: 3
                            columnSpacing: 8
                            rowSpacing: 8
                            Repeater {
                                model: [
                                    { id: "Natural", label: qsTr("Natural") }, { id: "Warm", label: qsTr("Warm") },
                                    { id: "Cool", label: qsTr("Cool") }, { id: "Punchy", label: qsTr("Punchy") },
                                    { id: "Film", label: qsTr("Film") }, { id: "Mono", label: qsTr("Mono") }
                                ]
                                delegate: Button {
                                    required property var modelData
                                    Layout.fillWidth: true
                                    text: modelData.label
                                    onClicked: backend.applyPreset(modelData.id)
                                }
                            }
                        }

                        ValueSlider { label: qsTr("Exposure"); value: backend.look.exposure; onMoved: function(v) { backend.setLookValue("exposure", v) } }
                        ValueSlider { label: qsTr("Contrast"); value: backend.look.contrast; onMoved: function(v) { backend.setLookValue("contrast", v) } }
                        ValueSlider { label: qsTr("Saturation"); value: backend.look.saturation; onMoved: function(v) { backend.setLookValue("saturation", v) } }
                        ValueSlider { label: qsTr("Temperature"); value: backend.look.temperature; onMoved: function(v) { backend.setLookValue("temperature", v) } }

                        // DaVinci-style primaries over the whole reel, after each clip's own.
                        SectionToggle {
                            objectName: "lookWheelsToggle"
                            text: qsTr("Color wheels (whole reel)")
                            open: root.lookWheelsOpen
                            onToggled: root.lookWheelsOpen = !root.lookWheelsOpen
                        }
                        GradePanel {
                            objectName: "lookGrade"
                            visible: root.lookWheelsOpen
                            index: -1
                            grade: backend.look.grade
                        }

                        Label { text: qsTr("LUT"); font.bold: true; Layout.topMargin: 8 }
                        Label {
                            Layout.fillWidth: true
                            text: backend.look.lut.length > 0 ? backend.look.lutName : qsTr("Any .cube LUT works, including ones made in DaVinci Resolve. It's applied on export.")
                            color: backend.look.lut.length > 0 ? "white" : "#9a9aa2"
                            wrapMode: Text.Wrap
                            font.pixelSize: 12
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            Button { Layout.fillWidth: true; text: qsTr("Choose LUT"); onClicked: backend.chooseLut() }
                            Button { Layout.fillWidth: true; text: qsTr("Remove"); flat: true; visible: backend.look.lut.length > 0; onClicked: backend.clearLut() }
                        }
                    }
                    Item { Layout.preferredHeight: 4 }
                }
            }

            // ---- Audio ----
            ScrollView {
                contentWidth: availableWidth
                ColumnLayout {
                    width: parent.width
                    spacing: 12
                    Item { Layout.preferredHeight: 4 }

                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.leftMargin: 16
                        Layout.rightMargin: 16
                        spacing: 12

                        Label { text: qsTr("Music"); font.bold: true }
                        Label {
                            Layout.fillWidth: true
                            text: backend.music.set ? backend.music.name : qsTr("No music yet.")
                            color: backend.music.set ? "white" : "#9a9aa2"
                            elide: Text.ElideMiddle
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            Button { Layout.fillWidth: true; text: backend.music.set ? qsTr("Replace Music") : qsTr("Add Music"); onClicked: backend.chooseMusic() }
                            Button { Layout.fillWidth: true; text: qsTr("Remove"); flat: true; visible: backend.music.set; onClicked: backend.removeMusic() }
                        }
                        ColumnLayout {
                            visible: backend.music.set
                            Layout.fillWidth: true
                            spacing: 12
                            ValueSlider {
                                label: qsTr("Music volume")
                                from: 0; to: 1.5; resetValue: 0.35
                                value: backend.music.volume
                                valueText: Math.round(value * 100) + "%"
                                onMoved: function(v) { backend.setMusicValue("volume", v) }
                            }
                            ValueSlider {
                                label: qsTr("Start the song at")
                                from: 0; to: 180; resetValue: 0; stepSize: 0.5
                                value: backend.music.offset
                                valueText: win.timeText(value)
                                onMoved: function(v) { backend.setMusicValue("offset", v) }
                            }
                            Switch {
                                Layout.fillWidth: true
                                text: qsTr("Lower the music when someone talks")
                                checked: backend.music.duck
                                onToggled: { backend.checkpoint(); backend.setMusicValue("duck", checked) }
                            }
                        }
                        Label {
                            Layout.fillWidth: true
                            Layout.topMargin: 8
                            text: qsTr("Exports are leveled to -14 LUFS, the loudness Instagram, Facebook, YouTube and TikTok play at.")
                            color: "#9a9aa2"
                            wrapMode: Text.Wrap
                            font.pixelSize: 12
                        }
                    }
                    Item { Layout.preferredHeight: 4 }
                }
            }
            // ---- Effects ----
            ScrollView {
                contentWidth: availableWidth
                ColumnLayout {
                    width: parent.width
                    spacing: 12
                    Item { Layout.preferredHeight: 4 }

                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.leftMargin: 16
                        Layout.rightMargin: 16
                        spacing: 12

                        Label {
                            Layout.fillWidth: true
                            text: qsTr("Add an effect at the playhead. Drag it in the Effects lane to move or trim it.")
                            color: "#9a9aa2"
                            wrapMode: Text.Wrap
                            font.pixelSize: 12
                        }
                        GridLayout {
                            Layout.fillWidth: true
                            columns: 3
                            columnSpacing: 8
                            rowSpacing: 8
                            Repeater {
                                model: backend.effectKinds
                                delegate: Button {
                                    required property var modelData
                                    objectName: "addEffect-" + modelData.id
                                    Layout.fillWidth: true
                                    text: modelData.label
                                    font.pixelSize: 12
                                    onClicked: {
                                        const i = backend.addEffect(modelData.id, root.playhead)
                                        if (i >= 0) root.effectSelected(i)
                                    }
                                }
                            }
                        }

                        Label {
                            Layout.fillWidth: true
                            text: root.textSelected ? qsTr("Text effects: applied to the selected text")
                                                    : qsTr("Text effects: adds new text at the playhead")
                            color: "#9a9aa2"
                            wrapMode: Text.Wrap
                            font.pixelSize: 12
                        }
                        Button {
                            objectName: "addFisheyeText"
                            Layout.fillWidth: true
                            text: qsTr("Fisheye Text")
                            font.pixelSize: 12
                            enabled: backend.clips.length > 0
                            // With text selected, it bends that text; otherwise it adds a new
                            // fisheye text, an overlay like any other.
                            onClicked: {
                                if (root.textSelected) {
                                    backend.checkpoint()
                                    backend.setOverlayValue(root.selectedOverlay, "style", "fisheye")
                                    root.overlaySelected(root.selectedOverlay)
                                } else {
                                    root.overlaySelected(backend.addFisheyeText(root.playhead))
                                }
                            }
                        }

                        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: "#2a2a2f"; visible: root.effectData !== null }

                        ColumnLayout {
                            Layout.fillWidth: true
                            visible: root.effectData !== null
                            spacing: 12
                            Label {
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                                text: root.effectData ? root.effectData.label : ""
                                font.pixelSize: 16
                                font.bold: true
                            }
                            ValueSlider {
                                label: qsTr("Intensity")
                                from: 0; to: 1; resetValue: 0.6
                                value: root.effectData ? root.effectData.intensity : 0
                                onMoved: function(v) { backend.setEffectValue(root.selectedEffect, "intensity", v) }
                            }
                            ValueSlider {
                                visible: root.effectData !== null && root.effectData.usesSpeed
                                label: qsTr("Speed")
                                from: 0; to: 1; resetValue: 0.5
                                value: root.effectData ? root.effectData.speed : 0
                                onMoved: function(v) { backend.setEffectValue(root.selectedEffect, "speed", v) }
                            }
                            ValueSlider {
                                label: qsTr("Starts")
                                from: 0; to: Math.max(1, backend.duration); resetValue: 0
                                value: root.effectData ? root.effectData.start : 0
                                valueText: root.seconds(value)
                                onMoved: function(v) { backend.setEffectValue(root.selectedEffect, "start", v) }
                            }
                            ValueSlider {
                                label: qsTr("Ends")
                                from: 0; to: Math.max(1, backend.duration); resetValue: 0
                                value: root.effectData ? root.effectData.end : 0
                                valueText: root.seconds(value)
                                onMoved: function(v) { backend.setEffectValue(root.selectedEffect, "end", v) }
                            }
                            Button {
                                Layout.fillWidth: true
                                text: qsTr("Delete Effect")
                                flat: true
                                Material.foreground: "#ff8a80"
                                onClicked: backend.removeEffect(root.selectedEffect)
                            }
                        }
                    }
                    Item { Layout.preferredHeight: 4 }
                }
            }
        }
    }
}
