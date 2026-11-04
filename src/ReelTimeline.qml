import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// The edit laid out in time: clips with filmstrips and trim handles, then
// lanes for captions, text overlays and music. Ctrl+scroll zooms.
Rectangle {
    id: root
    color: "#121215"

    property real playhead: 0
    property int selectedClip: -1
    property int selectedOverlay: -1
    property int selectedEffect: -1
    property real zoom: 1
    readonly property real gutter: 74
    readonly property real fitPps: Math.max(8, (width - gutter - 24) / Math.max(backend.duration, 5))
    readonly property real pps: fitPps * zoom
    readonly property real rulerH: 22
    readonly property real videoH: 76
    readonly property real laneH: 28
    property real musicH: 52
    // Lane stacking, shared by the gutter labels and the time-positioned
    // blocks so the two can't drift apart: Video, Captions, Text, Music, Effects.
    readonly property real laneGap: 6
    readonly property real videoY: rulerH + laneGap
    readonly property real captionsY: videoY + videoH + laneGap
    readonly property real textY: captionsY + laneH + laneGap
    readonly property real musicY: textY + laneH + laneGap
    readonly property real effectsY: musicY + musicH + laneGap
    implicitHeight: effectsY + laneH + 14

    // Keep the playhead in view while playing or stepping.
    onPlayheadChanged: {
        const x = playhead * pps
        if (x < flick.contentX || x > flick.contentX + flick.width - 20)
            flick.contentX = Math.max(0, Math.min(x - flick.width * 0.3, flick.contentWidth - flick.width))
    }

    signal seekRequested(real t)
    signal clipSelected(int index)
    signal overlaySelected(int index)
    signal effectSelected(int index)
    signal captionsSelected()
    signal musicSelected()

    // Scene coordinates avoid feedback as the timeline itself grows upward.
    // Keep the handle outside the horizontal Flickable so it spans the view.
    MouseArea {
        id: musicResize
        objectName: "musicResizeHandle"
        visible: backend.music.set
        x: 0
        y: root.musicY - 4
        width: root.width
        height: 8
        z: 100
        hoverEnabled: true
        preventStealing: true
        cursorShape: Qt.SizeVerCursor
        property real pressY: 0
        property real initialHeight: 52
        onPressed: function(mouse) {
            pressY = mapToItem(null, mouse.x, mouse.y).y
            initialHeight = root.musicH
        }
        onPositionChanged: function(mouse) {
            if (pressed) {
                const delta = mapToItem(null, mouse.x, mouse.y).y - pressY
                root.musicH = Math.max(52, Math.min(320, initialHeight - delta))
            }
        }
        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width
            height: musicResize.containsMouse || musicResize.pressed ? 2 : 1
            color: musicResize.containsMouse || musicResize.pressed ? theme.accent : "#526581"
        }
        Rectangle {
            anchors.centerIn: parent
            width: 32
            height: 4
            radius: 2
            color: musicResize.containsMouse || musicResize.pressed ? theme.accent : "#8799b3"
        }
        ToolTip.visible: containsMouse && !pressed
        ToolTip.text: qsTr("Drag up or down to resize the music waveform")
    }

    // Lane names stay put while the lanes scroll. The corner above them, beside
    // the ruler, holds the zoom hint so it never covers a lane.
    ColumnLayout {
        anchors.left: parent.left
        anchors.top: parent.top
        width: root.gutter
        spacing: root.laneGap

        Label {
            Layout.fillWidth: true
            Layout.preferredHeight: root.rulerH
            leftPadding: 12
            rightPadding: 4
            text: qsTr("Ctrl+scroll")
            color: "#5e5e66"
            font.pixelSize: 10
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
            ToolTip.visible: zoomHint.hovered
            ToolTip.text: qsTr("Ctrl+scroll to zoom")
            HoverHandler { id: zoomHint }
        }

        Repeater {
            model: [{ label: qsTr("Video"), h: root.videoH }, { label: qsTr("Captions"), h: root.laneH },
                    { label: qsTr("Text"), h: root.laneH }, { label: qsTr("Music"), h: root.musicH },
                    { label: qsTr("Effects"), h: root.laneH }]
            delegate: Label {
                required property var modelData
                Layout.fillWidth: true
                Layout.preferredHeight: modelData.h
                text: modelData.label
                color: "#8a8a92"
                font.pixelSize: 12
                leftPadding: 12
                rightPadding: 4
                elide: Text.ElideRight
                verticalAlignment: Text.AlignVCenter
            }
        }
    }

    Flickable {
        id: flick
        anchors.fill: parent
        anchors.leftMargin: root.gutter
        contentWidth: Math.max(width, backend.duration * root.pps + 40)
        contentHeight: height
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded }

        WheelHandler {
            acceptedModifiers: Qt.ControlModifier
            onWheel: function(event) {
                const anchorTime = (flick.contentX + event.x) / root.pps
                root.zoom = Math.max(1, Math.min(40, root.zoom * (event.angleDelta.y > 0 ? 1.25 : 0.8)))
                flick.contentX = Math.max(0, anchorTime * root.pps - event.x)
            }
        }

        // Clicking or dragging on the ruler (or any empty space) moves the playhead.
        MouseArea {
            width: flick.contentWidth
            height: flick.height
            preventStealing: true
            onPressed: function(mouse) { root.seekRequested(mouse.x / root.pps) }
            onPositionChanged: function(mouse) { if (pressed) root.seekRequested(Math.max(0, mouse.x / root.pps)) }
        }

        // Faint lane tracks so each row reads as a row even when it's empty.
        Repeater {
            model: [{ y: root.videoY, h: root.videoH }, { y: root.captionsY, h: root.laneH },
                    { y: root.textY, h: root.laneH }, { y: root.musicY, h: root.musicH },
                    { y: root.effectsY, h: root.laneH }]
            delegate: Rectangle {
                required property var modelData
                x: 0
                y: modelData.y
                width: flick.contentWidth
                height: modelData.h
                radius: Math.min(4, theme.radius)
                color: Qt.rgba(1, 1, 1, 0.025)
            }
        }

        // Ruler
        Repeater {
            model: Math.ceil(backend.duration) + 1
            delegate: Item {
                required property int index
                readonly property int every: root.pps < 18 ? 5 : root.pps < 40 ? 2 : 1
                visible: index % every === 0
                x: index * root.pps
                width: 1
                height: root.rulerH
                Rectangle { width: 1; height: 6; anchors.bottom: parent.bottom; color: "#55555c" }
                Label {
                    x: 4
                    text: Math.floor(index / 60) + ":" + String(index % 60).padStart(2, "0")
                    font.pixelSize: 10
                    color: "#7a7a82"
                }
            }
        }

        // Video lane
        Repeater {
            model: backend.clips.length
            delegate: Rectangle {
                id: clipItem
                objectName: "clip" + index
                // Items stay put while their entry changes, so a drag survives edits.
                readonly property var modelData: backend.clips[index] || ({})
                required property int index
                readonly property bool selected: index === root.selectedClip
                property real dragOffset: 0
                x: modelData.start * root.pps + dragOffset
                z: dragOffset !== 0 ? 10 : 0
                opacity: dragOffset !== 0 ? 0.8 : 1
                y: root.videoY
                width: Math.max(4, modelData.duration * root.pps - 2)
                height: root.videoH
                radius: Math.min(4, theme.radius)
                color: modelData.offline ? "#5a2020" : "#26262c"
                border.color: selected ? theme.accent : "#3a3a42"
                border.width: selected ? 2 : 1
                clip: true

                // Filmstrip
                Row {
                    x: 2
                    y: 2
                    Repeater {
                        model: Math.max(1, Math.ceil((clipItem.width - 4) / 44))
                        delegate: Image {
                            required property int index
                            readonly property real tileSeconds: 44 / root.pps * clipItem.modelData.speed
                            width: 44
                            height: root.videoH - 4
                            fillMode: Image.PreserveAspectCrop
                            asynchronous: true
                            cache: true
                            sourceSize.height: 90
                            source: clipItem.modelData.thumb > 0
                                    ? "image://thumbs/" + clipItem.modelData.thumb + "/"
                                      + (clipItem.modelData.still ? 0
                                         : Math.round(Math.min(clipItem.modelData.out - 0.05, clipItem.modelData.in + (index + 0.5) * tileSeconds) * 10) * 100)
                                    : ""
                            opacity: 0.85
                        }
                    }
                }

                Rectangle {
                    visible: !!clipItem.modelData.audio && !clipItem.modelData.still
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 2
                    height: 28
                    color: "#cc121820"
                    Waveform {
                        objectName: "clipWaveform" + clipItem.index
                        anchors.fill: parent
                        sourcePath: parent.visible ? (clipItem.modelData.path || "") : ""
                        sourceStart: clipItem.modelData.in || 0
                        speed: clipItem.modelData.speed || 1
                        pixelsPerSecond: root.pps
                        viewportStart: flick.contentX - clipItem.x
                        viewportWidth: flick.width
                    }
                }

                // Photo / speed badge, clear of the in-handle; hidden when the
                // clip is too narrow to show it whole.
                Label {
                    anchors.left: parent.left
                    anchors.bottom: parent.bottom
                    anchors.leftMargin: 12
                    anchors.bottomMargin: 6
                    text: clipItem.modelData.still ? "▣" : clipItem.modelData.speed !== 1 ? clipItem.modelData.speed + "×" : ""
                    font.pixelSize: 11
                    font.bold: true
                    padding: 2
                    visible: text.length > 0 && clipItem.width >= width + 24
                    background: Rectangle { color: Qt.rgba(0, 0, 0, 0.6); radius: 2 }
                }

                // Click to select and seek; drag sideways to move the clip elsewhere.
                MouseArea {
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    anchors.rightMargin: 8
                    preventStealing: true
                    cursorShape: dragging ? Qt.ClosedHandCursor : Qt.PointingHandCursor
                    property real startX: 0
                    property bool dragging: false
                    onPressed: function(mouse) {
                        startX = mapToItem(flick.contentItem, mouse.x, 0).x
                        dragging = false
                        root.clipSelected(clipItem.index)
                        root.seekRequested(clipItem.modelData.start + mouse.x / root.pps)
                    }
                    onPositionChanged: function(mouse) {
                        if (!pressed) return
                        const dx = mapToItem(flick.contentItem, mouse.x, 0).x - startX
                        if (!dragging && Math.abs(dx) < 6) return
                        dragging = true
                        clipItem.dragOffset = dx
                    }
                    onReleased: function(mouse) {
                        if (dragging) {
                            // Drop where the clip's middle lands.
                            const middle = clipItem.modelData.start + clipItem.modelData.duration / 2 + clipItem.dragOffset / root.pps
                            const target = backend.locate(Math.max(0, Math.min(middle, backend.duration))).index
                            clipItem.dragOffset = 0
                            dragging = false
                            if (target >= 0 && target !== clipItem.index) {
                                backend.moveClip(clipItem.index, target)
                                root.clipSelected(target)
                            }
                        }
                    }
                    onCanceled: { clipItem.dragOffset = 0; dragging = false }
                }

                // Trim handles: drag to move the in and out points.
                Repeater {
                    model: ["in", "out"]
                    delegate: Rectangle {
                        required property string modelData
                        width: 8
                        height: parent.height
                        x: modelData === "in" ? 0 : parent.width - width
                        color: handle.containsMouse || handle.pressed ? theme.accent : Qt.rgba(1, 1, 1, 0.18)
                        MouseArea {
                            id: handle
                            anchors.fill: parent
                            hoverEnabled: true
                            preventStealing: true
                            cursorShape: Qt.SizeHorCursor
                            property real startX: 0
                            property real startValue: 0
                            onPressed: function(mouse) {
                                backend.checkpoint()
                                root.clipSelected(clipItem.index)
                                startX = mapToItem(flick.contentItem, mouse.x, 0).x
                                startValue = clipItem.modelData[parent.modelData]
                            }
                            onPositionChanged: function(mouse) {
                                if (!pressed) return
                                const dx = mapToItem(flick.contentItem, mouse.x, 0).x - startX
                                backend.setClipValue(clipItem.index, parent.modelData, startValue + dx / root.pps * clipItem.modelData.speed)
                            }
                        }
                    }
                }
            }
        }

        // Captions lane
        Repeater {
            model: backend.captionLines
            delegate: Rectangle {
                required property var modelData
                x: modelData.start * root.pps
                y: root.captionsY
                width: Math.max(3, (modelData.end - modelData.start) * root.pps - 1)
                height: root.laneH
                radius: Math.min(4, theme.radius)
                color: Qt.rgba(1, 1, 1, 0.12)
                clip: true
                Label {
                    anchors.fill: parent
                    anchors.leftMargin: 6
                    anchors.rightMargin: 6
                    visible: parent.width >= 16
                    verticalAlignment: Text.AlignVCenter
                    text: parent.modelData.words.map(w => w.text).join(" ")
                    font.pixelSize: 11
                    elide: Text.ElideRight
                }
                MouseArea {
                    anchors.fill: parent
                    onClicked: { root.captionsSelected(); root.seekRequested(parent.modelData.start) }
                }
            }
        }

        // Text lane
        // Transitions: where two clips overlap, a marker over the overlap.
        Repeater {
            model: backend.clips.length
            delegate: Rectangle {
                required property int index
                readonly property var modelData: backend.clips[index] || ({})
                objectName: "transition" + index
                visible: (modelData.overlap || 0) > 0
                x: (modelData.start + modelData.duration - (modelData.overlap || 0)) * root.pps
                y: root.videoY + root.videoH - 14
                z: 5
                width: Math.max(10, (modelData.overlap || 0) * root.pps)
                height: 12
                radius: 3
                color: theme.accent
                opacity: 0.85
                ToolTip.visible: tip.hovered
                ToolTip.text: (modelData.transitionName || "") + " · " + (modelData.overlap || 0).toFixed(2) + " s"
                HoverHandler { id: tip }
                TapHandler { onTapped: root.clipSelected(parent.index) }
            }
        }

        Repeater {
            model: backend.overlays.length
            delegate: Rectangle {
                id: textBar
                objectName: "textBar" + index
                // Items stay put while their entry changes, so a drag survives edits.
                readonly property var modelData: backend.overlays[index] || ({})
                required property int index
                x: modelData.start * root.pps
                y: root.textY
                width: Math.max(3, (modelData.end - modelData.start) * root.pps - 1)
                height: root.laneH
                radius: Math.min(4, theme.radius)
                color: index === root.selectedOverlay ? theme.accent : Qt.rgba(1, 1, 1, 0.2)
                clip: true
                Label {
                    anchors.fill: parent
                    anchors.leftMargin: 6
                    anchors.rightMargin: 6
                    visible: parent.width >= 16
                    verticalAlignment: Text.AlignVCenter
                    text: parent.modelData.text
                    color: parent.index === root.selectedOverlay ? theme.accentForeground : "white"
                    font.pixelSize: 11
                    elide: Text.ElideRight
                }
                // Drag to move in time; drag an edge to change when it starts or ends.
                MouseArea {
                    anchors.fill: parent
                    preventStealing: true
                    hoverEnabled: true
                    cursorShape: mouseX < 6 || mouseX > width - 6 ? Qt.SizeHorCursor : Qt.OpenHandCursor
                    property string part: "body"
                    property real startX: 0
                    property real startStart: 0
                    property real startEnd: 0
                    property bool moving: false
                    onPressed: function(mouse) {
                        part = mouse.x < 6 ? "start" : mouse.x > width - 6 ? "end" : "body"
                        startX = mapToItem(flick.contentItem, mouse.x, 0).x
                        startStart = textBar.modelData.start
                        startEnd = textBar.modelData.end
                        moving = false
                        root.overlaySelected(textBar.index)
                    }
                    onPositionChanged: function(mouse) {
                        if (!pressed) return
                        const dt = (mapToItem(flick.contentItem, mouse.x, 0).x - startX) / root.pps
                        if (!moving && Math.abs(dt * root.pps) < 3) return
                        if (!moving) {
                            moving = true
                            backend.checkpoint()
                        }
                        if (part === "start") {
                            backend.setOverlayValue(textBar.index, "start", Math.max(0, startStart + dt))
                        } else if (part === "end") {
                            backend.setOverlayValue(textBar.index, "end", startEnd + dt)
                        } else {
                            // Keep its length; set the side moving away first so neither clamps.
                            const shift = Math.max(-startStart, dt)
                            if (shift > 0) {
                                backend.setOverlayValue(textBar.index, "end", startEnd + shift)
                                backend.setOverlayValue(textBar.index, "start", startStart + shift)
                            } else {
                                backend.setOverlayValue(textBar.index, "start", startStart + shift)
                                backend.setOverlayValue(textBar.index, "end", startEnd + shift)
                            }
                        }
                    }
                    onReleased: if (!moving) root.seekRequested(textBar.modelData.start)
                }
            }
        }

        // Effects lane: drag to move, drag an edge to trim, click to select.
        Repeater {
            model: backend.effects.length
            delegate: Rectangle {
                id: effectBar
                objectName: "effectBar" + index
                readonly property var modelData: backend.effects[index] || ({})
                required property int index
                readonly property bool selected: index === root.selectedEffect
                x: modelData.start * root.pps
                y: root.effectsY
                width: Math.max(3, (modelData.end - modelData.start) * root.pps - 1)
                height: root.laneH
                radius: Math.min(4, theme.radius)
                color: selected ? theme.accent : Qt.rgba(0.75, 0.45, 1, 0.45)
                clip: true
                Label {
                    anchors.fill: parent
                    anchors.leftMargin: 6
                    anchors.rightMargin: 6
                    visible: parent.width >= 16
                    verticalAlignment: Text.AlignVCenter
                    text: "✦ " + (effectBar.modelData.label || "")
                    color: effectBar.selected ? theme.accentForeground : "white"
                    font.pixelSize: 11
                    elide: Text.ElideRight
                }
                MouseArea {
                    anchors.fill: parent
                    preventStealing: true
                    hoverEnabled: true
                    cursorShape: mouseX < 6 || mouseX > width - 6 ? Qt.SizeHorCursor : Qt.OpenHandCursor
                    property string part: "body"
                    property real startX: 0
                    property real startStart: 0
                    property real startEnd: 0
                    property bool moving: false
                    onPressed: function(mouse) {
                        part = mouse.x < 6 ? "start" : mouse.x > width - 6 ? "end" : "body"
                        startX = mapToItem(flick.contentItem, mouse.x, 0).x
                        startStart = effectBar.modelData.start
                        startEnd = effectBar.modelData.end
                        moving = false
                        root.effectSelected(effectBar.index)
                    }
                    onPositionChanged: function(mouse) {
                        if (!pressed) return
                        const dt = (mapToItem(flick.contentItem, mouse.x, 0).x - startX) / root.pps
                        if (!moving && Math.abs(dt * root.pps) < 3) return
                        if (!moving) {
                            moving = true
                            backend.checkpoint()
                        }
                        if (part === "start") {
                            backend.setEffectValue(effectBar.index, "start", Math.max(0, startStart + dt))
                        } else if (part === "end") {
                            backend.setEffectValue(effectBar.index, "end", startEnd + dt)
                        } else {
                            // Keep its length; move the side heading away first so neither clamps.
                            const shift = Math.max(-startStart, dt)
                            if (shift > 0) {
                                backend.setEffectValue(effectBar.index, "end", startEnd + shift)
                                backend.setEffectValue(effectBar.index, "start", startStart + shift)
                            } else {
                                backend.setEffectValue(effectBar.index, "start", startStart + shift)
                                backend.setEffectValue(effectBar.index, "end", startEnd + shift)
                            }
                        }
                    }
                    onReleased: if (!moving) root.seekRequested(effectBar.modelData.start)
                }
            }
        }

        // Music lane
        Rectangle {
            visible: backend.music.set
            x: 0
            y: root.musicY
            width: backend.duration * root.pps
            height: root.musicH
            radius: Math.min(4, theme.radius)
            color: Qt.rgba(0.35, 0.55, 1, 0.35)
            clip: true
            Waveform {
                objectName: "musicWaveform"
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 2
                height: root.musicH - 20
                sourcePath: backend.music.set ? backend.music.path : ""
                sourceStart: backend.music.offset || 0
                pixelsPerSecond: root.pps
                viewportStart: flick.contentX
                viewportWidth: flick.width
            }
            Label {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                height: 18
                anchors.leftMargin: 6
                anchors.rightMargin: 6
                verticalAlignment: Text.AlignVCenter
                text: "♪ " + backend.music.name
                font.pixelSize: 11
                elide: Text.ElideRight
            }
            MouseArea { anchors.fill: parent; onClicked: root.musicSelected() }
        }

        // Playhead
        Rectangle {
            x: root.playhead * root.pps - 1
            width: 2
            height: flick.height
            color: theme.accent
            Rectangle {
                width: 12
                height: 12
                rotation: 45
                x: -5
                y: -6
                color: theme.accent
            }
        }
    }
}
