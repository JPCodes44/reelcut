import QtQuick

// Captions and text overlays on the preview, drawn like the burned-in .ass
// file draws them: same font, sizes at 1080x1920 scaled to the frame.
Item {
    id: root
    property real time: 0
    property real scaleFactor: 1
    // Frame scale times the ASS-to-Qt font size conversion.
    readonly property real fontScale: scaleFactor * backend.captionScale

    readonly property var line: {
        const lines = backend.captionLines
        for (let i = 0; i < lines.length; ++i)
            if (time >= lines[i].start && time < lines[i].end)
                return lines[i]
        return null
    }
    readonly property int spoken: {
        if (!line) return -1
        let current = 0
        for (let i = 0; i < line.words.length; ++i)
            if (time >= line.words[i].start) current = i
        return current
    }
    readonly property string style: backend.captionStyle

    // Highlight and bold: the whole line, upper case.
    Row {
        id: words
        visible: root.line !== null && (root.style === "highlight" || root.style === "bold")
        anchors.horizontalCenter: parent.horizontalCenter
        y: backend.captionY * root.height - height / 2
        spacing: 22 * root.scaleFactor
        Repeater {
            model: root.line ? root.line.words : []
            delegate: Text {
                required property var modelData
                required property int index
                text: modelData.text.toUpperCase()
                font.family: backend.captionFont
                font.bold: true
                font.pixelSize: Math.max(1, Math.round(84 * root.fontScale))
                color: root.style === "highlight" && index === root.spoken ? theme.accent : "white"
                style: Text.Outline
                styleColor: "black"
            }
        }
    }

    // Pop: the spoken word alone, growing in.
    Text {
        id: popWord
        visible: root.line !== null && root.style === "pop"
        readonly property var word: root.line && root.spoken >= 0 ? root.line.words[root.spoken] : null
        text: word ? word.text.toUpperCase() : ""
        anchors.horizontalCenter: parent.horizontalCenter
        y: backend.captionY * root.height - height / 2
        font.family: backend.captionFont
        font.bold: true
        font.pixelSize: Math.max(1, Math.round(84 * root.fontScale))
        color: "white"
        style: Text.Outline
        styleColor: "black"
        scale: word ? Math.min(1, 0.7 + 0.3 * (root.time - word.start) / 0.09) : 1
    }

    // Box: plain case on a dark box.
    Rectangle {
        visible: root.line !== null && root.style === "box"
        anchors.horizontalCenter: parent.horizontalCenter
        y: backend.captionY * root.height - height / 2
        width: boxText.implicitWidth + 28 * root.scaleFactor
        height: boxText.implicitHeight + 20 * root.scaleFactor
        color: Qt.rgba(0.07, 0.07, 0.07, 0.87)
        Text {
            id: boxText
            anchors.centerIn: parent
            text: root.line ? root.line.words.map(w => w.text).join(" ") : ""
            font.family: backend.captionFont
            font.bold: true
            font.pixelSize: Math.max(1, Math.round(76 * root.fontScale))
            color: "white"
        }
    }

    // Text overlays, with their entrance animation.
    Repeater {
        model: backend.overlays
        delegate: Text {
            required property var modelData
            readonly property real age: root.time - modelData.start
            readonly property real entrance: Math.min(1, Math.max(0, age / 0.25))
            visible: root.time >= modelData.start && root.time < modelData.end && modelData.text.length > 0
            text: modelData.text
            width: root.width * 0.94
            x: (root.width - width) / 2
            y: modelData.y * root.height - height / 2
               + (modelData.animation === "slide" ? (1 - entrance) * 160 * root.scaleFactor : 0)
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            font.family: backend.captionFont
            font.bold: true
            font.pixelSize: Math.max(1, Math.round(modelData.size * root.fontScale))
            color: "white"
            style: Text.Outline
            styleColor: "black"
            opacity: modelData.animation === "fade" || modelData.animation === "slide" ? entrance : 1
            scale: modelData.animation === "pop" ? 0.4 + 0.6 * Math.min(1, age / 0.18) : 1
        }
    }
}
