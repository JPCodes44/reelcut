import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Effects
import QtQuick.Layouts
import QtQuick.Shapes
import QtMultimedia

// Plays the edit: two clip slots take turns (even and odd clips), so the
// next clip is ready before the cut and both can show during a transition.
// Each clip is framed exactly as the export frames it. The look and filter
// are drawn live; a .cube LUT you load only applies on export.
Rectangle {
    id: root
    color: "#0b0b0d"

    property real time: 0
    property bool safeZones: true
    property bool showAppUi: true
    property bool playing: false
    property int clipIndex: -1
    property var currentClip: clipIndex >= 0 && clipIndex < backend.clips.length ? backend.clips[clipIndex] : null
    readonly property var nextClip: clipIndex >= 0 && clipIndex + 1 < backend.clips.length ? backend.clips[clipIndex + 1] : null
    // Inside a transition: the next clip has started while this one plays out.
    readonly property bool inTransition: currentClip !== null && nextClip !== null && currentClip.overlap > 0
                                         && time >= nextClip.start - 0.0001
    readonly property real transitionProgress: inTransition ? Math.max(0, Math.min(1, (time - nextClip.start) / currentClip.overlap)) : 0
    // The video effect showing now: the last one on the track that covers
    // this time. (The export applies every overlapping effect; the preview
    // draws one.)
    readonly property var activeEffect: {
        const list = backend.effects
        for (let k = list.length - 1; k >= 0; --k)
            if (time >= list[k].start && time < list[k].end) return list[k]
        return null
    }
    // Wall-clock time of the last tick, for advancing through photos.
    property real lastTick: 0
    signal timeAdvanced(real t)

    // Even clips play in one slot, odd clips in the other, so the next clip
    // is always loaded in the slot that isn't showing.
    function slotFor(i) { return i % 2 === 0 ? slotEven : slotOdd }

    function seek(t) {
        const at = backend.locate(t)
        clipIndex = at.index
        if (at.index < 0) {
            slotEven.clear()
            slotOdd.clear()
            return
        }
        slotFor(at.index).show(at.index, at.source, playing)
        prepareNext(t)
        lastTick = Date.now()
        music.sync(t)
    }
    // Loads the clip after the current one: at its place if a transition has
    // already started, otherwise paused at its first frame.
    function prepareNext(t) {
        const n = clipIndex + 1
        const other = slotFor(n)
        if (n >= backend.clips.length) {
            other.clear()
            return
        }
        const nc = backend.clips[n]
        const local = t - nc.start
        if (local >= 0) other.show(n, nc.in + local * nc.speed, playing)
        else other.show(n, nc.in, false)
    }
    function refresh() { seek(time) }
    function toggle() { playing ? pause() : play() }
    function play() {
        if (backend.clips.length === 0) return
        if (time >= backend.duration - 0.05) { timeAdvanced(0); playing = true; seek(0); return }
        playing = true
        seek(time)
    }
    function pause() {
        playing = false
        slotEven.pause()
        slotOdd.pause()
        music.pause()
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

    // Follows the current clip's player while playing, starts the next clip
    // when its transition begins, and moves on at each clip's out point.
    Timer {
        interval: 16
        repeat: true
        running: root.playing
        onTriggered: {
            const c = root.currentClip
            const cur = root.slotFor(root.clipIndex)
            if (!c || cur.loading) return
            const now = Date.now()
            const t = c.still ? root.time + (now - root.lastTick) / 1000 : cur.reached()
            const done = c.still ? t >= c.start + c.duration : cur.finished()
            root.lastTick = now

            const n = root.clipIndex + 1
            const nc = root.nextClip
            if (nc && c.overlap > 0 && t >= nc.start) {
                const incoming = root.slotFor(n)
                if (!incoming.playing || incoming.index !== n)
                    incoming.show(n, nc.in + (t - nc.start) * nc.speed, true)
            }
            if (done) {
                if (!nc) {
                    root.pause()
                    root.timeAdvanced(backend.duration)
                    return
                }
                const incoming = root.slotFor(n)
                if (!incoming.playing || incoming.index !== n)
                    incoming.show(n, nc.in + Math.max(0, t - nc.start) * nc.speed, true)
                root.clipIndex = n
                // The slot just freed loads the clip after next.
                if (n + 1 < backend.clips.length) root.slotFor(n + 1).show(n + 1, backend.clips[n + 1].in, false)
                else root.slotFor(n + 1).clear()
                root.timeAdvanced(Math.max(t, nc.start))
                return
            }
            root.timeAdvanced(Math.min(t, backend.duration))
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 12

        Item {
            id: stage
            Layout.fillWidth: true
            Layout.fillHeight: true

            // The 9:16 frame, as large as fits.
            Rectangle {
                id: frame
                readonly property real scaleFactor: height / 1920
                // Where dragged text and captions snap, in frame pixels: the
                // frame's center lines and thirds (where the eye goes first),
                // the middle of the safe zone, and the safe zone's borders.
                readonly property var snapGuides: {
                    const w = width, h = height
                    const dedupe = function(list) {
                        const out = []
                        for (const v of list.sort((a, b) => a - b))
                            if (out.length === 0 || v - out[out.length - 1] > 2) out.push(v)
                        return out
                    }
                    return {
                        area: frame,
                        x: dedupe([w / 2, w / 3, w * 2 / 3, (safeArea.leftEdge + safeArea.rightEdge) / 2]),
                        y: dedupe([h / 2, h / 3, h * 2 / 3, (safeArea.topEdge + safeArea.bottomEdge) / 2]),
                        edgeX: [safeArea.leftEdge, safeArea.rightEdge],
                        edgeY: [safeArea.topEdge, safeArea.bottomEdge]
                    }
                }
                height: Math.min(stage.height, stage.width * 16 / 9)
                width: height * 9 / 16
                anchors.centerIn: parent
                color: "black"
                clip: true

                // The picture: both clip slots, and during a transition the
                // shader that blends them. The filter look applies on top.
                Item {
                    id: picture
                    anchors.fill: parent
                    layer.enabled: backend.look.filter.length > 0 && backend.look.filterAmount > 0
                    layer.effect: ShaderEffect {
                        // src/shaders/lut.frag explains the lookup.
                        property variant lut: Image {
                            source: backend.look.filterStrip
                            smooth: true
                            mipmap: false
                            visible: false
                        }
                        property real amount: backend.look.filterAmount
                        fragmentShader: "qrc:/shaders/lut.frag.qsb"
                    }

                    // The whole reel's color wheels, after the clips and the
                    // video effect, before the filter (as in the export).
                    Item {
                        id: graded
                        objectName: "reelGrade"
                        anchors.fill: parent
                        layer.enabled: backend.look.grade !== undefined && !backend.look.grade.neutral
                        layer.effect: GradeEffect { grade: backend.look.grade }

                        // Clips, transitions and the active video effect. The filter
                        // look above applies after the effect, as in the export.
                        Item {
                            id: effected
                            anchors.fill: parent
                            layer.enabled: root.activeEffect !== null
                            layer.effect: ShaderEffect {
                                // src/shaders/effect.frag draws each effect.
                                property real mode: root.activeEffect ? root.activeEffect.mode : -1
                                property real local: root.activeEffect ? root.time - root.activeEffect.start : 0
                                property real intensity: root.activeEffect ? root.activeEffect.intensity : 0
                                property real speed: root.activeEffect ? root.activeEffect.speed : 0
                                property real aspect: effected.width / Math.max(1, effected.height)
                                fragmentShader: "qrc:/shaders/effect.frag.qsb"
                            }

                            ClipSlot {
                                id: slotEven
                                objectName: "slotEven"
                                anchors.fill: parent
                                time: root.time
                                z: root.clipIndex % 2 === 0 ? 1 : 0
                                opacity: root.inTransition || root.clipIndex % 2 === 0 ? 1 : 0
                                volume: root.inTransition ? (root.clipIndex % 2 === 0 ? 1 - root.transitionProgress : root.transitionProgress) : 1
                            }
                            ClipSlot {
                                id: slotOdd
                                objectName: "slotOdd"
                                anchors.fill: parent
                                time: root.time
                                z: root.clipIndex % 2 === 1 ? 1 : 0
                                opacity: root.inTransition || root.clipIndex % 2 === 1 ? 1 : 0
                                volume: root.inTransition ? (root.clipIndex % 2 === 1 ? 1 - root.transitionProgress : root.transitionProgress) : 1
                            }

                            // src/shaders/transition.frag draws each transition.
                            ShaderEffect {
                                objectName: "transitionEffect"
                                anchors.fill: parent
                                z: 2
                                visible: root.inTransition
                                property variant from: ShaderEffectSource {
                                    sourceItem: root.clipIndex % 2 === 0 ? slotEven : slotOdd
                                    hideSource: root.inTransition
                                    live: true
                                }
                                property variant to: ShaderEffectSource {
                                    sourceItem: root.clipIndex % 2 === 0 ? slotOdd : slotEven
                                    hideSource: root.inTransition
                                    live: true
                                }
                                property real progress: root.transitionProgress
                                property real mode: root.currentClip && root.currentClip.transitionMode >= 0 ? root.currentClip.transitionMode : 0
                                property real aspect: width / Math.max(1, height)
                                fragmentShader: "qrc:/shaders/transition.frag.qsb"
                            }

                            // Layers over the main track, later ones on top,
                            // under the effect and the look (as in the export).
                            Repeater {
                                model: backend.layers.length
                                delegate: LayerSlot {
                                    required property int index
                                    objectName: "layerSlot" + index
                                    layerIndex: index
                                    z: 3 + index
                                    frameWidth: frame.width
                                    frameHeight: frame.height
                                    time: root.time
                                    playing: root.playing
                                }
                            }
                        }
                    }
                }

                // The eyedropper: while it's on, a click reads the video's color there.
                MouseArea {
                    objectName: "eyedropperArea"
                    anchors.fill: parent
                    z: 100
                    visible: win.eyedropperCallback !== null
                    cursorShape: Qt.CrossCursor
                    hoverEnabled: true
                    function pick(mx, my) {
                        const fx = mx / width, fy = my / height
                        picture.grabToImage(function(result) {
                            const callback = win.eyedropperCallback
                            win.eyedropperCallback = null
                            const color = backend.pixelColor(result.image, fx, fy)
                            if (callback && color.length > 0)
                                callback(color)
                        })
                    }
                    onClicked: function(m) { pick(m.x, m.y) }
                    // Fits a narrow frame: the pill never grows past the frame's
                    // width, and the hint elides instead of being cut off.
                    Rectangle {
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.top: parent.top
                        anchors.topMargin: 8
                        width: Math.min(hint.implicitWidth + 24, parent.width - 16)
                        height: hint.implicitHeight + 12
                        radius: height / 2
                        color: "#cc000000"
                        Label {
                            id: hint
                            anchors.fill: parent
                            anchors.leftMargin: 12
                            anchors.rightMargin: 12
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                            elide: Text.ElideRight
                            text: qsTr("Click to pick a color · Esc to cancel")
                            color: "white"
                            font.pixelSize: 12
                        }
                    }
                }

                // The picture itself: drag to pan the framing, corners or wheel to zoom.
                EditBox {
                    objectName: "clipBox"
                    anchors.fill: parent
                    // Guides only: the picture snaps its pan, not its box.
                    snap: ({ area: frame, x: [], y: [], edgeX: [], edgeY: [] })
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
                        const framed = root.slotFor(root.clipIndex).framedItem
                        spanX = (framed.width - frame.width) / 2
                        spanY = (framed.height - frame.height) / 2
                    }
                    onBegan: backend.checkpoint()
                    // Pan snaps to centered (0) and to the picture's edges (±1)
                    // when the picture is within a few pixels of them.
                    function snapPan(pan, span, line) {
                        if (!snapping)
                            return [pan, -1]
                        for (const target of [0, -1, 1])
                            if (Math.abs((pan - target) * span) < snapDistance)
                                return [target, target === 0 ? line : -1]
                        return [pan, -1]
                    }
                    onDragged: function(dx, dy) {
                        if (spanX > 0.5) {
                            const px = snapPan(startPanX - dx / spanX, spanX, frame.width / 2)
                            backend.setClipValue(root.clipIndex, "panX", px[0])
                            guideX = px[1]
                        }
                        if (spanY > 0.5) {
                            const py = snapPan(startPanY - dy / spanY, spanY, frame.height / 2)
                            backend.setClipValue(root.clipIndex, "panY", py[0])
                            guideY = py[1]
                        }
                    }
                    onResized: function(factor) { backend.setClipValue(root.clipIndex, "zoom", startZoom * factor) }
                    onWheeled: function(steps) {
                        backend.setClipValue(root.clipIndex, "zoom", root.currentClip.zoom * Math.pow(1.05, steps))
                    }
                }

                // The selected layer's box: drag to move it, corners or wheel to resize.
                EditBox {
                    id: layerBox
                    objectName: "layerBox"
                    readonly property int index: win.selectionKind === "layer" ? win.selectedLayer : -1
                    readonly property var layerData: index >= 0 && index < backend.layers.length ? backend.layers[index] : null
                    readonly property bool showing: layerData !== null && root.time >= layerData.start && root.time < layerData.end
                    visible: showing
                    selected: showing
                    snap: frame.snapGuides
                    width: layerData ? frame.width * layerData.scale : 0
                    height: layerData ? frame.height * layerData.scale : 0
                    x: layerData ? frame.width * layerData.x - width / 2 : 0
                    y: layerData ? frame.height * layerData.y - height / 2 : 0
                    property real startX
                    property real startY
                    property real startScale
                    onPressed: {
                        startX = layerData.x
                        startY = layerData.y
                        startScale = layerData.scale
                    }
                    onBegan: backend.checkpoint()
                    onDragged: function(dx, dy) {
                        backend.setLayerValue(index, "x", startX + dx / frame.width)
                        backend.setLayerValue(index, "y", startY + dy / frame.height)
                    }
                    onResized: function(factor) { backend.setLayerValue(index, "scale", startScale * factor) }
                    onWheeled: function(steps) {
                        backend.setLayerValue(index, "scale", layerData.scale * Math.pow(1.05, steps))
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
                    snap: frame.snapGuides
                    time: root.time
                    animate: root.playing
                    scaleFactor: frame.scaleFactor
                }

                // Where the app's own buttons and text don't cover the video:
                // a dashed outline only, so the picture itself stays untouched.
                // One logical phone surface scales every layout, icon and
                // text size together, including at narrow preview widths.
                SocialPreview {
                    width: 360
                    height: 640
                    scale: frame.width / 360
                    anchors.centerIn: parent
                    visible: root.showAppUi
                    platform: backend.platform
                    // The connected account on that platform, when there is one.
                    username: (platform === "instagram" ? backend.instagramUser
                               : platform === "facebook" ? backend.facebookPage
                               : platform === "youtube" ? backend.youtubeChannel : "") || "your_account"
                    title: backend.title
                    caption: backend.postCaption
                    hashtags: backend.checkPost(backend.platform, backend.postCaption, backend.postHashtags).tags.join(" ")
                    onEditRequested: postEditor.begin()
                }

                Shape {
                    id: safeArea
                    anchors.fill: parent
                    visible: root.safeZones && backend.clips.length > 0
                    readonly property var zone: backend.platformInfo
                    readonly property real topEdge: zone.safeTop * frame.scaleFactor
                    readonly property real bottomEdge: zone.safeBottom * frame.scaleFactor
                    readonly property real leftEdge: zone.safeLeft * frame.scaleFactor
                    readonly property real rightEdge: zone.safeRight * frame.scaleFactor
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
                    width: parent.width - 32
                    visible: root.currentClip !== null && root.currentClip.offline
                    text: qsTr("Missing file:\n%1").arg(root.currentClip ? root.currentClip.name : "")
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                    color: "#ff8a65"
                }
            }

            // Empty state
            // Sits inside the 9:16 frame so the text wraps within it.
            ColumnLayout {
                // Falls back to the wider stage when the frame gets too narrow.
                anchors.horizontalCenter: frame.horizontalCenter
                anchors.verticalCenter: frame.verticalCenter
                width: Math.min(stage.width - 32, Math.max(frame.width - 32, 260))
                visible: backend.clips.length === 0
                spacing: 12
                Label {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
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
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    text: qsTr("Press ? for shortcuts")
                    color: "#9a9aa2"
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            CheckBox {
                objectName: "showAppUi"
                text: qsTr("Show app UI")
                checked: true
                onCheckedChanged: root.showAppUi = checked
            }
            Item { Layout.fillWidth: true }
            Button {
                objectName: "editPostText"
                text: qsTr("Caption and tags")
                onClicked: postEditor.begin()
            }
        }

        // Transport: centered; the time label can shrink and elide on narrow panels.
        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            spacing: 12
            ToolButton {
                // Fixed width: the two glyphs differ, so the row would shift.
                Layout.preferredWidth: 48
                text: root.playing ? "❚❚" : "▶"
                font.pixelSize: 18
                enabled: backend.clips.length > 0
                onClicked: root.toggle()
            }
            Label {
                Layout.alignment: Qt.AlignVCenter
                Layout.minimumWidth: 0
                text: win.timeText(root.time) + " / " + win.timeText(backend.duration)
                font.family: "monospace"
                color: "#c8c8cf"
                elide: Text.ElideRight
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

    Popup {
        id: postEditor
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(parent.width - 32, 500)
        height: Math.min(parent.height - 32, implicitHeight)
        padding: 20
        modal: true
        property bool editing: false
        property bool hasEdited: false
        function begin() {
            editing = false
            previewCaption.text = backend.postCaption
            previewTags.text = backend.postHashtags
            hasEdited = false
            editing = true
            open()
            previewCaption.forceActiveFocus()
        }
        function updateText() {
            if (!editing) return
            backend.setPostText(previewCaption.text, previewTags.text, !hasEdited)
            hasEdited = true
        }
        onClosed: editing = false
        contentItem: ColumnLayout {
            spacing: 8
            Label {
                Layout.fillWidth: true
                text: qsTr("%1 preview text").arg(backend.platformInfo.label)
                font.bold: true
                wrapMode: Text.WordWrap
            }
            Label { text: qsTr("Caption") }
            ScrollView {
                Layout.fillWidth: true
                Layout.preferredHeight: 120
                clip: true
                TextArea {
                    id: previewCaption
                    objectName: "previewPostCaption"
                    wrapMode: TextEdit.Wrap
                    placeholderText: qsTr("What's this reel about?")
                    onTextChanged: postEditor.updateText()
                }
            }
            Label { text: qsTr("Hashtags") }
            TextField {
                id: previewTags
                objectName: "previewPostHashtags"
                Layout.fillWidth: true
                placeholderText: qsTr("#travel #sunset")
                onTextChanged: postEditor.updateText()
            }
            Label {
                Layout.fillWidth: true
                text: qsTr("Saved with your project and used when posting. App UI is preview only.")
                wrapMode: Text.WordWrap
                font.pixelSize: 12
                opacity: 0.7
            }
            Button {
                Layout.alignment: Qt.AlignRight
                text: qsTr("Done")
                onClicked: postEditor.close()
            }
        }
    }
}
