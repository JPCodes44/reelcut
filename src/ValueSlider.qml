import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// A labelled slider that records one undo step per drag. Double-click the
// label to reset it.
ColumnLayout {
    id: root
    property string label
    property real from: -1
    property real to: 1
    property real value: 0
    property real resetValue: 0
    property real stepSize: 0
    property string valueText: Math.round(value * 100) + ""
    signal moved(real value)

    spacing: 0
    Layout.fillWidth: true

    RowLayout {
        Layout.fillWidth: true
        Label {
            text: root.label
            Layout.fillWidth: true
            TapHandler {
                onDoubleTapped: {
                    backend.checkpoint()
                    root.moved(root.resetValue)
                }
            }
        }
        Label { text: root.valueText; color: "#9a9aa2"; font.family: "monospace" }
    }
    Slider {
        Layout.fillWidth: true
        from: root.from
        to: root.to
        stepSize: root.stepSize
        value: root.value
        onPressedChanged: if (pressed) backend.checkpoint()
        onMoved: root.moved(value)
    }
}
