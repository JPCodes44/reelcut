import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Higgsfield spending in the top bar: dollars spent since the last reset, red
// once over budget. Click for the balance, today's spend, the budget and
// every recent charge.
Button {
    id: meter
    flat: true
    Layout.minimumWidth: implicitWidth

    readonly property real rate: higgsfield.dollarsPerCredit
    function money(credits) { return "$" + (credits * rate).toLocaleString(Qt.locale(), 'f', 2) }
    function credits(n) { return n.toLocaleString(Qt.locale(), 'f', n % 1 === 0 ? 0 : 2) }
    readonly property color warn: "#ff6b6b"
    // A burst of spending right now: more than a dollar in ten minutes.
    // Counting from a reset, rather than from the first charge ever.
    readonly property bool counting: !isNaN(higgsfield.since.getTime())
    readonly property bool burning: higgsfield.spentRecent * rate >= 1
    // Narrow windows drop the "Higgsfield" name; "spent" and "left" always
    // stay, or the two amounts read as one taken from the other.
    readonly property bool compact: ApplicationWindow.window !== null && ApplicationWindow.window.width < 1300

    text: !higgsfield.connected ? qsTr("Higgsfield")
        : !higgsfield.loaded ? (higgsfield.status !== "" ? qsTr("Higgsfield ⚠") : qsTr("Higgsfield…"))
        : (compact ? "" : qsTr("Higgsfield ")) + money(higgsfield.spentSince) + (higgsfield.budget > 0 ? " / $" + higgsfield.budget.toLocaleString(Qt.locale(), 'f', 0) : "")
          + qsTr(" spent") + "  ·  " + money(higgsfield.balance) + qsTr(" left")
    Material.foreground: higgsfield.overBudget ? warn : burning ? "#ffb74d" : Material.foreground
    font.bold: higgsfield.overBudget
    // Outlined, so it reads as something to click rather than a label.
    background: Rectangle {
        implicitHeight: 34
        radius: 17
        color: meter.hovered ? "#24242a" : "transparent"
        border.width: 1
        border.color: higgsfield.overBudget ? meter.warn : meter.burning ? "#ffb74d" : "#3a3a42"
    }
    ToolTip.visible: hovered && !panel.visible
    ToolTip.delay: 400
    ToolTip.text: !higgsfield.connected ? qsTr("Sign in to watch Higgsfield credits as agents spend them")
        : (counting ? qsTr("Higgsfield: spent since %1, and what's left").arg(higgsfield.since.toLocaleString(Qt.locale(), Locale.ShortFormat))
                    : qsTr("Higgsfield: spent in all, and what's left. Reset to count one job."))
    onClicked: { panel.open(); higgsfield.refresh() }

    Popup {
        id: panel
        y: meter.height + 4
        x: meter.width - width
        width: 380
        padding: 16
        background: Rectangle { color: "#1d1d21"; border.color: "#2a2a2f"; radius: 8 }

        ColumnLayout {
            width: parent.width
            spacing: 10

            RowLayout {
                Layout.fillWidth: true
                Label { text: qsTr("Higgsfield credits"); font.bold: true; font.pixelSize: 15; Layout.fillWidth: true }
                Label { text: higgsfield.plan.toUpperCase(); color: "#9a9aa2"; visible: higgsfield.plan !== "" }
            }

            // Not signed in.
            ColumnLayout {
                visible: !higgsfield.connected
                Layout.fillWidth: true
                spacing: 8
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: "#9a9aa2"
                    text: qsTr("Sign in with your Higgsfield account to see what agents spend through the Higgsfield MCP, in dollars, as it happens.")
                }
                Button {
                    text: higgsfield.signingIn ? qsTr("Waiting for the browser…") : qsTr("Sign in to Higgsfield")
                    highlighted: true
                    enabled: !higgsfield.signingIn
                    Material.foreground: theme.accentForeground
                    onClicked: higgsfield.signIn()
                }
            }

            Label {
                visible: higgsfield.status !== ""
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: meter.warn
                text: higgsfield.status
            }

            // Signed in.
            GridLayout {
                visible: higgsfield.connected && higgsfield.loaded
                Layout.fillWidth: true
                columns: 2
                columnSpacing: 16
                rowSpacing: 6
                component Stat: ColumnLayout {
                    property string title
                    property string value
                    property string detail
                    property color tint: "#ffffff"
                    spacing: 0
                    Layout.fillWidth: true
                    Label { text: title; color: "#9a9aa2"; font.pixelSize: 12 }
                    Label { text: value; color: tint; font.pixelSize: 20; font.bold: true }
                    Label { text: detail; color: "#9a9aa2"; font.pixelSize: 11; visible: detail !== "" }
                }
                Stat {
                    title: qsTr("All time")
                    value: meter.money(higgsfield.spentAll)
                    detail: meter.credits(higgsfield.spentAll) + qsTr(" credits")
                    tint: higgsfield.overBudget && !meter.counting ? meter.warn : "#ffffff"
                }
                Stat {
                    visible: meter.counting
                    title: qsTr("Since %1").arg(higgsfield.since.toLocaleString(Qt.locale(), Locale.ShortFormat))
                    value: meter.money(higgsfield.spentSince)
                    detail: meter.credits(higgsfield.spentSince) + qsTr(" credits")
                    tint: higgsfield.overBudget ? meter.warn : "#ffffff"
                }
                Stat {
                    title: qsTr("Last 10 minutes")
                    value: meter.money(higgsfield.spentRecent)
                    detail: meter.credits(higgsfield.spentRecent) + qsTr(" credits")
                    tint: meter.burning ? "#ffb74d" : "#ffffff"
                }
                Stat {
                    title: qsTr("Today")
                    value: meter.money(higgsfield.spentToday)
                    detail: meter.credits(higgsfield.spentToday) + qsTr(" credits")
                }
                Stat {
                    title: qsTr("Balance")
                    value: meter.money(higgsfield.balance)
                    detail: meter.credits(higgsfield.balance) + qsTr(" credits")
                }
            }

            ProgressBar {
                visible: higgsfield.connected && higgsfield.loaded && higgsfield.budget > 0
                Layout.fillWidth: true
                from: 0
                to: Math.max(higgsfield.budget, 0.01)
                value: Math.min(higgsfield.spentSince * meter.rate, higgsfield.budget)
                Material.accent: higgsfield.overBudget ? meter.warn : theme.accent
            }

            RowLayout {
                visible: higgsfield.connected
                Layout.fillWidth: true
                spacing: 8
                Label { text: qsTr("Budget $"); color: "#9a9aa2" }
                TextField {
                    id: budgetField
                    Layout.preferredWidth: 70
                    text: higgsfield.budget > 0 ? higgsfield.budget.toString() : ""
                    validator: DoubleValidator { bottom: 0; decimals: 2; notation: DoubleValidator.StandardNotation }
                    inputMethodHints: Qt.ImhFormattedNumbersOnly
                    onEditingFinished: higgsfield.budget = text === "" ? 0 : Number.fromLocaleString(Qt.locale(), text)
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Dollars allowed since the reset (or in all, without one); empty for no budget. Past it, Reelcut warns you and marks ~/.config/reelcut/higgsfield-status.json overBudget.")
                }
                Label { text: qsTr("$/credit"); color: "#9a9aa2" }
                TextField {
                    Layout.preferredWidth: 70
                    text: higgsfield.dollarsPerCredit.toString()
                    validator: DoubleValidator { bottom: 0.0001; decimals: 4; notation: DoubleValidator.StandardNotation }
                    inputMethodHints: Qt.ImhFormattedNumbersOnly
                    onEditingFinished: higgsfield.dollarsPerCredit = Number.fromLocaleString(Qt.locale(), text)
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("What a credit costs you. $0.05 is Plus auto-refill (20 credits per dollar); top-up packs are about $0.053–0.059.")
                }
                Item { Layout.fillWidth: true }
                Button {
                    flat: true
                    visible: meter.counting
                    text: qsTr("All time")
                    onClicked: higgsfield.clearSince()
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Stop counting from the reset; show everything you've spent")
                }
                Button {
                    flat: true
                    text: qsTr("Reset")
                    onClicked: higgsfield.resetSince()
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Count spending from now, e.g. before asking an agent for a new reel")
                }
            }

            Label {
                visible: higgsfield.connected && higgsfield.recent.length > 0
                text: qsTr("Recent charges")
                color: "#9a9aa2"
                font.pixelSize: 12
            }
            ListView {
                visible: higgsfield.connected && higgsfield.recent.length > 0
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(contentHeight, 220)
                clip: true
                model: higgsfield.recent
                ScrollBar.vertical: ScrollBar {}
                delegate: RowLayout {
                    required property var modelData
                    width: ListView.view.width
                    spacing: 8
                    readonly property bool before: meter.counting && modelData.time < higgsfield.since
                    opacity: before ? 0.5 : 1
                    Label {
                        text: modelData.time.toLocaleTimeString(Qt.locale(), Locale.ShortFormat)
                        color: "#9a9aa2"
                        font.pixelSize: 12
                        Layout.preferredWidth: 56
                    }
                    Label {
                        text: modelData.name !== "" ? modelData.name : modelData.action
                        elide: Text.ElideRight
                        font.pixelSize: 12
                        Layout.fillWidth: true
                    }
                    Label {
                        text: (modelData.dollars < 0 ? "−$" : "+$") + Math.abs(modelData.dollars).toLocaleString(Qt.locale(), 'f', 2)
                        color: modelData.dollars < 0 ? "#ffffff" : "#81c784"
                        font.pixelSize: 12
                    }
                }
            }

            RowLayout {
                visible: higgsfield.connected
                Layout.fillWidth: true
                Label {
                    Layout.fillWidth: true
                    color: "#9a9aa2"
                    font.pixelSize: 11
                    text: higgsfield.loaded ? qsTr("Updated %1 · every 20 s").arg(higgsfield.updated.toLocaleTimeString(Qt.locale(), Locale.ShortFormat)) : ""
                }
                Button { flat: true; text: qsTr("Sign out"); onClicked: higgsfield.signOut() }
            }
        }
    }
}
