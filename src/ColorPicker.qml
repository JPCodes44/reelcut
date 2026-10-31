import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import QtCore

// A color swatch button that opens a picker: Default / Transparent, a hue and
// saturation wheel, brightness and opacity, hex and RGB / HSV numbers,
// swatches (presets and recent), and an eyedropper for the preview.
//
// value is what's stored: "" (the style's default), "#00000000"
// (transparent) or "#aarrggbb". shown is the color it actually draws in.
// picked(value) fires on every change; the picker takes one undo step per
// opening.
RowLayout {
    id: root
    property string label
    property string value: ""
    property string shown: "#ffffffff"
    signal picked(string value)

    spacing: 8
    Layout.fillWidth: true

    readonly property bool isDefault: value === ""
    readonly property bool isTransparent: !isDefault && Qt.color(value).a === 0

    // Working color, kept as HSV so hue survives grey and black.
    property real h: 0
    property real s: 0
    property real v: 1
    property real a: 1

    function load(c) {
        const col = Qt.color(c)
        if (col.hsvHue >= 0)
            h = col.hsvHue * 360
        s = col.hsvSaturation
        v = col.hsvValue
        a = col.a
    }
    function hex2(n) { return ("0" + Math.round(n).toString(16)).slice(-2) }
    function argb() {
        const c = Qt.hsva(h / 360, s, v, a)
        return "#" + hex2(c.a * 255) + hex2(c.r * 255) + hex2(c.g * 255) + hex2(c.b * 255)
    }
    function commit() {
        const out = argb()
        root.value = out
        root.picked(out)
    }
    function pickValue(val) {
        root.value = val
        root.picked(val)
        if (val !== "" && val !== "#00000000")
            load(val)
    }
    function remember(val) {
        if (val === "" || Qt.color(val).a === 0)
            return
        let list = (recent.colors || []).filter(function(c) { return c !== val })
        list.unshift(val)
        recent.colors = list.slice(0, 8)
    }

    Settings {
        id: recent
        // Its own file, so it needs no organization name.
        location: StandardPaths.writableLocation(StandardPaths.GenericConfigLocation) + "/reelcut/colors.ini"
        category: "colorPicker"
        property var colors: []
    }

    Label { text: root.label; Layout.preferredWidth: 64 }

    // The swatch: the color over a checkerboard, so see-through shows.
    AbstractButton {
        id: swatch
        objectName: "colorSwatch-" + root.label
        Layout.fillWidth: true
        implicitHeight: 30
        onClicked: {
            root.load(root.shown)
            backend.checkpoint()
            popup.open()
        }
        background: Rectangle {
            radius: 6
            color: "#2a2a2f"
            border.color: swatch.hovered ? "#5a5a62" : "#3a3a40"
            Checkers { anchors.fill: parent; anchors.margins: 3 }
            Rectangle { anchors.fill: parent; anchors.margins: 3; radius: 4; color: root.shown }
            // The name on a small dark pill, readable over any color.
            Rectangle {
                anchors.centerIn: parent
                width: name.implicitWidth + 12
                height: name.implicitHeight + 4
                radius: height / 2
                color: "#b0000000"
                Label {
                    id: name
                    anchors.centerIn: parent
                    text: root.isDefault ? qsTr("Default") : root.isTransparent ? qsTr("Transparent")
                                                             : root.value.toUpperCase()
                    font.pixelSize: 11
                    font.family: root.isDefault || root.isTransparent ? "" : "monospace"
                    color: "white"
                }
            }
        }
    }

    component Checkers: Canvas {
        onPaint: {
            const ctx = getContext("2d")
            const n = 6
            for (let y = 0; y < height; y += n)
                for (let x = 0; x < width; x += n) {
                    ctx.fillStyle = ((x + y) / n) % 2 === 0 ? "#bdbdbd" : "#ffffff"
                    ctx.fillRect(x, y, n, n)
                }
        }
    }

    Popup {
        id: popup
        objectName: "colorPopup-" + root.label
        parent: Overlay.overlay
        x: Math.min(parent.width - width - 8, Math.max(8, swatch.mapToItem(parent, 0, 0).x - width + swatch.width))
        y: Math.min(parent.height - height - 8, Math.max(8, swatch.mapToItem(parent, 0, swatch.height + 4).y))
        width: 300
        padding: 14
        modal: false
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        onClosed: root.remember(root.value)
        background: Rectangle { color: "#1c1c21"; radius: 10; border.color: "#3a3a40" }

        ColumnLayout {
            width: parent.width
            spacing: 10

            RowLayout {
                Layout.fillWidth: true
                Label { text: root.label; font.bold: true; Layout.fillWidth: true }
                Button {
                    text: qsTr("Default")
                    flat: true
                    highlighted: root.isDefault
                    onClicked: root.pickValue("")
                }
                Button {
                    text: qsTr("Transparent")
                    flat: true
                    highlighted: root.isTransparent
                    onClicked: root.pickValue("#00000000")
                }
            }

            // Hue around the wheel, saturation out from the middle.
            Item {
                id: wheelArea
                Layout.alignment: Qt.AlignHCenter
                width: 180
                height: 180
                ShaderEffect {
                    anchors.fill: parent
                    property real value: Math.max(0.15, root.v)
                    property real strength: 1
                    fragmentShader: "qrc:/shaders/wheel.frag.qsb"
                }
                Rectangle {
                    // red at the top, hue clockwise: angle = 90 - hue
                    readonly property real angle: (90 - root.h) * Math.PI / 180
                    x: parent.width / 2 + Math.cos(angle) * root.s * parent.width / 2 - width / 2
                    y: parent.height / 2 - Math.sin(angle) * root.s * parent.height / 2 - height / 2
                    width: 14; height: 14; radius: 7
                    color: Qt.hsva(root.h / 360, root.s, root.v, 1)
                    border.color: "white"; border.width: 2
                }
                MouseArea {
                    anchors.fill: parent
                    function at(mx, my) {
                        const dx = (mx - width / 2) / (width / 2), dy = -(my - height / 2) / (height / 2)
                        const r = Math.min(1, Math.sqrt(dx * dx + dy * dy))
                        if (r > 0.001)
                            root.h = ((90 - Math.atan2(dy, dx) * 180 / Math.PI) % 360 + 360) % 360
                        root.s = r
                        if (root.a === 0) root.a = 1
                        if (root.v < 0.05) root.v = 1
                        root.commit()
                    }
                    onPressed: function(m) { at(m.x, m.y) }
                    onPositionChanged: function(m) { if (pressed) at(m.x, m.y) }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Label { text: qsTr("Brightness"); Layout.preferredWidth: 76 }
                Slider {
                    Layout.fillWidth: true
                    from: 0; to: 1; value: root.v
                    onMoved: { root.v = value; root.commit() }
                }
                Label { text: Math.round(root.v * 100) + "%"; font.family: "monospace"; color: "#9a9aa2"; Layout.preferredWidth: 40 }
            }
            RowLayout {
                Layout.fillWidth: true
                Label { text: qsTr("Opacity"); Layout.preferredWidth: 76 }
                Slider {
                    objectName: "opacitySlider"
                    Layout.fillWidth: true
                    from: 0; to: 1; value: root.a
                    onMoved: { root.a = value; root.commit() }
                }
                Label { text: Math.round(root.a * 100) + "%"; font.family: "monospace"; color: "#9a9aa2"; Layout.preferredWidth: 40 }
            }

            // Exact numbers: hex, RGB, HSV.
            GridLayout {
                Layout.fillWidth: true
                columns: 4
                columnSpacing: 6
                rowSpacing: 4
                Label { text: qsTr("Hex") }
                TextField {
                    objectName: "hexField"
                    Layout.columnSpan: 3
                    Layout.fillWidth: true
                    font.family: "monospace"
                    text: root.argb().toUpperCase()
                    onEditingFinished: {
                        let t = text.trim()
                        if (!t.startsWith("#")) t = "#" + t
                        const col = Qt.color(t)
                        if (/^#([0-9a-fA-F]{3}|[0-9a-fA-F]{6}|[0-9a-fA-F]{8})$/.test(t)) {
                            root.load(t)
                            root.commit()
                        }
                    }
                }
                Label { text: "R G B" }
                Repeater {
                    model: 3
                    SpinBox {
                        required property int index
                        Layout.fillWidth: true
                        from: 0; to: 255; editable: true
                        readonly property color c: Qt.hsva(root.h / 360, root.s, root.v, 1)
                        value: Math.round([c.r, c.g, c.b][index] * 255)
                        onValueModified: {
                            const rgb = [c.r, c.g, c.b]
                            rgb[index] = value / 255
                            const col = Qt.rgba(rgb[0], rgb[1], rgb[2], root.a)
                            root.load(col)
                            root.commit()
                        }
                    }
                }
                Label { text: "H S V" }
                SpinBox {
                    Layout.fillWidth: true
                    from: 0; to: 359; editable: true; wrap: true
                    value: Math.round(root.h)
                    onValueModified: { root.h = value; root.commit() }
                }
                SpinBox {
                    Layout.fillWidth: true
                    from: 0; to: 100; editable: true
                    value: Math.round(root.s * 100)
                    onValueModified: { root.s = value / 100; root.commit() }
                }
                SpinBox {
                    Layout.fillWidth: true
                    from: 0; to: 100; editable: true
                    value: Math.round(root.v * 100)
                    onValueModified: { root.v = value / 100; root.commit() }
                }
            }

            // Presets, then the last colors used.
            Flow {
                Layout.fillWidth: true
                spacing: 6
                Repeater {
                    model: ["#ffffffff", "#ff000000", "#ffffee00", "#ffff3b30", "#ffff9500", "#ff34c759",
                            "#ff00c7be", "#ff0a84ff", "#ffbf5af2", "#ffff2d55", "#80000000", "#cc111111"]
                    delegate: swatchDot
                }
            }
            Flow {
                Layout.fillWidth: true
                spacing: 6
                visible: (recent.colors || []).length > 0
                Label { text: qsTr("Recent"); color: "#9a9aa2"; font.pixelSize: 11; height: 22; verticalAlignment: Text.AlignVCenter }
                Repeater {
                    model: recent.colors || []
                    delegate: swatchDot
                }
            }

            Button {
                objectName: "eyedropper"
                Layout.fillWidth: true
                text: qsTr("Pick from the video")
                icon.name: "color-picker"
                onClicked: {
                    popup.close()
                    win.startEyedropper(function(picked) {
                        root.load(picked)
                        root.a = 1
                        root.commit()
                        popup.open()
                    })
                }
            }
        }
    }

    Component {
        id: swatchDot
        AbstractButton {
            required property var modelData
            width: 22; height: 22
            onClicked: { root.load(modelData); root.commit() }
            background: Item {
                Checkers { anchors.fill: parent }
                Rectangle {
                    anchors.fill: parent
                    color: modelData
                    border.color: "#5a5a62"
                }
            }
        }
    }
}
