import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Effects
import QtQuick.Layouts
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
    signal timeAdvanced(real t)

    function seek(t) {
        const at = backend.locate(t)
        clipIndex = at.index
        if (at.index < 0) {
            player.stop()
            player.source = ""
            return
        }
        load(backend.clips[at.index].path, at.source)
        music.sync(t)
    }
    function refresh() { seek(time) }
    function load(path, sourceSeconds) {
        const url = "file://" + path
        player.playbackRate = currentClip ? currentClip.speed : 1
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
        player.play()
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
            const source = player.position / 1000
            const t = c.start + Math.max(0, source - c.in) / c.speed
            if (source >= c.out - 0.02 || player.mediaStatus === MediaPlayer.EndOfMedia) {
                const next = root.clipIndex + 1
                if (next >= backend.clips.length) {
                    root.pause()
                    root.timeAdvanced(backend.duration)
                    return
                }
                root.clipIndex = next
                root.load(backend.clips[next].path, backend.clips[next].in)
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

                    VideoOutput {
                        id: video
                        anchors.fill: parent
                        fillMode: VideoOutput.Stretch
                        layer.enabled: true
                        layer.effect: MultiEffect {
                            brightness: backend.look.exposure * 0.25
                            contrast: backend.look.contrast * 0.5
                            saturation: backend.look.saturation
                        }
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
                    scaleFactor: frame.scaleFactor
                }

                // Where the app's own buttons and text sit over the video.
                Item {
                    anchors.fill: parent
                    visible: root.safeZones && backend.clips.length > 0
                    readonly property bool tiktok: backend.platform === "tiktok"
                    readonly property real topBand: (tiktok ? 160 : 220) * frame.scaleFactor
                    readonly property real bottomBand: (tiktok ? 480 : 420) * frame.scaleFactor
                    readonly property real rightBand: (tiktok ? 150 : 130) * frame.scaleFactor
                    Rectangle { width: parent.width; height: parent.topBand; color: Qt.rgba(1, 0.3, 0.3, 0.13) }
                    Rectangle { width: parent.width; height: parent.bottomBand; anchors.bottom: parent.bottom; color: Qt.rgba(1, 0.3, 0.3, 0.13) }
                    Rectangle {
                        width: parent.rightBand
                        anchors.right: parent.right
                        y: parent.topBand
                        height: parent.height - parent.topBand - parent.bottomBand
                        color: Qt.rgba(1, 0.3, 0.3, 0.13)
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
                    text: qsTr("Drop videos here to start a reel")
                    font.pixelSize: 20
                }
                Button {
                    Layout.alignment: Qt.AlignHCenter
                    text: qsTr("Add Clips")
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
