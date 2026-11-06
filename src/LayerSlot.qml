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
    readonly property bool active: layerData !== null && time >= layerData.start && time < layerData.end
    // Where the source should be now, in seconds.
    readonly property real sourceTime: layerData ? layerData.in + Math.max(0, time - layerData.start) * layerData.speed : 0

    // The frame this layer sits in.
    property real frameWidth: 0
    property real frameHeight: 0
    width: frameWidth * (layerData ? layerData.scale : 1)
    height: frameHeight * (layerData ? layerData.scale : 1)
    x: frameWidth * (layerData ? layerData.x : 0.5) - width / 2
    y: frameHeight * (layerData ? layerData.y : 0.5) - height / 2
    visible: active
    clip: true

    readonly property bool isVideo: layerData !== null && !layerData.still
    readonly property string url: layerData ? "file://" + layerData.path : ""

    function sync() {
        if (!isVideo) {
            player.stop()
            return
        }
        if (player.source.toString() !== encodeURI(url) && player.source.toString() !== url)
            player.source = url
        player.playbackRate = layerData.speed
        if (!active) {
            if (player.playbackState === MediaPlayer.PlayingState) player.pause()
            return
        }
        // Small drift is left alone while playing; seeking every tick stutters.
        if (!playing || Math.abs(player.position / 1000 - sourceTime) > 0.25)
            player.position = sourceTime * 1000
        if (playing && player.playbackState !== MediaPlayer.PlayingState) player.play()
        if (!playing && player.playbackState === MediaPlayer.PlayingState) player.pause()
    }
    onTimeChanged: sync()
    onPlayingChanged: sync()
    onActiveChanged: sync()
    onLayerDataChanged: sync()
    Component.onCompleted: sync()

    MediaPlayer {
        id: player
        videoOutput: video
        audioOutput: AudioOutput { volume: slot.layerData && slot.layerData.audio ? 1 : 0 }
        onMediaStatusChanged: if (mediaStatus === MediaPlayer.LoadedMedia) slot.sync()
    }

    // The look sliders, as the clip slots draw them.
    Item {
        anchors.fill: parent
        layer.enabled: true
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
            readonly property real s: Math.max(slot.width / srcW, slot.height / srcH) * (c ? c.zoom : 1)
            width: srcW * s
            height: srcH * s
            x: -(width - slot.width) * ((c ? c.panX : 0) + 1) / 2
            y: -(height - slot.height) * ((c ? c.panY : 0) + 1) / 2

            VideoOutput {
                id: video
                anchors.fill: parent
                fillMode: VideoOutput.Stretch
                visible: slot.isVideo
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
