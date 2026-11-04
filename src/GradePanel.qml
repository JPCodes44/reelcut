import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// DaVinci-style primaries: Lift, Gamma, Gain and Offset wheels, then
// Contrast / Pivot, Saturation, Hue and Temperature / Tint. Used for one clip
// (index >= 0) or the whole reel (index -1). See src/colorgrade.h.
ColumnLayout {
    id: root
    property int index: -1
    property var grade: ({})
    readonly property var params: grade && grade.params ? grade.params : ({})

    function set(key, value) { backend.setGrade(root.index, key, value) }
    function wheel(name) { return grade && grade[name] ? grade[name] : ({ x: 0, y: 0, master: 0 }) }
    // The numbers under each wheel, centred on "no change" like Resolve's.
    function shift(list, base) { return list ? list.map(function(v) { return v - base }) : [0, 0, 0] }

    spacing: 10
    Layout.fillWidth: true

    RowLayout {
        Layout.fillWidth: true
        Label {
            Layout.fillWidth: true
            text: qsTr("Drag a wheel's centre toward a color, the slider under it for brightness. Shift drags finer.")
            color: "#9a9aa2"
            wrapMode: Text.Wrap
            font.pixelSize: 12
        }
        Button {
            objectName: "resetGrade"
            Layout.alignment: Qt.AlignVCenter
            text: qsTr("Reset All")
            flat: true
            enabled: root.grade && !root.grade.neutral
            onClicked: root.set("reset", 0)
        }
    }

    GridLayout {
        id: wheels
        Layout.fillWidth: true
        // Two wheels a row when there's room, else stack them.
        columns: width >= 2 * 140 + columnSpacing ? 2 : 1
        uniformCellWidths: true
        columnSpacing: 12
        rowSpacing: 12
        ColorWheel {
            Layout.fillWidth: true
            label: qsTr("Lift")
            wheel: root.wheel("lift")
            channels: root.params.lift || [0, 0, 0]
            onMoved: function(key, value) { root.set("lift." + key, value) }
            onResetWheel: root.set("reset.lift", 0)
        }
        ColorWheel {
            Layout.fillWidth: true
            label: qsTr("Gamma")
            wheel: root.wheel("gamma")
            // shown as how far each channel's exponent moves from 1
            channels: root.shift(root.params.gexp, 1).map(function(v) { return -v })
            onMoved: function(key, value) { root.set("gamma." + key, value) }
            onResetWheel: root.set("reset.gamma", 0)
        }
        ColorWheel {
            Layout.fillWidth: true
            label: qsTr("Gain")
            wheel: root.wheel("gain")
            channels: root.params.gain || [1, 1, 1]
            onMoved: function(key, value) { root.set("gain." + key, value) }
            onResetWheel: root.set("reset.gain", 0)
        }
        ColorWheel {
            Layout.fillWidth: true
            label: qsTr("Offset")
            wheel: root.wheel("offset")
            channels: root.params.off || [0, 0, 0]
            onMoved: function(key, value) { root.set("offset." + key, value) }
            onResetWheel: root.set("reset.offset", 0)
        }
    }

    ValueSlider {
        label: qsTr("Contrast")
        from: 0; to: 2; resetValue: 1
        value: root.grade.contrast === undefined ? 1 : root.grade.contrast
        valueText: value.toFixed(2)
        onMoved: function(v) { root.set("contrast", v) }
    }
    ValueSlider {
        label: qsTr("Pivot")
        from: 0; to: 1; resetValue: 0.435
        value: root.grade.pivot === undefined ? 0.435 : root.grade.pivot
        valueText: value.toFixed(3)
        onMoved: function(v) { root.set("pivot", v) }
    }
    ValueSlider {
        label: qsTr("Saturation")
        from: 0; to: 2; resetValue: 1
        value: root.grade.saturation === undefined ? 1 : root.grade.saturation
        valueText: Math.round(value * 50) + ""
        onMoved: function(v) { root.set("saturation", v) }
    }
    ValueSlider {
        label: qsTr("Hue")
        from: -180; to: 180; resetValue: 0; stepSize: 1
        value: root.grade.hue === undefined ? 0 : root.grade.hue
        valueText: Math.round(value) + "°"
        onMoved: function(v) { root.set("hue", v) }
    }
    ValueSlider {
        label: qsTr("Temperature")
        from: -1; to: 1; resetValue: 0
        value: root.grade.temperature === undefined ? 0 : root.grade.temperature
        onMoved: function(v) { root.set("temperature", v) }
    }
    ValueSlider {
        label: qsTr("Tint")
        from: -1; to: 1; resetValue: 0
        value: root.grade.tint === undefined ? 0 : root.grade.tint
        onMoved: function(v) { root.set("tint", v) }
    }
}
