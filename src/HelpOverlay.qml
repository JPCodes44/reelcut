import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Every shortcut, shown with ?.
Rectangle {
    id: root
    color: Qt.rgba(0, 0, 0, 0.78)
    signal dismissed()

    MouseArea { anchors.fill: parent; onClicked: root.dismissed() }

    Rectangle {
        anchors.centerIn: parent
        width: grid.implicitWidth + 64
        height: grid.implicitHeight + 96
        radius: theme.radius
        color: "#1a1a1e"
        border.color: "#2a2a2f"

        Label {
            id: heading
            text: qsTr("Shortcuts")
            font.pixelSize: 20
            font.bold: true
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.margins: 28
        }

        GridLayout {
            id: grid
            anchors.top: heading.bottom
            anchors.left: parent.left
            anchors.margins: 28
            anchors.topMargin: 18
            columns: 4
            columnSpacing: 18
            rowSpacing: 8
            Repeater {
                model: [
                    ["Space", qsTr("Play / pause")], ["S", qsTr("Split at playhead")],
                    ["← →", qsTr("Step a frame")], ["Shift+← →", qsTr("Step a second")],
                    ["Home / End", qsTr("Go to start / end")], ["Delete", qsTr("Delete clip")],
                    ["Alt+← →", qsTr("Move clip")], ["I", qsTr("Add clips")],
                    ["T", qsTr("Add text")], ["G", qsTr("Safe zones")],
                    ["Ctrl+Z", qsTr("Undo")], ["Ctrl+Shift+Z", qsTr("Redo")],
                    ["Ctrl+S", qsTr("Save")], ["Ctrl+O", qsTr("Open")],
                    ["Ctrl+E", qsTr("Export")], ["Ctrl+scroll", qsTr("Zoom timeline")],
                    ["?", qsTr("Shortcuts")], ["Q", qsTr("Quit")]
                ]
                delegate: RowLayout {
                    required property var modelData
                    Layout.columnSpan: 2
                    spacing: 12
                    Label {
                        text: modelData[0]
                        font.family: "monospace"
                        color: theme.accent
                        Layout.preferredWidth: 120
                    }
                    Label { text: modelData[1]; Layout.preferredWidth: 160 }
                }
            }
        }
    }
}
