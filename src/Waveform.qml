import QtQuick
import QtQuick.Window

// Visible tiles only. Every zoom level reads native source peaks rather than
// stretching a low-resolution overview. Coordinates remain in source seconds.
Item {
    id: waveform
    property string sourcePath: ""
    property real sourceStart: 0
    property real speed: 1
    property real pixelsPerSecond: 100
    property real viewportStart: 0
    property real viewportWidth: width
    property color waveColor: "#a6caff"
    readonly property int tileWidth: 512
    readonly property int firstTile: Math.max(0, Math.floor(viewportStart / tileWidth) - 1)
    readonly property int endTile: Math.min(Math.ceil(width / tileWidth),
        Math.ceil((viewportStart + viewportWidth) / tileWidth) + 1)
    clip: true

    Rectangle {
        anchors.verticalCenter: parent.verticalCenter
        width: parent.width
        height: 1
        color: waveform.waveColor
        opacity: 0.15
    }
    Repeater {
        model: waveform.visible && waveform.sourcePath.length > 0
            ? Math.max(0, waveform.endTile - waveform.firstTile) : 0
        delegate: Image {
            required property int index
            readonly property int tile: waveform.firstTile + index
            x: tile * waveform.tileWidth
            width: Math.max(0, Math.min(waveform.tileWidth, waveform.width - x))
            height: waveform.height
            asynchronous: true
            cache: false // provider cache includes the file's size and modification time
            smooth: false
            sourceSize.width: Math.max(1, Math.ceil(width * Screen.devicePixelRatio))
            sourceSize.height: Math.max(1, Math.ceil(height * Screen.devicePixelRatio))
            source: width > 0 && height > 0 && waveform.pixelsPerSecond > 0
                ? "image://waveforms/tile?path=" + encodeURIComponent(waveform.sourcePath)
                  + "&start=" + (waveform.sourceStart + x / waveform.pixelsPerSecond * waveform.speed).toFixed(9)
                  + "&duration=" + (width / waveform.pixelsPerSecond * waveform.speed).toFixed(9)
                  + "&color=" + encodeURIComponent(waveform.waveColor.toString()) : ""
        }
    }
}
