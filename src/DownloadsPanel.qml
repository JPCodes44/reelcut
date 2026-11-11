import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Reels saved from the web: paste a YouTube, Instagram, TikTok or Facebook
// link to download it, then drag a video onto the timeline (or double-click
// to add it at the end).
Rectangle {
    id: root
    color: "#141417"

    // A video is being dragged out of the library; the window shows where it lands.
    signal dragStarted()

    // Deleting rescans the folder, which rebuilds the grid and destroys the
    // tile (and the button or menu item still handling the click), so the
    // delete runs once that handler has returned.
    function removeLater(path) {
        Qt.callLater(function() { downloads.remove(path) })
    }

    function submit() {
        if (downloads.download(link.text))
            link.text = ""
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            Label { text: qsTr("Downloads"); font.bold: true; font.pixelSize: 14; Layout.fillWidth: true }
            ToolButton {
                text: "📁"
                font.pixelSize: 13
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Show the downloads folder")
                onClicked: downloads.showFolder()
            }
        }

        TextField {
            id: link
            objectName: "downloadLink"
            Layout.fillWidth: true
            placeholderText: qsTr("Paste a reel link…")
            selectByMouse: true
            onAccepted: root.submit()
        }
        Button {
            objectName: "downloadButton"
            Layout.fillWidth: true
            text: qsTr("Download")
            highlighted: true
            enabled: link.text.trim().length > 0
            Material.foreground: theme.accentForeground
            onClicked: root.submit()
        }
        Label {
            Layout.fillWidth: true
            visible: !downloads.available
            text: qsTr("Needs yt-dlp: sudo pacman -S yt-dlp")
            color: "#ff8a65"
            wrapMode: Text.Wrap
            font.pixelSize: 11
        }
        Label {
            Layout.fillWidth: true
            text: qsTr("YouTube, Instagram, TikTok, Facebook")
            color: "#6e6e76"
            font.pixelSize: 11
            elide: Text.ElideRight
        }

        // Downloads in progress
        Repeater {
            model: downloads.active
            delegate: RowLayout {
                required property var modelData
                Layout.fillWidth: true
                spacing: 6
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    Label {
                        Layout.fillWidth: true
                        text: modelData.title || modelData.link
                        elide: Text.ElideRight
                        font.pixelSize: 11
                        color: "#c8c8d0"
                    }
                    ProgressBar {
                        Layout.fillWidth: true
                        indeterminate: modelData.progress < 0
                        value: Math.max(0, modelData.progress)
                    }
                }
                ToolButton {
                    text: "✕"
                    implicitWidth: 28
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Cancel download")
                    onClicked: { const id = modelData.id; Qt.callLater(function() { downloads.cancel(id) }) }
                }
            }
        }

        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: "#2a2a2f" }

        Label {
            Layout.fillWidth: true
            visible: downloads.items.length === 0
            text: qsTr("Downloaded videos show up here. Drag one onto the timeline to use it.")
            color: "#6e6e76"
            wrapMode: Text.Wrap
            font.pixelSize: 11
        }

        GridView {
            id: grid
            objectName: "downloadsGrid"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: downloads.items
            readonly property int columns: 2
            // Never 0: GridView divides its width by the cell width.
            cellWidth: Math.max(1, Math.floor(width / columns))
            cellHeight: Math.round(cellWidth * 16 / 9) + 22
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

            delegate: Item {
                id: tile
                required property var modelData
                width: grid.cellWidth
                height: grid.cellHeight

                // A native drag, so the video drops as a file into other apps too.
                DragHandler {
                    id: drag
                    target: null
                    onActiveChanged: if (active) {
                        root.dragStarted()
                        const path = tile.modelData.path, thumb = tile.modelData.thumb
                        Qt.callLater(function() { downloads.startDrag(path, thumb) })
                    }
                }
                TapHandler {
                    onDoubleTapped: backend.addUrls([tile.modelData.url])
                }
                TapHandler {
                    acceptedButtons: Qt.RightButton
                    onTapped: menu.popup()
                }
                HoverHandler { id: hover; cursorShape: Qt.OpenHandCursor }

                Menu {
                    id: menu
                    MenuItem { text: qsTr("Add to timeline"); onTriggered: backend.addUrls([tile.modelData.url]) }
                    MenuItem { text: qsTr("Copy file (paste into a chat)"); onTriggered: { downloads.copyFile(tile.modelData.path); backend.notify(qsTr("Copied. Paste with Ctrl+V.")) } }
                    MenuItem { text: qsTr("Copy path (for claude, codex)"); onTriggered: { downloads.copyPath(tile.modelData.path); backend.notify(qsTr("Path copied.")) } }
                    MenuItem { text: qsTr("Show in folder"); onTriggered: downloads.showFolder() }
                    MenuItem { text: qsTr("Delete"); onTriggered: root.removeLater(tile.modelData.path) }
                }

                Rectangle {
                    id: frame
                    x: 3
                    y: 3
                    width: parent.width - 6
                    height: parent.height - 24
                    radius: Math.min(6, theme.radius)
                    color: "#0c0c0e"
                    border.color: hover.hovered ? theme.accent : "#2a2a2f"
                    clip: true
                    Image {
                        anchors.fill: parent
                        anchors.margins: 1
                        source: tile.modelData.thumb
                        fillMode: Image.PreserveAspectCrop
                        asynchronous: true
                        sourceSize.width: 240
                    }
                    Label {
                        anchors.centerIn: parent
                        visible: !tile.modelData.thumb || tile.modelData.thumb.toString().length === 0
                        text: "▶"
                        color: "#5e5e66"
                        font.pixelSize: 22
                    }
                    ToolButton {
                        anchors.top: parent.top
                        anchors.right: parent.right
                        visible: hover.hovered
                        text: "✕"
                        implicitWidth: 26
                        implicitHeight: 26
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("Delete from downloads")
                        onClicked: root.removeLater(tile.modelData.path)
                        background: Rectangle { radius: 13; color: "#cc000000" }
                    }
                }
                Label {
                    anchors.top: frame.bottom
                    anchors.topMargin: 2
                    x: 4
                    width: parent.width - 8
                    text: tile.modelData.title
                    elide: Text.ElideRight
                    font.pixelSize: 11
                    color: "#c8c8d0"
                    ToolTip.visible: hover.hovered
                    ToolTip.delay: 600
                    ToolTip.text: tile.modelData.title + "\n" + qsTr("Drag onto the timeline, or into Claude, ChatGPT, Codex… · right-click to copy")
                }
            }
        }
    }
}
