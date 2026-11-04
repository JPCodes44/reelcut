import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Every shortcut, shown with ?.
Rectangle {
    id: root
    color: Qt.rgba(0, 0, 0, 0.78)
    signal dismissed()

    MouseArea { anchors.fill: parent; onClicked: root.dismissed() }

    Rectangle {
        anchors.centerIn: parent
        width: Math.min(parent.width - 48, content.implicitWidth + 48)
        height: Math.min(parent.height - 48, content.implicitHeight + 48)
        radius: theme.radius
        color: "#1a1a1e"
        border.color: "#2a2a2f"

        ColumnLayout {
            id: content
            anchors.fill: parent
            anchors.margins: 24
            spacing: 16

            Label {
                id: heading
                Layout.fillWidth: true
                text: qsTr("Shortcuts")
                font.pixelSize: 20
                font.bold: true
                elide: Text.ElideRight
            }

            // Two key / action pairs per row; each column lines up across rows.
            GridLayout {
                id: grid
                Layout.fillWidth: true
                Layout.fillHeight: true
                columns: 4
                columnSpacing: 12
                rowSpacing: 8
                Repeater {
                    // Flattened [key, action, key, action, ...] so every cell is a grid child.
                    model: [
                        "Space", qsTr("Play / pause"), "S", qsTr("Split at playhead"),
                        "← →", qsTr("Step a frame"), "Shift+← →", qsTr("Step a second"),
                        "Home / End", qsTr("Go to start / end"), "Delete", qsTr("Delete selection"),
                        "Alt+← →", qsTr("Move clip"), "I", qsTr("Add clips"),
                        "T", qsTr("Add text"), "G", qsTr("Safe zones"),
                        "Ctrl+Z", qsTr("Undo"), "Ctrl+Y", qsTr("Redo"),
                        "Ctrl+S", qsTr("Save"), "Ctrl+O", qsTr("Open"),
                        "Ctrl+E", qsTr("Export"), "Ctrl+scroll", qsTr("Zoom timeline"),
                        qsTr("Drag"), qsTr("Move text, captions, picture"), qsTr("Corner / scroll"), qsTr("Resize them"),
                        "Ctrl+drag", qsTr("Move without snapping"),
                        "?", qsTr("Shortcuts"), "Q", qsTr("Quit")
                    ]
                    delegate: Label {
                        id: cell
                        required property var modelData
                        required property int index
                        readonly property bool isKey: index % 2 === 0
                        text: modelData
                        Binding on font.family { when: cell.isKey; value: "monospace" }
                        color: isKey ? theme.accent : Material.foreground
                        // Leave a wider gap before the second pair of columns.
                        Layout.leftMargin: index % 4 === 2 ? 12 : 0
                        Layout.fillWidth: !isKey
                        Layout.alignment: Qt.AlignVCenter
                        elide: Text.ElideRight
                    }
                }
            }
        }
    }
}
