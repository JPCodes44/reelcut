import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Effects
import QtQuick.Layouts
import QtQuick.Shapes
import QtMultimedia

// Plays the edit: one player hops from clip to clip, at each clip's speed,
// framed exactly as the export frames it. The look is approximated live; the
// LUT only applies on export.
Rectangle {
    id: root
    color: "#0b0b0d"

    property real time: 0
    property bool safeZones: true
    property bool playing: false
    property int clipIndex: -1
    property var currentClip: clipIndex >= 0 && clipIndex < backend.clips.length ? backend.clips[clipIndex] : null
    // Where to seek once a newly set source has loaded, in milliseconds.
    property real pendingSeek: -1
    // Wall-clock time of the last tick, for advancing through photos.
    property real lastTick: 0
    signal timeAdvanced(real t)

    function seek(t) {
        const at = backend.locate(t)
        clipIndex = at.index
        if (at.index < 0) {
            player.stop()
            player.source = ""
            return
        }
        load(backend.clips[at.index], at.source)
        music.sync(t)
    }
    function refresh() { seek(time) }
    function load(c, sourceSeconds) {
        // Photos have no player; the timer moves through them by the clock.
        if (c.still) {
            pendingSeek = -1
            player.stop()
            player.source = ""
            lastTick = Date.now()
            return
        }
        const url = "file://" + c.path
        player.playbackRate = c.speed
        if (player.source.toString() !== encodeURI(url) && player.source.toString() !== url) {
            pendingSeek = sourceSeconds * 1000
            player.source = url
            if (playing) player.play(); else player.pause()
        } else {
            player.position = sourceSeconds * 1000
        }
    }
    function toggle() { playing ? pause() : play() }
    function play() {
        if (backend.clips.length === 0) return
        if (time >= backend.duration - 0.05) { timeAdvanced(0); seek(0) }
        playing = true
        lastTick = Date.now()
        if (!(currentClip && currentClip.still)) player.play()
        music.sync(time)
    }
    function pause() {
        playing = false
        player.pause()
        music.pause()
    }

    MediaPlayer {
        id: player
        videoOutput: video
        audioOutput: AudioOutput { volume: 1.0 }
        onMediaStatusChanged: {
            if ((mediaStatus === MediaPlayer.LoadedMedia || mediaStatus === MediaPlayer.BufferedMedia) && root.pendingSeek >= 0) {
                position = root.pendingSeek
                root.pendingSeek = -1
                if (root.playing) play(); else pause()
            }
        }
    }

    MediaPlayer {
        id: music
        source: backend.music.set ? "file://" + backend.music.path : ""
        audioOutput: AudioOutput { volume: backend.music.volume }
        function sync(t) {
            if (!backend.music.set) return
            position = (backend.music.offset + t) * 1000
            if (root.playing) play()
        }
    }

    // Follows the player while playing and moves on at each clip's out point.
    Timer {
        interval: 16
        repeat: true
        running: root.playing
        onTriggered: {
            const c = root.currentClip
            if (!c || root.pendingSeek >= 0) return
            const now = Date.now()
            let t, done
            if (c.still) {
                t = root.time + (now - root.lastTick) / 1000
                done = t >= c.start + c.duration
            } else {
                const source = player.position / 1000
                t = c.start + Math.max(0, source - c.in) / c.speed
                done = source >= c.out - 0.02 || player.mediaStatus === MediaPlayer.EndOfMedia
            }
            root.lastTick = now
            if (done) {
                const next = root.clipIndex + 1
                if (next >= backend.clips.length) {
                    root.pause()
                    root.timeAdvanced(backend.duration)
                    return
                }
                root.clipIndex = next
                root.load(backend.clips[next], backend.clips[next].in)
                if (!backend.clips[next].still) player.play()
                root.timeAdvanced(backend.clips[next].start)
                return
            }
            root.timeAdvanced(Math.min(t, backend.duration))
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 10

        Item {
            id: stage
            Layout.fillWidth: true
            Layout.fillHeight: true

            // The 9:16 frame, as large as fits.
            Rectangle {
                id: frame
                readonly property real scaleFactor: height / 1920
                height: Math.min(stage.height, stage.width * 16 / 9)
                width: height * 9 / 16
                anchors.centerIn: parent
                color: "black"
                clip: true

                // A photo's slow zoom or pan, the same moves as motionFilter().
                Item {
                    id: mover
                    width: frame.width
                    height: frame.height
                    readonly property var c: root.currentClip
                    readonly property string motion: c && c.still ? c.motion : "none"
                    readonly property real p: c && c.duration > 0 ? Math.max(0, Math.min(1, (root.time - c.start) / c.duration)) : 0
                    readonly property real zoomLevel: motion === "zoom-in" ? 1 + 0.12 * p
                                            : motion === "zoom-out" ? 1.12 - 0.12 * p
                                            : motion === "pan-left" || motion === "pan-right" ? 1.12 : 1
                    readonly property real across: motion === "pan-right" ? p : motion === "pan-left" ? 1 - p : 0.5
                    transform: [
                        Scale { xScale: mover.zoomLevel; yScale: mover.zoomLevel },
                        Translate { x: -(mover.zoomLevel - 1) * frame.width * mover.across; y: -(mover.zoomLevel - 1) * frame.height / 2 }
                    ]

                    Item {
                        id: framed
                        readonly property var c: root.currentClip
                        readonly property real srcW: c && c.width > 0 ? c.width : 1080
                        readonly property real srcH: c && c.height > 0 ? c.height : 1920
                        // Same math as framingFilter(): cover, zoom, then pan the crop.
                        readonly property real s: Math.max(frame.width / srcW, frame.height / srcH) * (c ? c.zoom : 1)
                        width: srcW * s
                        height: srcH * s
                        x: -(width - frame.width) * ((c ? c.panX : 0) + 1) / 2
                        y: -(height - frame.height) * ((c ? c.panY : 0) + 1) / 2
                        layer.enabled: true
                        layer.effect: MultiEffect {
                            brightness: backend.look.exposure * 0.25
                            contrast: backend.look.contrast * 0.5
                            saturation: backend.look.saturation
                        }

                        VideoOutput {
                            id: video
                            anchors.fill: parent
                            fillMode: VideoOutput.Stretch
                            visible: !(framed.c && framed.c.still)
                        }
                        Image {
                            anchors.fill: parent
                            visible: framed.c !== null && framed.c.still
                            source: visible ? "file://" + framed.c.path : ""
                            fillMode: Image.Stretch
                            asynchronous: true
                            autoTransform: true
                            sourceSize.width: 2160
                        }
                    }
                }

                // The picture itself: drag to pan the framing, corners or wheel to zoom.
                EditBox {
                    objectName: "clipBox"
                    anchors.fill: parent
                    handlesInside: true
                    visible: root.currentClip !== null
                    selected: win.selectionKind === "clip" && win.selectedClip === root.clipIndex
                    property real startPanX
                    property real startPanY
                    property real startZoom
                    property real spanX
                    property real spanY
                    onPressed: {
                        win.select("clip", root.clipIndex)
                        startPanX = root.currentClip.panX
                        startPanY = root.currentClip.panY
                        startZoom = root.currentClip.zoom
                        // How far a pan of 1 moves the picture, as framed now.
                        spanX = (framed.width - frame.width) / 2
                        spanY = (framed.height - frame.height) / 2
                    }
                    onBegan: backend.checkpoint()
                    onDragged: function(dx, dy) {
                        if (spanX > 0.5) backend.setClipValue(root.clipIndex, "panX", startPanX - dx / spanX)
                        if (spanY > 0.5) backend.setClipValue(root.clipIndex, "panY", startPanY - dy / spanY)
                    }
                    onResized: function(factor) { backend.setClipValue(root.clipIndex, "zoom", startZoom * factor) }
                    onWheeled: function(steps) {
                        backend.setClipValue(root.clipIndex, "zoom", root.currentClip.zoom * Math.pow(1.05, steps))
                    }
                }

                // Temperature, as a light tint over the picture.
                Rectangle {
                    anchors.fill: parent
                    color: backend.look.temperature > 0 ? "#ff8a1e" : "#1e78ff"
                    opacity: Math.abs(backend.look.temperature) * 0.14
                }

                CaptionLayer {
                    anchors.fill: parent
                    time: root.time
                    animate: root.playing
                    scaleFactor: frame.scaleFactor
                }

                // Where the app's own buttons and text don't cover the video:
                // a dashed outline only, so the picture itself stays untouched.
                Shape {
                    id: safeArea
                    anchors.fill: parent
                    visible: root.safeZones && backend.clips.length > 0
                    readonly property bool tiktok: backend.platform === "tiktok"
                    readonly property real topEdge: (tiktok ? 160 : 220) * frame.scaleFactor
                    readonly property real bottomEdge: height - (tiktok ? 480 : 420) * frame.scaleFactor
                    readonly property real leftEdge: 40 * frame.scaleFactor
                    readonly property real rightEdge: width - (tiktok ? 150 : 130) * frame.scaleFactor
                    ShapePath {
                        strokeColor: Qt.rgba(1, 1, 1, 0.7)
                        strokeWidth: 1
                        strokeStyle: ShapePath.DashLine
                        dashPattern: [4, 4]
                        fillColor: "transparent"
                        startX: safeArea.leftEdge; startY: safeArea.topEdge
                        PathLine { x: safeArea.rightEdge; y: safeArea.topEdge }
                        PathLine { x: safeArea.rightEdge; y: safeArea.bottomEdge }
                        PathLine { x: safeArea.leftEdge; y: safeArea.bottomEdge }
                        PathLine { x: safeArea.leftEdge; y: safeArea.topEdge }
                    }
                }

                Label {
                    anchors.top: parent.top
                    anchors.right: parent.right
                    anchors.margins: 8
                    visible: backend.look.lut.length > 0
                    text: qsTr("LUT on export")
                    font.pixelSize: 11
                    padding: 4
                    background: Rectangle { color: Qt.rgba(0, 0, 0, 0.6); radius: Math.min(4, theme.radius) }
                }

                Label {
                    anchors.centerIn: parent
                    visible: root.currentClip !== null && root.currentClip.offline
                    text: qsTr("Missing file:\n%1").arg(root.currentClip ? root.currentClip.name : "")
                    horizontalAlignment: Text.AlignHCenter
                    color: "#ff8a65"
                }
            }

            // Empty state
            ColumnLayout {
                anchors.centerIn: parent
                visible: backend.clips.length === 0
                spacing: 14
                Label {
                    Layout.alignment: Qt.AlignHCenter
                    text: qsTr("Drop videos or photos here to start a reel")
                    font.pixelSize: 20
                }
                Button {
                    Layout.alignment: Qt.AlignHCenter
                    text: qsTr("Add Videos or Photos")
                    highlighted: true
                    Material.foreground: theme.accentForeground
                    onClicked: backend.addMedia()
                }
                Label {
                    Layout.alignment: Qt.AlignHCenter
                    text: qsTr("Press ? for shortcuts")
                    color: "#9a9aa2"
                }
            }
        }

        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            spacing: 12
            ToolButton {
                text: root.playing ? "❚❚" : "▶"
                font.pixelSize: 18
                enabled: backend.clips.length > 0
                onClicked: root.toggle()
            }
            Label {
                text: win.timeText(root.time) + " / " + win.timeText(backend.duration)
                font.family: "monospace"
                color: "#c8c8cf"
            }
            ToolButton {
                text: qsTr("Safe zones")
                checkable: true
                checked: root.safeZones
                onToggled: win.safeZones = checked
                font.pixelSize: 12
            }
        }
    }
}
