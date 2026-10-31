import QtQuick

// Captions and text overlays on the preview, drawn like the burned-in .ass
// file draws them: same font, sizes at 1080x1920 scaled to the frame. Each
// can be dragged to move and resized from its corners or with the wheel.
Item {
    id: root
    property real time: 0
    property real scaleFactor: 1
    property bool editable: true
    // Frame scale times the ASS-to-Qt font size conversion.
    readonly property real fontScale: scaleFactor * backend.captionScale
    readonly property real captionPx: Math.max(1, Math.round(84 * fontScale * backend.captionSize))

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

    // ---- Captions ----
    Item {
        id: captionArea
        readonly property Item shown: root.style === "pop" ? popWord : root.style === "box" ? boxed : words
        visible: root.line !== null
        width: shown.width
        height: shown.height
        x: backend.captionX * root.width - width / 2
        y: backend.captionY * root.height - height / 2

        // Highlight and bold: the whole line, upper case.
        Row {
            id: words
            visible: root.style === "highlight" || root.style === "bold"
            anchors.centerIn: parent
            spacing: 22 * root.scaleFactor * backend.captionSize
            Repeater {
                model: root.line ? root.line.words : []
                delegate: Text {
                    required property var modelData
                    required property int index
                    text: modelData.text.toUpperCase()
                    font.family: backend.captionFont
                    font.bold: true
                    font.pixelSize: root.captionPx
                    color: root.style === "highlight" && index === root.spoken ? theme.accent : "white"
                    style: Text.Outline
                    styleColor: "black"
                }
            }
        }

        // Pop: the spoken word alone, growing in.
        Text {
            id: popWord
            visible: root.style === "pop"
            anchors.centerIn: parent
            readonly property var word: root.line && root.spoken >= 0 ? root.line.words[root.spoken] : null
            text: word ? word.text.toUpperCase() : ""
            font.family: backend.captionFont
            font.bold: true
            font.pixelSize: root.captionPx
            color: "white"
            style: Text.Outline
            styleColor: "black"
            scale: word ? Math.min(1, 0.7 + 0.3 * (root.time - word.start) / 0.09) : 1
        }

        // Box: plain case on a dark box.
        Rectangle {
            id: boxed
            visible: root.style === "box"
            anchors.centerIn: parent
            width: boxText.implicitWidth + 28 * root.scaleFactor * backend.captionSize
            height: boxText.implicitHeight + 20 * root.scaleFactor * backend.captionSize
            color: Qt.rgba(0.07, 0.07, 0.07, 0.87)
            Text {
                id: boxText
                anchors.centerIn: parent
                text: root.line ? root.line.words.map(w => w.text).join(" ") : ""
                font.family: backend.captionFont
                font.bold: true
                font.pixelSize: Math.max(1, Math.round(76 * root.fontScale * backend.captionSize))
                color: "white"
            }
        }

        EditBox {
            objectName: "captionBox"
            anchors.fill: parent
            anchors.margins: -6
            visible: root.editable
            selected: win.selectionKind === "captions"
            property real startX
            property real startY
            property real startSize
            onPressed: {
                win.select("captions", -1)
                startX = backend.captionX
                startY = backend.captionY
                startSize = backend.captionSize
            }
            onBegan: backend.checkpoint()
            onDragged: function(dx, dy) {
                backend.setCaptionValue("x", startX + dx / root.width)
                backend.setCaptionValue("y", startY + dy / root.height)
            }
            onResized: function(factor) { backend.setCaptionValue("scale", startSize * factor) }
            onWheeled: function(steps) { backend.setCaptionValue("scale", backend.captionSize * Math.pow(1.08, steps)) }
        }
    }

    // ---- Text overlays, with their entrance animation ----
    Repeater {
        model: backend.overlays.length
        delegate: Item {
            id: overlay
            // Items stay put while their entry changes, so a drag survives edits.
            readonly property var modelData: backend.overlays[index] || ({})
            required property int index
            readonly property real age: root.time - modelData.start
            readonly property real entrance: Math.min(1, Math.max(0, age / 0.25))
            visible: root.time >= modelData.start && root.time < modelData.end && modelData.text.length > 0
            width: label.width
            height: label.height
            x: modelData.x * root.width - width / 2
            y: modelData.y * root.height - height / 2

            Text {
                id: label
                width: Math.min(implicitWidth, root.width * 0.94)
                text: overlay.modelData.text
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                font.family: backend.captionFont
                font.bold: true
                font.pixelSize: Math.max(1, Math.round(overlay.modelData.size * root.fontScale))
                color: "white"
                style: Text.Outline
                styleColor: "black"
                transform: Translate {
                    y: overlay.modelData.animation === "slide" ? (1 - overlay.entrance) * 160 * root.scaleFactor : 0
                }
                opacity: overlay.modelData.animation === "fade" || overlay.modelData.animation === "slide" ? overlay.entrance : 1
                scale: overlay.modelData.animation === "pop" ? 0.4 + 0.6 * Math.min(1, overlay.age / 0.18) : 1
            }

            EditBox {
                objectName: "overlayBox" + overlay.index
                anchors.fill: parent
                anchors.margins: -6
                visible: root.editable
                selected: win.selectionKind === "overlay" && win.selectedOverlay === overlay.index
                property real startX
                property real startY
                property real startSize
                onPressed: {
                    win.select("overlay", overlay.index)
                    startX = overlay.modelData.x
                    startY = overlay.modelData.y
                    startSize = overlay.modelData.size
                }
                onBegan: backend.checkpoint()
                onDragged: function(dx, dy) {
                    backend.setOverlayValue(overlay.index, "x", startX + dx / root.width)
                    backend.setOverlayValue(overlay.index, "y", startY + dy / root.height)
                }
                onResized: function(factor) { backend.setOverlayValue(overlay.index, "size", Math.round(startSize * factor)) }
                onWheeled: function(steps) {
                    backend.setOverlayValue(overlay.index, "size", Math.round(overlay.modelData.size * Math.pow(1.08, steps)))
                }
            }
        }
    }
}
