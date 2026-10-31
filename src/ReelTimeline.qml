import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

// The edit laid out in time: clips with filmstrips and trim handles, then
// lanes for captions, text overlays and music. Ctrl+scroll zooms.
Rectangle {
    id: root
    color: "#121215"

    property real playhead: 0
    property int selectedClip: -1
    property int selectedOverlay: -1
    property real zoom: 1
    readonly property real gutter: 74
    readonly property real fitPps: Math.max(8, (width - gutter - 24) / Math.max(backend.duration, 5))
    readonly property real pps: fitPps * zoom
    readonly property real rulerH: 22
    readonly property real videoH: 76
    readonly property real laneH: 28

    // Keep the playhead in view while playing or stepping.
    onPlayheadChanged: {
        const x = playhead * pps
        if (x < flick.contentX || x > flick.contentX + flick.width - 20)
            flick.contentX = Math.max(0, Math.min(x - flick.width * 0.3, flick.contentWidth - flick.width))
    }

    signal seekRequested(real t)
    signal clipSelected(int index)
    signal overlaySelected(int index)
    signal captionsSelected()
    signal musicSelected()

    // Lane names stay put while the lanes scroll.
    Column {
        x: 0
        y: root.rulerH + 6
        width: root.gutter
        spacing: 6
        Repeater {
            model: [{ label: qsTr("Video"), h: root.videoH }, { label: qsTr("Captions"), h: root.laneH },
                    { label: qsTr("Text"), h: root.laneH }, { label: qsTr("Music"), h: root.laneH }]
            delegate: Label {
                required property var modelData
                width: root.gutter
                height: modelData.h
                text: modelData.label
                color: "#8a8a92"
                font.pixelSize: 12
                leftPadding: 12
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
            model: backend.clips
            delegate: Rectangle {
                id: clipItem
                required property var modelData
                required property int index
                readonly property bool selected: index === root.selectedClip
                x: modelData.start * root.pps
                y: root.rulerH + 6
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

                Label {
                    anchors.left: parent.left
                    anchors.bottom: parent.bottom
                    anchors.margins: 6
                    text: clipItem.modelData.still ? "▣" : clipItem.modelData.speed !== 1 ? clipItem.modelData.speed + "×" : ""
                    font.pixelSize: 11
                    font.bold: true
                    padding: 2
                    visible: text.length > 0
                    background: Rectangle { color: Qt.rgba(0, 0, 0, 0.6); radius: 2 }
                }

                MouseArea {
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    anchors.rightMargin: 8
                    onPressed: function(mouse) {
                        root.clipSelected(clipItem.index)
                        root.seekRequested(clipItem.modelData.start + mouse.x / root.pps)
                    }
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
                y: root.rulerH + 6 + root.videoH + 6
                width: Math.max(3, (modelData.end - modelData.start) * root.pps - 1)
                height: root.laneH
                radius: Math.min(4, theme.radius)
                color: Qt.rgba(1, 1, 1, 0.12)
                clip: true
                Label {
                    anchors.fill: parent
                    anchors.leftMargin: 4
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
        Repeater {
            model: backend.overlays
            delegate: Rectangle {
                required property var modelData
                required property int index
                x: modelData.start * root.pps
                y: root.rulerH + 6 + root.videoH + 6 + root.laneH + 6
                width: Math.max(3, (modelData.end - modelData.start) * root.pps - 1)
                height: root.laneH
                radius: Math.min(4, theme.radius)
                color: index === root.selectedOverlay ? theme.accent : Qt.rgba(1, 1, 1, 0.2)
                clip: true
                Label {
                    anchors.fill: parent
                    anchors.leftMargin: 4
                    verticalAlignment: Text.AlignVCenter
                    text: parent.modelData.text
                    color: parent.index === root.selectedOverlay ? theme.accentForeground : "white"
                    font.pixelSize: 11
                    elide: Text.ElideRight
                }
                MouseArea {
                    anchors.fill: parent
                    onClicked: { root.overlaySelected(parent.index); root.seekRequested(parent.modelData.start) }
                }
            }
        }

        // Music lane
        Rectangle {
            visible: backend.music.set
            x: 0
            y: root.rulerH + 6 + root.videoH + 6 + (root.laneH + 6) * 2
            width: backend.duration * root.pps
            height: root.laneH
            radius: Math.min(4, theme.radius)
            color: Qt.rgba(0.35, 0.55, 1, 0.35)
            Label {
                anchors.fill: parent
                anchors.leftMargin: 6
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

    Label {
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 8
        text: qsTr("Ctrl+scroll to zoom")
        font.pixelSize: 11
        color: "#5e5e66"
    }
}
