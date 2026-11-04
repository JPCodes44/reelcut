import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// A font chooser that filters as you type: click it, type part of a name,
// and the list narrows to the families that match, each shown in itself.
// Up/Down move, Enter picks, Esc closes. value "" is the style's default.
AbstractButton {
    id: root
    property string value: ""
    property string defaultFamily: ""
    signal picked(string family)

    readonly property var allFamilies: Qt.fontFamilies()
    property string query: ""
    // "" (Default) first while it matches, then every family containing all the typed words.
    readonly property var matches: {
        const words = query.toLowerCase().split(/\s+/).filter(function(w) { return w.length > 0 })
        const fits = function(name) {
            const lower = name.toLowerCase()
            return words.every(function(w) { return lower.indexOf(w) >= 0 })
        }
        const list = words.length === 0 || fits(qsTr("Default")) ? [""] : []
        for (let i = 0; i < allFamilies.length; ++i)
            if (fits(allFamilies[i]))
                list.push(allFamilies[i])
        return list
    }

    Layout.fillWidth: true
    implicitWidth: 160
    implicitHeight: 48
    leftPadding: 12
    rightPadding: 12
    onClicked: {
        query = ""
        popup.open()
    }

    contentItem: RowLayout {
        spacing: 8
        Label {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignVCenter
            text: root.value ? root.value : qsTr("Default (%1)").arg(root.defaultFamily)
            font.family: root.value ? root.value : root.defaultFamily
            font.pixelSize: 15
            elide: Text.ElideRight
        }
        Label { text: "▾"; color: "#9a9aa2"; Layout.alignment: Qt.AlignVCenter }
    }
    background: Rectangle {
        radius: 6
        color: "transparent"
        border.color: root.hovered || popup.visible ? "#8a8a92" : "#5a5a62"
    }

    Popup {
        id: popup
        objectName: "fontPopup"
        y: root.height + 4
        width: Math.max(root.width, 280)
        height: 360
        padding: 8
        // Keep the list inside the window when the field sits low or near an edge.
        margins: 8
        focus: true
        onOpened: {
            search.text = ""
            search.forceActiveFocus()
            list.currentIndex = Math.max(0, root.matches.indexOf(root.value))
            list.positionViewAtIndex(list.currentIndex, ListView.Center)
        }
        background: Rectangle { color: "#1c1c21"; radius: 8; border.color: "#3a3a40" }

        function choose(i) {
            if (i < 0 || i >= root.matches.length)
                return
            root.picked(root.matches[i])
            popup.close()
        }

        ColumnLayout {
            anchors.fill: parent
            spacing: 6
            TextField {
                id: search
                objectName: "fontSearch"
                Layout.fillWidth: true
                placeholderText: qsTr("Type to search %1 fonts").arg(root.allFamilies.length)
                onTextChanged: {
                    root.query = text
                    list.currentIndex = 0
                    list.positionViewAtBeginning()
                }
                Keys.onDownPressed: list.incrementCurrentIndex()
                Keys.onUpPressed: list.decrementCurrentIndex()
                Keys.onReturnPressed: popup.choose(list.currentIndex)
                Keys.onEnterPressed: popup.choose(list.currentIndex)
            }
            Label {
                visible: root.matches.length === 0
                Layout.fillWidth: true
                text: qsTr("No font matches “%1”.").arg(search.text)
                color: "#9a9aa2"
                wrapMode: Text.Wrap
            }
            ListView {
                id: list
                objectName: "fontList"
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: root.matches
                highlightMoveDuration: 0
                ScrollBar.vertical: ScrollBar {}
                delegate: ItemDelegate {
                    required property string modelData
                    required property int index
                    width: ListView.view.width
                    height: 40
                    highlighted: ListView.isCurrentItem
                    text: modelData === "" ? qsTr("Default (%1)").arg(root.defaultFamily) : modelData
                    font.family: modelData === "" ? root.defaultFamily : modelData
                    font.pixelSize: 16
                    onClicked: popup.choose(index)
                }
            }
        }
    }
}
