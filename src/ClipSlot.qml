import QtQuick
import QtQuick.Effects
import QtMultimedia

// One clip on screen: its player (or photo), framed exactly as the export
// frames it, with a photo's slow move. The preview keeps two of these so the
// next clip is loaded before it's needed and both can show during a
// transition.
Item {
    id: slot

    property int index: -1
    readonly property var clipData: index >= 0 && index < backend.clips.length ? backend.clips[index] : null
    // Timeline time, for a photo's move.
    property real time: 0
    property bool playing: false
    property real volume: 1.0
    // The framed picture, for the drag-to-pan math.
    readonly property alias framedItem: framed
    readonly property alias player: mediaPlayer
    // Where to seek once a newly set source has loaded, in milliseconds.
    property real pendingSeek: -1
    readonly property bool loading: pendingSeek >= 0

    // Shows clip `i` at `sourceSeconds` into its source; plays if `play`.
    function show(i, sourceSeconds, play) {
        index = i
        playing = play
        const c = clipData
        syncMask(c ? (sourceSeconds - c.in) / c.speed : 0, play)
        if (!c || c.still) {
            pendingSeek = -1
            mediaPlayer.stop()
            mediaPlayer.source = ""
            return
        }
        const url = "file://" + c.path
        mediaPlayer.playbackRate = c.speed
        if (mediaPlayer.source.toString() !== encodeURI(url) && mediaPlayer.source.toString() !== url) {
            pendingSeek = sourceSeconds * 1000
            mediaPlayer.source = url
            if (play) mediaPlayer.play(); else mediaPlayer.pause()
        } else {
            mediaPlayer.position = sourceSeconds * 1000
            if (play) mediaPlayer.play(); else mediaPlayer.pause()
        }
    }
    function pause() {
        playing = false
        mediaPlayer.pause()
        maskPlayer.pause()
    }
    // The person mask is a video in timeline time, starting with the clip.
    function syncMask(localSeconds, play) {
        const url = clipData && clipData.mask ? clipData.mask : ""
        if (maskPlayer.source.toString() !== url)
            maskPlayer.source = url
        if (url === "") return
        maskPlayer.position = Math.max(0, localSeconds) * 1000
        if (play) maskPlayer.play(); else maskPlayer.pause()
    }
    function clear() {
        index = -1
        playing = false
        pendingSeek = -1
        mediaPlayer.stop()
        mediaPlayer.source = ""
        maskPlayer.stop()
        maskPlayer.source = ""
    }
    // Timeline time this slot's clip has reached, from its player.
    function reached() {
        const c = clipData
        if (!c || c.still) return -1
        return c.start + Math.max(0, mediaPlayer.position / 1000 - c.in) / c.speed
    }
    function finished() {
        const c = clipData
        return c && !c.still && (mediaPlayer.position / 1000 >= c.out - 0.02 || mediaPlayer.mediaStatus === MediaPlayer.EndOfMedia)
    }

    MediaPlayer {
        id: mediaPlayer
        videoOutput: video
        audioOutput: AudioOutput { volume: slot.volume }
        onMediaStatusChanged: {
            if ((mediaStatus === MediaPlayer.LoadedMedia || mediaStatus === MediaPlayer.BufferedMedia) && slot.pendingSeek >= 0) {
                position = slot.pendingSeek
                slot.pendingSeek = -1
                if (slot.playing) play(); else pause()
            }
        }
    }

    MediaPlayer {
        id: maskPlayer
        videoOutput: maskVideo
    }
    // The mask frames, drawn only into the body shader's texture.
    VideoOutput {
        id: maskVideo
        anchors.fill: parent
        fillMode: VideoOutput.Stretch
    }

    // The clip's picture, with its body effect when there is one and its
    // person mask is ready (src/shaders/body.frag).
    Item {
        id: bodyLayer
        anchors.fill: parent
        readonly property bool active: slot.clipData !== null && slot.clipData.bodyMode >= 0 && slot.clipData.mask !== ""
        layer.enabled: active
        layer.effect: ShaderEffect {
            property variant mask: ShaderEffectSource {
                sourceItem: maskVideo
                hideSource: true
                live: true
            }
            property real mode: slot.clipData ? slot.clipData.bodyMode : -1
            property real amount: slot.clipData ? slot.clipData.bodyAmount : 0
            property real aspect: slot.width / Math.max(1, slot.height)
            fragmentShader: "qrc:/shaders/body.frag.qsb"
        }

        // A photo's slow zoom or pan, the same moves as motionFilter().
        Item {
            id: mover
            width: slot.width
            height: slot.height
            readonly property var c: slot.clipData
            readonly property string motion: c && c.still ? c.motion : "none"
            readonly property real p: c && c.duration > 0 ? Math.max(0, Math.min(1, (slot.time - c.start) / c.duration)) : 0
            readonly property real zoomLevel: motion === "zoom-in" ? 1 + 0.12 * p
                                    : motion === "zoom-out" ? 1.12 - 0.12 * p
                                    : motion === "pan-left" || motion === "pan-right" ? 1.12 : 1
            readonly property real across: motion === "pan-right" ? p : motion === "pan-left" ? 1 - p : 0.5
            layer.enabled: true
            layer.effect: MultiEffect {
                brightness: backend.look.exposure * 0.25
                contrast: backend.look.contrast * 0.5
                saturation: backend.look.saturation
            }
            transform: [
                Scale { xScale: mover.zoomLevel; yScale: mover.zoomLevel },
                Translate { x: -(mover.zoomLevel - 1) * slot.width * mover.across; y: -(mover.zoomLevel - 1) * slot.height / 2 }
            ]

            Item {
                id: framed
                readonly property var c: slot.clipData
                readonly property real srcW: c && c.width > 0 ? c.width : 1080
                readonly property real srcH: c && c.height > 0 ? c.height : 1920
                // Same math as framingFilter(): cover, zoom, then pan the crop.
                readonly property real s: Math.max(slot.width / srcW, slot.height / srcH) * (c ? c.zoom : 1)
                width: srcW * s
                height: srcH * s
                x: -(width - slot.width) * ((c ? c.panX : 0) + 1) / 2
                y: -(height - slot.height) * ((c ? c.panY : 0) + 1) / 2
                // This clip's color wheels; the look sliders apply after, on mover.
                layer.enabled: framed.c !== null && framed.c.grade !== undefined && !framed.c.grade.neutral
                layer.effect: GradeEffect { grade: framed.c ? framed.c.grade : null }

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
    }
}
