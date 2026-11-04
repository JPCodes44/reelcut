import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

ApplicationWindow {
    id: win
    width: 1360
    height: 860
    minimumWidth: 980
    minimumHeight: 640
    visible: true
    title: (backend.dirty ? "• " : "") + backend.title + " — Reelcut"
    color: "#0e0e10"

    Material.theme: Material.Dark
    Material.accent: theme.accent
    Material.roundedScale: theme.radius

    readonly property color panel: "#17171a"
    readonly property color line: "#2a2a2f"
    readonly property color dim: "#9a9aa2"
    property bool helpVisible: false
    property bool safeZones: true
    property int selectedClip: -1
    property int selectedOverlay: -1
    property int selectedEffect: -1
    // What a click last picked: clip, overlay, captions, or none. Delete acts on it.
    property string selectionKind: "none"
    property real playhead: 0
    // Single-key shortcuts must not fire while the user is typing.
    readonly property bool typing: activeFocusItem !== null && activeFocusItem.hasOwnProperty("cursorPosition")

    // The color picker's eyedropper: the next click on the preview picks a
    // color from the video and hands it to `callback`. Escape cancels.
    property var eyedropperCallback: null
    function startEyedropper(callback) { eyedropperCallback = callback }

    function seek(t) {
        playhead = Math.max(0, Math.min(t, backend.duration))
        preview.seek(playhead)
    }
    function step(seconds) {
        preview.pause()
        seek(playhead + seconds)
    }
    function select(kind, index) {
        selectionKind = kind
        if (kind === "clip") {
            selectedClip = index
            inspector.currentIndex = 0
        } else if (kind === "overlay") {
            selectedOverlay = index
            inspector.currentIndex = 2
        } else if (kind === "captions") {
            inspector.currentIndex = 1
        } else if (kind === "effect") {
            selectedEffect = index
            inspector.currentIndex = 5
        }
    }
    function deleteSelection() {
        if (selectionKind === "effect" && selectedEffect >= 0) {
            backend.removeEffect(selectedEffect)
            selectedEffect = -1
            selectionKind = "none"
        } else if (selectionKind === "overlay" && selectedOverlay >= 0) {
            backend.removeOverlay(selectedOverlay)
            selectionKind = "none"
        } else if (selectionKind === "captions") {
            backend.clearCaptions()
            selectionKind = "none"
        } else if (selectedClip >= 0) {
            backend.removeClip(selectedClip)
        }
    }

    Connections {
        target: backend
        function onProjectChanged() {
            if (win.selectedClip >= backend.clips.length)
                win.selectedClip = backend.clips.length - 1
            if (win.selectedOverlay >= backend.overlays.length)
                win.selectedOverlay = -1
            if (win.selectedEffect >= backend.effects.length)
                win.selectedEffect = -1
            if (win.playhead > backend.duration)
                win.seek(backend.duration)
            else
                preview.refresh()
        }
    }

    Shortcut { sequence: "Escape"; enabled: win.eyedropperCallback !== null; context: Qt.ApplicationShortcut; onActivated: win.eyedropperCallback = null }
    Shortcut { sequence: "?"; enabled: !win.typing; context: Qt.ApplicationShortcut; onActivated: win.helpVisible = !win.helpVisible }
    Shortcut { sequence: "Esc"; context: Qt.ApplicationShortcut; onActivated: { win.helpVisible = false; win.selectionKind = "none"; win.contentItem.forceActiveFocus() } }
    Shortcut { sequence: "Q"; enabled: !win.typing; context: Qt.ApplicationShortcut; onActivated: win.close() }
    Shortcut { sequence: "Space"; enabled: !win.typing; context: Qt.ApplicationShortcut; onActivated: preview.toggle() }
    Shortcut { sequence: "S"; enabled: !win.typing; context: Qt.ApplicationShortcut; onActivated: backend.split(win.playhead) }
    Shortcut { sequence: "I"; enabled: !win.typing; context: Qt.ApplicationShortcut; onActivated: backend.addMedia() }
    Shortcut { sequence: "T"; enabled: !win.typing; context: Qt.ApplicationShortcut; onActivated: win.select("overlay", backend.addOverlay(win.playhead)) }
    Shortcut { sequence: "G"; enabled: !win.typing; context: Qt.ApplicationShortcut; onActivated: win.safeZones = !win.safeZones }
    Shortcut { sequences: ["Delete", "Backspace"]; enabled: !win.typing; context: Qt.ApplicationShortcut; onActivated: win.deleteSelection() }
    Shortcut { sequence: "Left"; enabled: !win.typing; context: Qt.ApplicationShortcut; onActivated: win.step(-1 / 30) }
    Shortcut { sequence: "Right"; enabled: !win.typing; context: Qt.ApplicationShortcut; onActivated: win.step(1 / 30) }
    Shortcut { sequence: "Shift+Left"; enabled: !win.typing; context: Qt.ApplicationShortcut; onActivated: win.step(-1) }
    Shortcut { sequence: "Shift+Right"; enabled: !win.typing; context: Qt.ApplicationShortcut; onActivated: win.step(1) }
    Shortcut { sequence: "Home"; enabled: !win.typing; context: Qt.ApplicationShortcut; onActivated: win.seek(0) }
    Shortcut { sequence: "End"; enabled: !win.typing; context: Qt.ApplicationShortcut; onActivated: win.seek(backend.duration) }
    Shortcut { sequence: "Alt+Left"; enabled: !win.typing && win.selectedClip > 0; context: Qt.ApplicationShortcut; onActivated: { backend.moveClip(win.selectedClip, win.selectedClip - 1); win.selectedClip-- } }
    Shortcut { sequence: "Alt+Right"; enabled: !win.typing && win.selectedClip >= 0 && win.selectedClip < backend.clips.length - 1; context: Qt.ApplicationShortcut; onActivated: { backend.moveClip(win.selectedClip, win.selectedClip + 1); win.selectedClip++ } }
    Shortcut { sequences: [StandardKey.Undo]; enabled: !win.typing; context: Qt.ApplicationShortcut; onActivated: backend.undo() }
    Shortcut { sequences: ["Ctrl+Y", "Ctrl+Shift+Z", StandardKey.Redo]; enabled: !win.typing; context: Qt.ApplicationShortcut; onActivated: backend.redo() }
    Shortcut { sequences: [StandardKey.Save]; context: Qt.ApplicationShortcut; onActivated: backend.save() }
    Shortcut { sequence: "Ctrl+Shift+S"; context: Qt.ApplicationShortcut; onActivated: backend.saveAs() }
    Shortcut { sequences: [StandardKey.Open]; context: Qt.ApplicationShortcut; onActivated: confirm.ask(qsTr("Open another project?"), function() { backend.open() }) }
    Shortcut { sequence: "Ctrl+E"; context: Qt.ApplicationShortcut; onActivated: backend.exportVideo() }

    // Quitting with unsaved edits asks first.
    property bool closeConfirmed: false
    onClosing: function(close) {
        if (backend.dirty && !closeConfirmed) {
            close.accepted = false
            confirm.ask(qsTr("Quit without saving your changes?"), function() { win.closeConfirmed = true; win.close() })
        }
    }

    Item {
        anchors.fill: parent
        LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
        LayoutMirroring.childrenInherit: true

        ColumnLayout {
            anchors.fill: parent
            spacing: 0

            // Top bar
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 52
                color: win.panel
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 16
                    anchors.rightMargin: 12
                    spacing: 8
                    Label {
                        text: backend.title
                        font.pixelSize: 16
                        font.bold: true
                        elide: Text.ElideRight
                        Layout.maximumWidth: 280
                    }
                    Label {
                        text: qsTr("%n clips", "", backend.clips.length) + " · " + timeText(backend.duration)
                        color: backend.duration > 90 ? "#ff8a65" : win.dim
                        elide: Text.ElideRight
                        ToolTip.visible: backend.duration > 90 && overDuration.hovered
                        ToolTip.text: qsTr("Longer than 90 seconds. Reels and TikToks perform best when short.")
                        HoverHandler { id: overDuration }
                    }
                    Item { Layout.fillWidth: true }
                    ButtonGroup { id: platforms }
                    Repeater {
                        model: [{ id: "instagram", label: qsTr("Instagram") }, { id: "tiktok", label: qsTr("TikTok") }]
                        delegate: Button {
                            required property var modelData
                            Layout.minimumWidth: implicitWidth
                            text: modelData.label
                            checkable: true
                            flat: true
                            checked: backend.platform === modelData.id
                            ButtonGroup.group: platforms
                            onClicked: backend.setPlatform(modelData.id)
                        }
                    }
                    ToolSeparator {}
                    ToolButton { Layout.minimumWidth: implicitWidth; text: qsTr("Undo"); enabled: backend.canUndo; onClicked: backend.undo() }
                    ToolButton { Layout.minimumWidth: implicitWidth; text: qsTr("Redo"); enabled: backend.canRedo; onClicked: backend.redo() }
                    ToolButton { Layout.minimumWidth: implicitWidth; text: qsTr("Open"); onClicked: confirm.ask(qsTr("Open another project?"), function() { backend.open() }) }
                    ToolButton { Layout.minimumWidth: implicitWidth; text: qsTr("Save"); onClicked: backend.save() }
                    Button {
                        Layout.minimumWidth: implicitWidth
                        text: qsTr("Photo Set")
                        flat: true
                        enabled: backend.clips.length > 0 && !backend.busy
                        ToolTip.visible: hovered
                        ToolTip.delay: 500
                        ToolTip.text: qsTr("One 1080×1920 JPG per clip, for TikTok photo posts")
                        onClicked: backend.exportPhotos()
                    }
                    Button {
                        objectName: "postButton"
                        Layout.minimumWidth: implicitWidth
                        text: qsTr("Post")
                        flat: true
                        enabled: backend.clips.length > 0 && !backend.busy
                        ToolTip.visible: hovered
                        ToolTip.delay: 500
                        ToolTip.text: qsTr("Export and open TikTok or Instagram, ready to post")
                        onClicked: postDialog.openFor("")
                    }
                    Button {
                        Layout.minimumWidth: implicitWidth
                        text: qsTr("Export")
                        highlighted: true
                        enabled: backend.clips.length > 0 && !backend.busy
                        Material.foreground: theme.accentForeground
                        onClicked: backend.exportVideo()
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 0

                Preview {
                    id: preview
                    objectName: "preview"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    time: win.playhead
                    safeZones: win.safeZones
                    onTimeAdvanced: function(t) { win.playhead = t }
                }

                Rectangle { Layout.fillHeight: true; Layout.preferredWidth: 1; color: win.line }

                Inspector {
                    id: inspector
                    objectName: "inspector"
                    Layout.preferredWidth: 440
                    Layout.fillHeight: true
                    selectedClip: win.selectedClip
                    selectedOverlay: win.selectedOverlay
                    selectedEffect: win.selectedEffect
                    textSelected: win.selectionKind === "overlay" && win.selectedOverlay >= 0
                                  && win.selectedOverlay < backend.overlays.length
                    playhead: win.playhead
                    onOverlaySelected: function(i) { win.select("overlay", i) }
                    onEffectSelected: function(i) { win.select("effect", i) }
                    onSeekRequested: function(t) { win.seek(t) }
                }
            }

            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: win.line }

            ReelTimeline {
                id: timeline
                Layout.fillWidth: true
                Layout.preferredHeight: implicitHeight
                Layout.minimumHeight: implicitHeight  // follows the resized music lane
                playhead: win.playhead
                selectedClip: win.selectedClip
                selectedOverlay: win.selectedOverlay
                selectedEffect: win.selectionKind === "effect" ? win.selectedEffect : -1
                onSeekRequested: function(t) { preview.pause(); win.seek(t) }
                onClipSelected: function(i) { win.select("clip", i) }
                onOverlaySelected: function(i) { win.select("overlay", i) }
                onEffectSelected: function(i) { win.select("effect", i) }
                onCaptionsSelected: win.select("captions", -1)
                onMusicSelected: inspector.currentIndex = 4
            }
        }

        // Videos can be dropped anywhere on the window.
        DropArea {
            anchors.fill: parent
            keys: ["text/uri-list"]
            onDropped: function(drop) {
                if (drop.hasUrls) {
                    backend.addUrls(drop.urls)
                    drop.accept()
                }
            }
            Rectangle {
                anchors.fill: parent
                visible: parent.containsDrag
                color: Qt.rgba(0, 0, 0, 0.55)
                border.color: theme.accent
                border.width: 3
                Label { anchors.centerIn: parent; text: qsTr("Drop to add clips"); font.pixelSize: 26 }
            }
        }

        // Busy bar, for anything that takes a while.
        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            // Floats over the preview, just above the timeline.
            anchors.bottomMargin: timeline.height + 16
            width: Math.min(460, parent.width - 32)
            height: busyRow.implicitHeight + 24
            radius: theme.radius
            color: "#232328"
            border.color: win.line
            visible: backend.busy
            RowLayout {
                id: busyRow
                anchors.fill: parent
                anchors.margins: 12
                spacing: 12
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignVCenter
                    spacing: 4
                    Label { Layout.fillWidth: true; text: backend.busyText; font.bold: true; elide: Text.ElideRight }
                    ProgressBar {
                        Layout.fillWidth: true
                        indeterminate: backend.progress < 0
                        value: Math.max(0, backend.progress)
                    }
                }
                Button { text: qsTr("Cancel"); flat: true; onClicked: backend.cancelJob() }
            }
        }

        // Messages: errors stay until dismissed, the rest fade on their own.
        Rectangle {
            id: toast
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.top
            anchors.topMargin: 64
            width: Math.min(620, parent.width - 32, toastRow.implicitWidth + 24)
            height: toastRow.implicitHeight + 24
            radius: theme.radius
            color: backend.messageIsError ? "#4a1d1d" : "#232328"
            border.color: backend.messageIsError ? "#c75050" : win.line
            visible: backend.message.length > 0
            onVisibleChanged: if (visible && !backend.messageIsError) toastTimer.restart()
            Timer { id: toastTimer; interval: 4500; onTriggered: if (!backend.messageIsError) backend.dismissMessage() }
            RowLayout {
                id: toastRow
                anchors.fill: parent
                anchors.margins: 12
                spacing: 8
                Label {
                    id: toastText
                    Layout.fillWidth: true
                    text: backend.message
                    wrapMode: Text.Wrap
                    textFormat: Text.PlainText
                }
                Button {
                    visible: !backend.messageIsError && backend.lastExport.length > 0 && backend.message.indexOf(backend.lastExport.split("/").pop()) >= 0
                    text: qsTr("Show")
                    flat: true
                    onClicked: backend.showExport()
                }
                ToolButton { text: "✕"; onClicked: backend.dismissMessage() }
            }
        }

        Dialog {
            id: confirm
            property var action
            function ask(question, then) {
                if (!backend.dirty) {
                    then()
                    return
                }
                confirmText.text = question
                action = then
                open()
            }
            anchors.centerIn: parent
            modal: true
            title: qsTr("Unsaved changes")
            standardButtons: Dialog.Yes | Dialog.No
            Label { id: confirmText }
            onAccepted: action()
        }

        PostDialog { id: postDialog }

        HelpOverlay {
            anchors.fill: parent
            visible: win.helpVisible
            onDismissed: win.helpVisible = false
        }
    }

    function timeText(seconds) {
        const s = Math.max(0, seconds)
        const m = Math.floor(s / 60)
        const rest = s - m * 60
        return m + ":" + (rest < 10 ? "0" : "") + rest.toLocaleString(Qt.locale(), 'f', 1)
    }
}
