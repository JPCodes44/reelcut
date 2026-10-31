import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// One grading wheel, DaVinci-style: drag the puck toward a hue to push the
// picture's color that way, the ring slider underneath moves R, G and B
// together. Double-click the wheel to centre the puck. The numbers are what
// the wheel does to each channel (src/colorgrade.h).
ColumnLayout {
    id: root
    property string label
    property var wheel: ({ x: 0, y: 0, master: 0 })
    property var channels: [0, 0, 0]  // per-channel result, shown under the wheel
    property int digits: 2
    signal moved(string key, real value)
    signal resetWheel()

    spacing: 4

    RowLayout {
        Layout.fillWidth: true
        Label { text: root.label; font.bold: true; Layout.fillWidth: true }
        ToolButton {
            text: "↺"
            font.pixelSize: 14
            implicitWidth: 28; implicitHeight: 28
            onClicked: root.resetWheel()
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Reset %1").arg(root.label)
        }
    }

    Item {
        id: face
        objectName: "wheel-" + root.label
        Layout.alignment: Qt.AlignHCenter
        Layout.preferredWidth: 150
        Layout.preferredHeight: 150

        ShaderEffect {
            anchors.fill: parent
            property real value: 0.55
            property real strength: 0.75
            fragmentShader: "qrc:/shaders/wheel.frag.qsb"
        }
        // Crosshair at the centre.
        Rectangle { anchors.centerIn: parent; width: 1; height: 12; color: "#80ffffff" }
        Rectangle { anchors.centerIn: parent; width: 12; height: 1; color: "#80ffffff" }
        Rectangle {
            x: face.width / 2 + root.wheel.x * face.width / 2 - width / 2
            y: face.height / 2 - root.wheel.y * face.height / 2 - height / 2
            width: 14; height: 14; radius: 7
            color: "transparent"
            border.color: "white"
            border.width: 2
        }
        MouseArea {
            anchors.fill: parent
            property real startX
            property real startY
            property real puckX
            property real puckY
            // Relative drag, at half speed with Shift held, like Resolve's fine adjust.
            onPressed: function(m) {
                backend.checkpoint()
                startX = m.x; startY = m.y
                puckX = root.wheel.x; puckY = root.wheel.y
            }
            onPositionChanged: function(m) {
                if (!pressed) return
                const k = (m.modifiers & Qt.ShiftModifier) ? 0.25 : 1
                let x = puckX + (m.x - startX) / (width / 2) * k
                let y = puckY - (m.y - startY) / (height / 2) * k
                const r = Math.sqrt(x * x + y * y)
                if (r > 1) { x /= r; y /= r }
                root.moved("x", x)
                root.moved("y", y)
            }
            onDoubleClicked: { root.moved("x", 0); root.moved("y", 0) }
        }
    }

    // The ring: all three channels together.
    Slider {
        objectName: "master-" + root.label
        Layout.fillWidth: true
        from: -1; to: 1
        value: root.wheel.master
        onPressedChanged: if (pressed) backend.checkpoint()
        onMoved: root.moved("master", value)
    }

    RowLayout {
        Layout.alignment: Qt.AlignHCenter
        spacing: 8
        Repeater {
            model: [["R", "#ff6b6b"], ["G", "#69db7c"], ["B", "#74c0fc"]]
            Label {
                required property var modelData
                required property int index
                text: modelData[0] + " " + Number(root.channels[index] || 0).toFixed(root.digits)
                color: modelData[1]
                font.family: "monospace"
                font.pixelSize: 11
            }
        }
    }
}
