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
    property real playhead: 0
    property alias currentIndex: tabs.currentIndex
    readonly property var clipData: selectedClip >= 0 && selectedClip < backend.clips.length ? backend.clips[selectedClip] : null
    signal overlaySelected(int index)
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
                    spacing: 14
                    anchors.margins: 18
                    Item { Layout.preferredHeight: 4 }

                    Label {
                        Layout.leftMargin: 18
                        Layout.rightMargin: 18
                        Layout.fillWidth: true
                        visible: root.clipData === null
                        text: backend.clips.length === 0 ? qsTr("Add clips to start.") : qsTr("Click a clip in the timeline to edit it.")
                        color: "#9a9aa2"
                        wrapMode: Text.Wrap
                    }

                    ColumnLayout {
                        visible: root.clipData !== null
                        Layout.fillWidth: true
                        Layout.leftMargin: 18
                        Layout.rightMargin: 18
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
                            color: "#9a9aa2"
                            font.pixelSize: 12
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
                            Label { text: qsTr("Motion"); Layout.fillWidth: true }
                            ComboBox {
                                id: motionBox
                                Layout.preferredWidth: 200
                                textRole: "label"
                                valueRole: "id"
                                model: [{ id: "none", label: qsTr("Still") }, { id: "zoom-in", label: qsTr("Zoom in") },
                                        { id: "zoom-out", label: qsTr("Zoom out") }, { id: "pan-left", label: qsTr("Pan left") },
                                        { id: "pan-right", label: qsTr("Pan right") }]
                                currentIndex: root.clipData ? indexOfValue(root.clipData.motion) : 0
                                onActivated: backend.setClipMotion(root.selectedClip, currentValue)
                            }
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

                        GridLayout {
                            Layout.fillWidth: true
                            columns: 2
                            columnSpacing: 8
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
                        Layout.leftMargin: 18
                        Layout.rightMargin: 18
                        Layout.fillWidth: true
                        text: qsTr("Add Videos or Photos")
                        onClicked: backend.addMedia()
                    }
                }
            }

            // ---- Captions ----
            ScrollView {
                contentWidth: availableWidth
                ColumnLayout {
                    width: parent.width
                    spacing: 14
                    Item { Layout.preferredHeight: 4 }

                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.leftMargin: 18
                        Layout.rightMargin: 18
                        spacing: 10
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
                        Layout.leftMargin: 18
                        Layout.rightMargin: 18
                        spacing: 12
                        visible: backend.wordCount > 0

                        Label { text: qsTr("Style") }
                        GridLayout {
                            Layout.fillWidth: true
                            columns: 2
                            columnSpacing: 8
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
                            label: qsTr("Position")
                            from: 0.1; to: 0.9; resetValue: 0.68
                            value: backend.captionY
                            valueText: Math.round(value * 100) + "%"
                            onMoved: function(v) { backend.setCaptionY(v) }
                        }

                        RowLayout {
                            Label { text: qsTr("%n words", "", backend.wordCount); Layout.fillWidth: true }
                            Label { text: qsTr("Click a word to fix it"); color: "#9a9aa2"; font.pixelSize: 12 }
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
                        Layout.leftMargin: 18
                        Layout.rightMargin: 18
                        Layout.fillWidth: true
                        text: qsTr("Add Text at Playhead")
                        enabled: backend.clips.length > 0
                        onClicked: root.overlaySelected(backend.addOverlay(root.playhead))
                    }

                    Repeater {
                        model: backend.overlays
                        delegate: Rectangle {
                            id: card
                            required property var modelData
                            required property int index
                            readonly property bool selected: index === root.selectedOverlay
                            Layout.fillWidth: true
                            Layout.leftMargin: 12
                            Layout.rightMargin: 12
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
                                TextField {
                                    Layout.fillWidth: true
                                    placeholderText: qsTr("Text")
                                    text: card.modelData.text
                                    onActiveFocusChanged: if (activeFocus) { backend.checkpoint(); root.overlaySelected(card.index) }
                                    onTextEdited: backend.setOverlayValue(card.index, "text", text)
                                }
                                RowLayout {
                                    Layout.fillWidth: true
                                    Label { text: qsTr("From") }
                                    SpinBox {
                                        id: startBox
                                        from: 0; to: Math.ceil(backend.duration * 10); editable: true
                                        value: Math.round(card.modelData.start * 10)
                                        textFromValue: function(v) { return (v / 10).toLocaleString(Qt.locale(), 'f', 1) }
                                        valueFromText: function(t) { return Math.round(Number.fromLocaleString(Qt.locale(), t) * 10) }
                                        onValueModified: { backend.checkpoint(); backend.setOverlayValue(card.index, "start", value / 10) }
                                        Layout.preferredWidth: 130
                                    }
                                    Label { text: qsTr("to") }
                                    SpinBox {
                                        from: 1; to: Math.ceil(backend.duration * 10) + 10; editable: true
                                        value: Math.round(card.modelData.end * 10)
                                        textFromValue: function(v) { return (v / 10).toLocaleString(Qt.locale(), 'f', 1) }
                                        valueFromText: function(t) { return Math.round(Number.fromLocaleString(Qt.locale(), t) * 10) }
                                        onValueModified: { backend.checkpoint(); backend.setOverlayValue(card.index, "end", value / 10) }
                                        Layout.preferredWidth: 130
                                    }
                                }
                                RowLayout {
                                    Layout.fillWidth: true
                                    Label { text: qsTr("Entrance") }
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
                                ValueSlider {
                                    label: qsTr("Size")
                                    from: 40; to: 200; resetValue: 96; stepSize: 1
                                    value: card.modelData.size
                                    valueText: Math.round(value) + ""
                                    onMoved: function(v) { backend.setOverlayValue(card.index, "size", Math.round(v)) }
                                }
                                ValueSlider {
                                    label: qsTr("Position")
                                    from: 0.05; to: 0.95; resetValue: 0.2
                                    value: card.modelData.y
                                    valueText: Math.round(value * 100) + "%"
                                    onMoved: function(v) { backend.setOverlayValue(card.index, "y", v) }
                                }
                                Button {
                                    text: qsTr("Delete")
                                    flat: true
                                    Material.foreground: "#ff8a80"
                                    onClicked: backend.removeOverlay(card.index)
                                }
                            }
                        }
                    }
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
                        Layout.leftMargin: 18
                        Layout.rightMargin: 18
                        spacing: 12

                        Label { text: qsTr("Presets") }
                        GridLayout {
                            Layout.fillWidth: true
                            columns: 3
                            columnSpacing: 6
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

                        Label { text: qsTr("LUT"); Layout.topMargin: 6 }
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
                        Layout.leftMargin: 18
                        Layout.rightMargin: 18
                        spacing: 12

                        Label { text: qsTr("Music") ; font.bold: true }
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
                                text: qsTr("Lower the music when someone talks")
                                checked: backend.music.duck
                                onToggled: { backend.checkpoint(); backend.setMusicValue("duck", checked) }
                            }
                        }
                        Label {
                            Layout.fillWidth: true
                            Layout.topMargin: 10
                            text: qsTr("Exports are leveled to -14 LUFS, the loudness Instagram and TikTok play at.")
                            color: "#9a9aa2"
                            wrapMode: Text.Wrap
                            font.pixelSize: 12
                        }
                    }
                }
            }
        }
    }
}
