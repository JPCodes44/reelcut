import QtQuick
import QtQuick.Effects
import QtMultimedia

// One layer on screen: its video (or photo) in its box over the main track,
// framed inside the box the way framingFilter() frames it, and shown only
// while the layer plays. Its player follows the preview's time, re-seeking
// when it drifts, so it stays in step through seeks, scrubs and playback.
Item {
    id: slot

    property int layerIndex: -1
    readonly property var layerData: layerIndex >= 0 && layerIndex < backend.layers.length ? backend.layers[layerIndex] : null
    // Timeline time and whether the preview is playing.
    property real time: 0
    property bool playing: false
    readonly property bool active: layerData !== null && time >= (layerData.start || 0) && time < (layerData.end || 0)
    // The layer's speed, never 0 or NaN (a playbackRate of 0 or NaN upsets the media backend).
    readonly property real speed: layerData && layerData.speed > 0 ? layerData.speed : 1
    // Where the source should be now, in seconds; always finite.
    readonly property real sourceTime: {
        if (!layerData) return 0
        const t = (layerData.in || 0) + Math.max(0, time - (layerData.start || 0)) * speed
        return isFinite(t) ? Math.max(0, t) : 0
    }

    // The frame this layer sits in.
    property real frameWidth: 0
    property real frameHeight: 0
    readonly property real boxScale: layerData && layerData.scale > 0 ? layerData.scale : 1
    width: frameWidth * boxScale
    height: frameHeight * boxScale
    x: frameWidth * (layerData && isFinite(layerData.x) ? layerData.x : 0.5) - width / 2
    y: frameHeight * (layerData && isFinite(layerData.y) ? layerData.y : 0.5) - height / 2
    visible: active
    clip: true

    // No player for a missing file or an empty path: "file://" alone is no source.
    readonly property bool isVideo: layerData !== null && !layerData.still && !layerData.offline && !!layerData.path
    readonly property string url: layerData && layerData.path ? "file://" + layerData.path : ""

    // The last position asked of the player while paused, so edits that
    // rebuild the layer list (every slider step) don't re-seek it to the
    // same frame over and over.
    property real lastSeekMs: -1

    function sync() {
        if (!isVideo) {
            if (player.playbackState !== MediaPlayer.StoppedState) player.stop()
            lastSeekMs = -1
            return
        }
        if (player.source.toString() !== encodeURI(url) && player.source.toString() !== url) {
            player.source = url
            lastSeekMs = -1
        }
        if (Math.abs(player.playbackRate - speed) > 1e-6)
            player.playbackRate = speed
        if (!active) {
            if (player.playbackState === MediaPlayer.PlayingState) player.pause()
            lastSeekMs = -1
            return
        }
        const ms = Math.round(sourceTime * 1000)
        if (playing) {
            // Small drift is left alone while playing; seeking every tick stutters.
            if (Math.abs(player.position / 1000 - sourceTime) > 0.25)
                player.position = ms
            lastSeekMs = -1
        } else if (ms !== lastSeekMs) {
            player.position = ms
            lastSeekMs = ms
        }
        if (playing && player.playbackState !== MediaPlayer.PlayingState) player.play()
        if (!playing && player.playbackState === MediaPlayer.PlayingState) player.pause()
    }
    onTimeChanged: sync()
    onPlayingChanged: sync()
    onActiveChanged: sync()
    // Each project change rebuilds backend.layers; fold the burst into one sync.
    onLayerDataChanged: Qt.callLater(slot.sync)
    Component.onCompleted: sync()
    // Stop the player before it goes, rather than tearing it down mid-play.
    Component.onDestruction: player.stop()

    MediaPlayer {
        id: player
        videoOutput: video
        audioOutput: AudioOutput { volume: slot.layerData && slot.layerData.audio ? 1 : 0 }
        onMediaStatusChanged: if (mediaStatus === MediaPlayer.LoadedMedia) { slot.lastSeekMs = -1; slot.sync() }
    }

    // The look sliders, as the clip slots draw them.
    Item {
        anchors.fill: parent
        // No offscreen texture while hidden or empty (a 0×0 layer is invalid).
        layer.enabled: slot.visible && slot.width >= 1 && slot.height >= 1
        layer.effect: MultiEffect {
            brightness: backend.look.exposure * 0.25
            contrast: backend.look.contrast * 0.5
            saturation: backend.look.saturation
        }

        Item {
            id: framed
            readonly property var c: slot.layerData
            readonly property real srcW: c && c.width > 0 ? c.width : 1080
            readonly property real srcH: c && c.height > 0 ? c.height : 1920
            // Same math as framingFilter(): cover the box, zoom, then pan the crop.
            readonly property real s: Math.max(slot.width / srcW, slot.height / srcH) * (c && c.zoom > 0 ? c.zoom : 1)
            width: srcW * s
            height: srcH * s
            x: -(width - slot.width) * (((c && c.panX) || 0) + 1) / 2
            y: -(height - slot.height) * (((c && c.panY) || 0) + 1) / 2

            VideoOutput {
                id: video
                anchors.fill: parent
                fillMode: VideoOutput.Stretch
                visible: slot.isVideo
            }
            Image {
                anchors.fill: parent
                visible: framed.c !== null && framed.c.still && !!framed.c.path
                source: visible ? "file://" + framed.c.path : ""
                fillMode: Image.Stretch
                asynchronous: true
                autoTransform: true
                sourceSize.width: 2160
            }
        }
    }
}
