import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Modal spending for the Songs page in the top bar: this month's estimate
// against the free credit. Click to connect Modal and set the limit.
Button {
    id: meter
    flat: true
    Layout.minimumWidth: implicitWidth
    function money(d) { return "$" + d.toLocaleString(Qt.locale(), 'f', 2) }
    readonly property color tint: modalUsage.level === "over" ? "#ff6b6b" : modalUsage.level === "warn" ? "#ffb74d" : "#7fd99a"

    text: !songs.configured ? qsTr("Modal") : qsTr("Modal ") + money(modalUsage.spent) + " / $" + modalUsage.limit.toLocaleString(Qt.locale(), 'f', 0)
    Material.foreground: modalUsage.level === "ok" ? Material.foreground : tint
    font.bold: modalUsage.level === "over"
    background: Rectangle {
        implicitHeight: 34
        radius: 17
        color: meter.hovered ? "#24242a" : "transparent"
        border.width: 1
        border.color: modalUsage.level === "ok" ? "#3a3a42" : meter.tint
        // The usage bar along the bottom edge.
        Rectangle {
            anchors { left: parent.left; bottom: parent.bottom; margins: 6 }
            height: 3; radius: 1.5
            width: (parent.width - 12) * Math.min(1, modalUsage.fraction)
            color: meter.tint
            visible: songs.configured
        }
    }
    ToolTip.visible: hovered && !panel.visible
    ToolTip.delay: 400
    ToolTip.text: qsTr("%1 songs this month · %2 s generating · %3 s idle GPU\nEstimates at Modal list prices; check modal.com/usage for the bill.")
                  .arg(modalUsage.songCount).arg(Math.round(modalUsage.busySeconds)).arg(Math.round(modalUsage.idleSeconds))
    onClicked: panel.open()

    Popup {
        id: panel
        y: meter.height + 4
        x: meter.width - width
        width: 400
        padding: 16
        background: Rectangle { color: "#1d1d21"; border.color: "#2a2a2f"; radius: 8 }
        onOpened: { endpoint.text = songs.endpoint; tokenId.text = songs.tokenId; tokenSecret.text = ""; result.text = "" }

        ColumnLayout {
            width: parent.width
            spacing: 10
            Label { text: qsTr("Modal songs"); font.bold: true; font.pixelSize: 15 }
            Label {
                Layout.fillWidth: true
                text: meter.money(modalUsage.spent) + qsTr(" of $") + modalUsage.limit.toLocaleString(Qt.locale(), 'f', 0) + qsTr(" this month")
                color: meter.tint; font.pixelSize: 20; font.bold: true
            }
            ProgressBar { Layout.fillWidth: true; from: 0; to: 1; value: Math.min(1, modalUsage.fraction) }
            RowLayout {
                Label { text: qsTr("Monthly limit $"); color: "#9a9aa2" }
                SpinBox {
                    objectName: "modalLimit"
                    from: 1; to: 1000; editable: true
                    value: modalUsage.limit
                    onValueModified: modalUsage.limit = value
                }
            }
            Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: "#9a9aa2"; font.pixelSize: 12
                    text: qsTr("Modal's Starter plan includes $30 of free compute a month. Costs are estimated from GPU time.") }
            MenuSeparator { Layout.fillWidth: true }
            Label { text: qsTr("Connection"); font.bold: true }
            TextField { id: endpoint; objectName: "modalEndpoint"; Layout.fillWidth: true; placeholderText: qsTr("https://…--reelcut-acestep-api.modal.run") }
            TextField { id: tokenId; objectName: "modalTokenId"; Layout.fillWidth: true; placeholderText: qsTr("Proxy auth token ID (wk-…)") }
            TextField { id: tokenSecret; objectName: "modalTokenSecret"; Layout.fillWidth: true; echoMode: TextInput.Password
                        placeholderText: songs.configured ? qsTr("Secret saved; type to replace") : qsTr("Proxy auth token secret (ws-…)") }
            RowLayout {
                Button { text: qsTr("Save"); highlighted: true; onClicked: { songs.configure(endpoint.text, tokenId.text, tokenSecret.text); tokenSecret.text = ""; result.text = qsTr("Saved.") } }
                Button { text: qsTr("Test"); enabled: songs.configured; onClicked: { result.text = qsTr("Waking the GPU…"); songs.testConnection() } }
            }
            Label { id: result; Layout.fillWidth: true; wrapMode: Text.WordWrap; visible: text !== "" }
            Connections { target: songs; function onConnectionTested(ok, message) { result.text = message; result.color = ok ? "#7fd99a" : "#ff6b6b" } }
        }
    }
}
