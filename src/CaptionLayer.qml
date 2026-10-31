import QtQuick

// Captions and text overlays on the preview, drawn like the burned-in .ass
// file draws them: same font, sizes at 1080x1920 scaled to the frame. Each
// can be dragged to move and resized from its corners or with the wheel.
Item {
    id: root
    property real time: 0
    property real scaleFactor: 1
    property bool editable: true
    // Entrances play only while playing; paused, everything sits at its final
    // size and place, so it can be grabbed where it looks to be.
    property bool animate: false
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
            // Glyph outlines drawn as curves: sharp at any size and zoom.
            renderType: Text.CurveRendering
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
            scale: word && root.animate ? Math.min(1, 0.7 + 0.3 * (root.time - word.start) / 0.09) : 1
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
                // Glyph outlines drawn as curves: sharp at any size and zoom.
                renderType: Text.CurveRendering
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
            readonly property real entrance: root.animate ? Math.min(1, Math.max(0, age / 0.25)) : 1
            // Fisheye text is drawn flat, then seen through a lens (src/fisheyetext.h).
            readonly property bool fisheye: modelData.style === "fisheye"
            visible: root.time >= modelData.start && root.time < modelData.end && modelData.text.length > 0
            // Colors, with the style's defaults filled in (Overlay::colorOf).
            readonly property color fillColor: modelData.fillColor || "white"
            readonly property color outlineColor: modelData.outlineColor || "black"
            readonly property color shadowColor: modelData.shadowColor || "transparent"
            readonly property color boxColor: modelData.boxColor || "transparent"
            readonly property bool hasBox: boxColor.a > 0
            // The box reaches this far past the text (TextBoxPadding, FisheyeText::BoxPadding).
            readonly property real pad: hasBox ? 18 * root.scaleFactor : 0
            // Outline ring offsets in preview pixels; none without an outline, or
            // behind a plain box (BorderStyle 3 draws the box instead).
            readonly property real outlineWidth: (modelData.outlineWidthValue || 0) * root.scaleFactor
            readonly property var outlineOffsets: {
                if (outlineColor.a === 0 || outlineWidth <= 0 || (hasBox && !fisheye))
                    return []
                const list = []
                for (let r = outlineWidth; r > 0.01; r -= 1.5)
                    for (let i = 0; i < 16; ++i)
                        list.push([r * Math.cos(i * Math.PI / 8), r * Math.sin(i * Math.PI / 8)])
                return list
            }
            width: label.width
            height: label.height
            x: modelData.x * root.width - width / 2
            y: modelData.y * root.height - height / 2

            // The text, its box and (plain text) its hard shadow, animated together.
            Item {
                id: block
                width: label.width
                height: label.height
                transform: Translate {
                    y: !overlay.fisheye && overlay.modelData.animation === "slide" ? (1 - overlay.entrance) * 160 * root.scaleFactor : 0
                }
                opacity: !overlay.fisheye && (overlay.modelData.animation === "fade" || overlay.modelData.animation === "slide") ? overlay.entrance : 1
                scale: !overlay.fisheye && overlay.modelData.animation === "pop" && root.animate ? 0.4 + 0.6 * Math.min(1, overlay.age / 0.18) : 1

                Rectangle {
                    objectName: "overlayBackground" + overlay.index
                    visible: overlay.hasBox
                    x: -overlay.pad
                    y: -overlay.pad
                    width: label.width + 2 * overlay.pad
                    height: label.height + 2 * overlay.pad
                    radius: overlay.fisheye ? 12 * root.scaleFactor : 0
                    color: overlay.boxColor
                }
                Text {
                    // Glyph outlines drawn as curves: sharp at any size and zoom.
                    renderType: Text.CurveRendering
                    // ASS draws a hard shadow the shadow distance down and right.
                    readonly property real d: (overlay.modelData.shadowDistanceValue || 0) * root.scaleFactor
                    visible: !overlay.fisheye && overlay.shadowColor.a > 0 && d > 0
                    x: d
                    y: d
                    width: label.width
                    text: label.text
                    horizontalAlignment: label.horizontalAlignment
                    wrapMode: label.wrapMode
                    font: label.font
                    color: overlay.shadowColor
                }
                // The outline: copies of the text in rings out to its width, the
                // same way the fisheye export draws it. (Text.Outline is 1 px.)
                Repeater {
                    model: overlay.outlineOffsets
                    Text {
                        // Glyph outlines drawn as curves: sharp at any size and zoom.
                        renderType: Text.CurveRendering
                        required property var modelData
                        x: modelData[0]
                        y: modelData[1]
                        width: label.width
                        text: label.text
                        horizontalAlignment: label.horizontalAlignment
                        wrapMode: label.wrapMode
                        font: label.font
                        color: overlay.outlineColor
                    }
                }
                Text {
                    // Glyph outlines drawn as curves: sharp at any size and zoom.
                    renderType: Text.CurveRendering
                    id: label
                    objectName: "overlayText" + overlay.index
                    width: Math.min(implicitWidth, root.width * 0.94)
                    text: overlay.modelData.text
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    font.family: overlay.modelData.fontFamily || backend.captionFont
                    font.bold: overlay.modelData.isBold === undefined ? !overlay.fisheye : overlay.modelData.isBold
                    font.italic: overlay.modelData.italic === true
                    font.letterSpacing: (overlay.modelData.letterSpacing || 0) * root.scaleFactor
                    // Fisheye text is an image at true pixel size in the export, so no caption scale.
                    font.pixelSize: Math.max(1, Math.round(overlay.modelData.size
                                                           * (overlay.fisheye ? root.scaleFactor : root.fontScale)))
                    color: overlay.fillColor
                }
            }

            // The lens: the flat text with room around it, bulged by the shader.
            ShaderEffectSource {
                id: flatText
                sourceItem: overlay.fisheye ? block : null
                hideSource: overlay.fisheye
                live: true
                visible: false
                sourceRect: Qt.rect(-(lens.width - label.width) / 2, -(lens.height - label.height) / 2,
                                    lens.width, lens.height)
                // The lens enlarges the middle about 1.5x, so capture the text 3x finer
                // than it shows; the shader averages 4 samples per pixel from it.
                textureSize: Qt.size(Math.ceil(lens.width * 3 * Screen.devicePixelRatio),
                                     Math.ceil(lens.height * 3 * Screen.devicePixelRatio))
                smooth: true
            }
            ShaderEffect {
                id: lens
                objectName: "overlayLens" + overlay.index
                visible: overlay.fisheye
                width: Math.ceil((label.width + 2 * overlay.pad) * 1.45)    // FisheyeText::Padding
                height: Math.ceil((label.height + 2 * overlay.pad) * 1.45)
                x: (label.width - width) / 2
                y: (label.height - height) / 2
                property variant source: flatText
                property real k: 0.6 * (overlay.modelData.bulge === undefined ? 0.6 : overlay.modelData.bulge)
                // FisheyeText::ShadowX / ShadowY times the shadow distance.
                readonly property real d: (overlay.modelData.shadowDistanceValue || 0) * root.scaleFactor
                property point shadow: Qt.point(0.66 * d / Math.max(1, width), d / Math.max(1, height))
                property point blur: Qt.point(5 * root.scaleFactor / Math.max(1, width), 5 * root.scaleFactor / Math.max(1, height))
                // One screen pixel, in the lens's 0..1 coordinates, for the 4-sample average.
                property point px: Qt.point(1 / Math.max(1, width * Screen.devicePixelRatio),
                                            1 / Math.max(1, height * Screen.devicePixelRatio))
                property vector4d shadowColor: Qt.vector4d(overlay.shadowColor.r, overlay.shadowColor.g,
                                                           overlay.shadowColor.b, overlay.shadowColor.a)
                fragmentShader: "qrc:/shaders/fisheye.frag.qsb"
                // Images can't pop or slide in the export, so every entrance is a fade
                // (and slide also rises).
                opacity: overlay.modelData.animation === "none" ? 1 : overlay.entrance
                transform: Translate {
                    y: overlay.modelData.animation === "slide" ? (1 - overlay.entrance) * 160 * root.scaleFactor : 0
                }
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
