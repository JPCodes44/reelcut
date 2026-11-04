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
    // Set customReset to have a double-click on the label emit reset()
    // instead of moving to resetValue.
    property bool customReset: false
    signal reset()

    spacing: 0
    Layout.fillWidth: true

    RowLayout {
        Layout.fillWidth: true
        spacing: 8
        Label {
            text: root.label
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignVCenter
            elide: Text.ElideRight
            TapHandler {
                onDoubleTapped: {
                    if (root.customReset) {
                        root.reset()
                        return
                    }
                    backend.checkpoint()
                    root.moved(root.resetValue)
                }
            }
        }
        // Right-aligned with a floor width so the number doesn't jitter while dragging.
        Label {
            text: root.valueText
            color: "#9a9aa2"
            font.family: "monospace"
            horizontalAlignment: Text.AlignRight
            Layout.alignment: Qt.AlignVCenter
            Layout.minimumWidth: 40
        }
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
