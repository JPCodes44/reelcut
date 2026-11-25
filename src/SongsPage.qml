import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Dialogs
import QtQuick.Layouts
import QtMultimedia

// Make a song with ACE-Step on Modal. Every box is optional: leave them all
// empty for a random song.
Rectangle {
    id: page
    objectName: "songsPage"
    color: "#141418"
    property string referencePath: ""
    property string remixPath: ""
    property var imagePaths: []
    property bool describing: false
    property bool writing: false
    property string note: ""
    readonly property var styleChips: ["pop", "hip-hop", "lo-fi", "EDM", "rock", "acoustic", "cinematic", "comedic"]

    function fileName(p) { return p.substring(p.lastIndexOf("/") + 1) }
    function localPath(url) { return decodeURIComponent(url.toString().replace(/^file:\/\//, "")) }
    function request() {
        const r = { prompt: prompt.text, style: style.text, imageStyle: imageStyle.text, lyrics: lyrics.text,
                    instrumental: instrumental.checked, versions: versions.value }
        if (length.text !== "") r.duration = Number(length.text)
        if (referencePath !== "") r.reference = referencePath
        if (remixPath !== "") {
            r.remix = remixPath
            r.remixMode = ["cover", "extend", "repaint"][remixMode.currentIndex]
            r.repaintStart = Number(repaintStart.text || 0)
            r.repaintEnd = Number(repaintEnd.text || 0)
        }
        return r
    }
    function startGenerate() {
        note = ""
        if (!songs.generate(request())) note = songs.configured ? qsTr("Already making a song.") : qsTr("Connect Modal first: click the Modal meter.")
    }

    Connections {
        target: songs
        function onFailed(message) { page.note = message }
        function onLyricsWritten(text, error) { page.writing = false; if (text !== "") lyrics.text = text; page.note = error }
        function onImagesDescribed(text, error) { page.describing = false; if (text !== "") imageStyle.text = text; page.note = error }
    }

    FileDialog { id: referenceDialog; title: qsTr("Reference song"); nameFilters: [qsTr("Audio or video (*.mp3 *.wav *.flac *.m4a *.ogg *.mp4 *.mov *.webm)")]
                 onAccepted: page.referencePath = page.localPath(selectedFile) }
    FileDialog { id: remixDialog; title: qsTr("Audio to remix"); nameFilters: referenceDialog.nameFilters
                 onAccepted: page.remixPath = page.localPath(selectedFile) }
    FileDialog { id: lyricsDialog; title: qsTr("Lyrics file"); nameFilters: [qsTr("Text (*.txt *.md)")]
                 onAccepted: lyrics.text = songs.readTextFile(page.localPath(selectedFile)) }
    FileDialog { id: imagesDialog; title: qsTr("Images"); fileMode: FileDialog.OpenFiles; nameFilters: [qsTr("Images (*.png *.jpg *.jpeg *.webp)")]
                 onAccepted: { page.imagePaths = selectedFiles.slice(0, 6).map(page.localPath); page.describing = true; songs.describeImages(page.imagePaths) } }
    MediaPlayer { id: player; audioOutput: AudioOutput {} }
    Dialog {
        id: overLimit
        title: qsTr("Over your Modal limit")
        anchors.centerIn: parent
        standardButtons: Dialog.Ok | Dialog.Cancel
        Label { text: qsTr("This month's estimated spend has reached your limit. Make the song anyway?") }
        onAccepted: page.startGenerate()
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 24

        // The request, with Generate always in view under it.
        ColumnLayout {
        Layout.preferredWidth: 460
        Layout.maximumWidth: 460
        Layout.fillHeight: true
        spacing: 10
        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: availableWidth
            ColumnLayout {
                width: parent.width
                spacing: 10
                Label { text: qsTr("Make a song"); font.pixelSize: 22; font.bold: true }
                Label { text: qsTr("Every box is optional. Leave them all empty for a random song."); color: "#9a9aa2" }

                TextField { id: prompt; objectName: "songPrompt"; Layout.fillWidth: true; placeholderText: qsTr("What it's about") }
                TextField { id: style; objectName: "songStyle"; Layout.fillWidth: true; placeholderText: qsTr("Style: genre, mood, instruments") }
                Flow {
                    Layout.fillWidth: true; spacing: 6
                    Repeater {
                        model: page.styleChips
                        delegate: Button { text: modelData; flat: true; onClicked: style.text = style.text === "" ? modelData : style.text + ", " + modelData }
                    }
                }
                TextArea { id: imageStyle; Layout.fillWidth: true; visible: text !== "" || page.describing
                           placeholderText: page.describing ? qsTr("Claude is looking at the images…") : ""; wrapMode: TextEdit.Wrap }

                RowLayout {
                    Layout.fillWidth: true
                    Label { text: qsTr("Lyrics"); font.bold: true; Layout.fillWidth: true }
                    CheckBox { id: instrumental; text: qsTr("Instrumental") }
                    Button { text: qsTr("Load .txt"); flat: true; onClicked: lyricsDialog.open() }
                    Button { text: page.writing ? qsTr("Writing…") : qsTr("Write lyrics"); flat: true
                             enabled: !page.writing && songs.configured
                             onClicked: { page.writing = true; songs.writeLyrics(page.request()) } }
                }
                TextArea { id: lyrics; objectName: "songLyrics"; Layout.fillWidth: true; Layout.preferredHeight: 160
                           enabled: !instrumental.checked; wrapMode: TextEdit.Wrap
                           placeholderText: qsTr("Empty: ACE-Step writes the words") }

                RowLayout {
                    Label { text: qsTr("Length (s)") }
                    TextField { id: length; Layout.preferredWidth: 80; placeholderText: qsTr("auto"); validator: IntValidator { bottom: 10; top: 240 } }
                    Label { text: qsTr("Versions") }
                    SpinBox { id: versions; from: 1; to: 4; value: 2 }
                }

                Label { text: qsTr("Attachments"); font.bold: true }
                // Inline components can't see page's id, so the name is worked out here.
                component Attachment: RowLayout {
                    id: attachment
                    property string label
                    property string path
                    signal pick()
                    signal clear()
                    Layout.fillWidth: true
                    Button { text: attachment.label; onClicked: attachment.pick() }
                    Label { text: attachment.path === "" ? qsTr("none") : attachment.path.substring(attachment.path.lastIndexOf("/") + 1)
                            color: "#9a9aa2"; elide: Text.ElideMiddle; Layout.fillWidth: true }
                    ToolButton { text: "✕"; visible: attachment.path !== ""; onClicked: attachment.clear() }
                }
                Attachment { label: qsTr("Reference song"); path: page.referencePath; onPick: referenceDialog.open(); onClear: page.referencePath = "" }
                Attachment { label: qsTr("Audio to remix"); path: page.remixPath; onPick: remixDialog.open(); onClear: page.remixPath = "" }
                RowLayout {
                    visible: page.remixPath !== ""
                    ComboBox { id: remixMode; model: [qsTr("Cover in new style"), qsTr("Extend"), qsTr("Repaint a section")] }
                    TextField { id: repaintStart; visible: remixMode.currentIndex === 2; Layout.preferredWidth: 70; placeholderText: qsTr("from s") }
                    TextField { id: repaintEnd; visible: remixMode.currentIndex === 2; Layout.preferredWidth: 70; placeholderText: qsTr("to s") }
                }
                Attachment { label: qsTr("Images"); path: page.imagePaths.length ? qsTr("%n image(s)", "", page.imagePaths.length) : ""
                             onPick: imagesDialog.open(); onClear: { page.imagePaths = []; imageStyle.text = "" } }
            }
        }

                Button {
                    id: generateButton
                    objectName: "songGenerate"
                    Layout.fillWidth: true
                    highlighted: true
                    enabled: songs.configured && !songs.busy && !page.describing
                    text: songs.busy ? songs.stage : qsTr("Generate")
                    onClicked: modalUsage.level === "over" ? overLimit.open() : page.startGenerate()
                }
                Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: "#9a9aa2"; visible: !songs.configured
                        text: qsTr("Connect Modal first: click the Modal meter in the top bar.") }
                Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: "#ff8a65"; text: page.note; visible: text !== "" }
        }

        // The songs.
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Label { text: qsTr("Your songs"); font.pixelSize: 18; font.bold: true }
            Label { text: songs.songsDir; color: "#6a6a72"; font.pixelSize: 11 }
            Label {
                visible: songs.songs.length === 0
                Layout.topMargin: 24
                color: "#9a9aa2"
                text: songs.busy ? qsTr("Your first song is on its way…") : qsTr("No songs yet. Fill in what you like on the left and press Generate.")
            }
            ListView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: 10
                model: songs.songs
                delegate: Rectangle {
                    required property var modelData
                    width: ListView.view.width
                    height: column.implicitHeight + 20
                    radius: 8
                    color: "#1d1d21"
                    ColumnLayout {
                        id: column
                        anchors { left: parent.left; right: parent.right; top: parent.top; margins: 10 }
                        Label { text: modelData.caption; font.bold: true; elide: Text.ElideRight; Layout.fillWidth: true }
                        Label { color: "#9a9aa2"; font.pixelSize: 12
                                text: modelData.name + " · " + Math.round(modelData.seconds) + " s · ~$" + modelData.cost.toFixed(3) }
                        Repeater {
                            model: modelData.files
                            delegate: RowLayout {
                                required property string modelData
                                Layout.fillWidth: true
                                ToolButton {
                                    text: player.source.toString().endsWith(encodeURI(modelData)) && player.playbackState === MediaPlayer.PlayingState ? "❚❚" : "▶"
                                    onClicked: {
                                        if (player.playbackState === MediaPlayer.PlayingState && player.source.toString().endsWith(encodeURI(modelData))) { player.pause(); return }
                                        player.source = "file://" + modelData
                                        player.play()
                                    }
                                }
                                Label { text: page.fileName(modelData); Layout.fillWidth: true }
                                Button { text: qsTr("Use in reel"); flat: true; enabled: backend !== null; onClicked: backend.selectFiles("music", [modelData]) }
                                Button { text: qsTr("Remix this"); flat: true; onClicked: page.remixPath = modelData }
                            }
                        }
                        Button { text: qsTr("Show folder"); flat: true; onClicked: Qt.openUrlExternally("file://" + modelData.folder) }
                    }
                }
            }
        }
    }
}
