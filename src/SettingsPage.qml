import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Popup {
    id: root
    objectName: "settingsPage"
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(parent.width - 32, 860)
    height: Math.min(parent.height - 32, 760)
    modal: true
    padding: 24
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    background: Rectangle { color: "#1c1c21"; radius: 14; border.color: "#3a3a40" }
    readonly property var manager: backend.accountSettings
    readonly property var account: { now; return manager.entries[platformPicker.currentIndex] || ({}) }
    property bool replacing: false
    property double now: Date.now() / 1000

    function automationTab(tab) { tabs.currentIndex=tab === "scheduling" ? 1 : 0 }
    function automationTabName() { return tabs.currentIndex === 1 ? "scheduling" : "accounts" }

    function openFor(platform) {
        if (manager.facebookPages.length > 0) platform = "facebook"
        const list = manager.entries
        for (let i = 0; i < list.length; ++i) {
            if (list[i].id === platform) platformPicker.currentIndex = i
        }
        tabs.currentIndex = 0
        open()
    }
    function dateText(seconds) {
        return Qt.formatDateTime(new Date(seconds * 1000), "ddd, d MMM yyyy · h:mm AP t")
    }
    function expiryText(seconds) {
        if (!account.connected) return qsTr("No token saved")
        if (seconds === 0) return qsTr("Never (reported by Meta)")
        if (!(seconds > 0)) return root.account.id === "youtube" ? qsTr("Unknown — Google has not reported an expiry") : qsTr("Unknown — test connection or check Meta's debugger")
        const remaining = seconds - now
        const duration = Math.abs(remaining)
        const amount = duration >= 86400 ? qsTr("%1 days").arg(Math.ceil(duration / 86400))
                         : duration >= 3600 ? qsTr("%1 hours").arg(Math.ceil(duration / 3600))
                         : qsTr("%1 minutes").arg(Math.max(1, Math.ceil(duration / 60)))
        return dateText(seconds) + " · " + (remaining > 0 ? qsTr("in %1").arg(amount) : qsTr("expired %1 ago").arg(amount))
    }
    onClosed: {
        token.text = ""
        schedulerKey.text = ""
        googleSecret.text = ""
        replacing = false
        manager.cancelSelection()
    }
    Timer { interval: 60000; running: root.visible; repeat: true; onTriggered: root.now = Date.now() / 1000 }
    onOpened: root.now = Date.now() / 1000

    contentItem: ColumnLayout {
        spacing: 12
        RowLayout {
            Layout.fillWidth: true
            Label { text: qsTr("Settings"); font.pixelSize: 24; font.bold: true; Layout.fillWidth: true }
            ToolButton { text: qsTr("Close"); onClicked: root.close() }
        }
        TabBar {
            id: tabs
            objectName: "settingsTabs"
            Layout.fillWidth: true
            TabButton { text: qsTr("Connected accounts") }
            TabButton { text: qsTr("Scheduling") }
        }
        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: tabs.currentIndex
            ScrollView {
                id: accountsScroll
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                contentWidth: availableWidth
                ColumnLayout {
                    width: accountsScroll.availableWidth
                    spacing: 14
                    Label {
                        Layout.fillWidth: true
                        text: qsTr("Manage connections independently of your project. Tokens are stored privately on this computer.")
                        color: "#b3b3bd"
                        wrapMode: Text.Wrap
                    }
                    ComboBox {
                        id: platformPicker
                        objectName: "settingsPlatform"
                        Layout.fillWidth: true
                        model: ["Instagram", "Facebook", "YouTube", "TikTok"]
                        enabled: !root.manager.busy && root.manager.facebookPages.length === 0
                        onCurrentIndexChanged: { token.text = ""; googleSecret.text = ""; root.replacing = false }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Label { text: root.account.label || ""; font.pixelSize: 20; font.bold: true; Layout.fillWidth: true }
                        Label { text: root.manager.busy ? qsTr("Checking…") : root.account.status || ""; color: theme.accent }
                    }
                    Label {
                        visible: root.account.connected || false
                        Layout.fillWidth: true
                        text: root.account.id === "instagram" ? "@" + root.account.name : root.account.name || ""
                        font.pixelSize: 16
                        wrapMode: Text.Wrap
                    }
                    Label {
                        visible: root.account.id === "facebook"
                        Layout.fillWidth: true
                        text: qsTr("Connect with a Page token, or a Meta user token with Pages permissions. Pick the Page when more than one is available. This is the same connection used by Post, Stats and Schedule.")
                        color: "#b3b3bd"
                        wrapMode: Text.Wrap
                    }
                    Label {
                        objectName: "facebookTokenHelp"
                        visible: root.account.id === "facebook"
                        Layout.fillWidth: true
                        textFormat: Text.StyledText
                        wrapMode: Text.Wrap
                        color: "#b3b3bd"
                        text: qsTr("Missing <b>read_insights</b>?<br>"
                            + "1. Open <a href=\"https://developers.facebook.com/apps/\">Meta app setup</a>, select your app, then go to <b>Use cases → Manage everything on your Page → Customize → Permissions and features</b>. Click <b>Add</b> beside <b>read_insights</b> if it is not enabled.<br>"
                            + "2. Open <a href=\"https://developers.facebook.com/tools/explorer/\">Meta token tools</a>, refresh the page and select the same app.<br>"
                            + "3. Choose <b>Get User Access Token</b>. Add <b>read_insights</b> along with <b>pages_show_list</b>, <b>pages_read_engagement</b> and <b>pages_manage_posts</b>.<br>"
                            + "4. Generate the token, approve access to your Page, then use <a href=\"https://developers.facebook.com/tools/debug/accesstoken/\">Meta's debugger</a> to extend it when available.<br>"
                            + "5. Click <b>Replace token / Reconnect</b> below, paste the new token and choose your Page. Return to Stats and press <b>Refresh</b>.<br>"
                            + "An existing token cannot gain a permission simply by extending its expiry.")
                        onLinkActivated: function(url) { backend.openLink(url) }
                    }
                    Label {
                        visible: root.account.id === "instagram"
                        Layout.fillWidth: true
                        text: qsTr("Connect a Creator or Business account to publish and schedule reels. Use the Instagram token for your login method; a Facebook Page token is a separate connection.")
                        color: "#b3b3bd"
                        wrapMode: Text.Wrap
                    }
                    Label {
                        objectName: "browserAccountHint"
                        visible: root.account.id === "tiktok"
                        Layout.fillWidth: true
                        text: root.account.id === "tiktok"
                              ? qsTr("Sign in to TikTok in your browser. Reelcut exports your video and prepares the caption; you finish posting on TikTok. No TikTok API token is stored here.")
                              : qsTr("Sign in to YouTube Studio in your browser. Export your video from Reelcut, then upload it in Studio. No YouTube API token is stored here.")
                        wrapMode: Text.Wrap
                        color: "#b3b3bd"
                    }
                    ColumnLayout {
                        visible: root.account.api || false
                        Layout.fillWidth: true
                        spacing: 6
                        Label { text: root.account.id === "youtube" ? qsTr("Access token expires (renews automatically)") : qsTr("Token expires"); font.bold: true }
                        Label {
                            objectName: "tokenExpiry"
                            Layout.fillWidth: true
                            text: root.expiryText(root.account.expiresAt)
                            wrapMode: Text.Wrap
                        }
                        Label { text: root.account.id === "youtube" ? qsTr("Refresh token expires") : qsTr("Data access expires"); font.bold: true; Layout.topMargin: 8 }
                        Label { Layout.fillWidth: true; text: root.expiryText(root.account.dataAccessExpiresAt); wrapMode: Text.Wrap }
                        Label {
                            Layout.fillWidth: true
                            text: qsTr("Last checked: %1").arg(root.account.checkedAt > 0 ? root.dateText(root.account.checkedAt) : qsTr("Not checked"))
                            color: "#b3b3bd"
                            wrapMode: Text.Wrap
                        }
                        Label {
                            Layout.fillWidth: true
                            text: root.account.id === "youtube" ? qsTr("Access tokens renew automatically. Google may not report a refresh-token expiry. Testing-mode OAuth apps may need sign-in again after seven days.") : qsTr("A token with no expiry can still be revoked. Data access has its own deadline.")
                            color: "#b3b3bd"
                            font.pixelSize: 12
                            wrapMode: Text.Wrap
                        }
                    }
                    Label {
                        Layout.fillWidth: true
                        visible: text.length > 0
                        text: root.account.message || ""
                        wrapMode: Text.Wrap
                        color: "#ffd180"
                    }
                    Flow {
                        Layout.fillWidth: true
                        spacing: 8
                        Button {
                            objectName: "settingsTestConnection"
                            visible: root.account.api && root.account.connected
                            text: qsTr("Test connection")
                            enabled: !root.manager.busy && root.manager.facebookPages.length === 0
                            onClicked: root.manager.testConnection(root.account.id)
                        }
                        Button {
                            visible: root.account.api && root.account.connected
                            text: root.account.id === "youtube" ? qsTr("Reconnect / Change channel") : qsTr("Replace token / Reconnect")
                            enabled: !root.manager.busy
                            onClicked: { root.replacing = true; token.forceActiveFocus() }
                        }
                        Button {
                            objectName: "settingsDisconnect"
                            visible: root.account.api && (root.account.connected || root.manager.busy)
                            text: qsTr("Disconnect")
                            onClicked: { token.text = ""; root.manager.disconnectAccount(root.account.id) }
                        }
                        Button { text: qsTr("Open %1").arg(root.account.label || ""); onClicked: backend.openLink(root.account.url) }
                    }
                    ColumnLayout {
                        visible: root.account.api && root.account.id !== "youtube" && (!root.account.connected || root.replacing)
                        Layout.fillWidth: true
                        Label { text: root.account.connected ? qsTr("New access token") : qsTr("Access token"); font.bold: true }
                        TextField {
                            id: token
                            objectName: "settingsToken"
                            Layout.fillWidth: true
                            placeholderText: qsTr("Paste token here")
                            echoMode: TextInput.Password
                            enabled: !root.manager.busy && root.manager.facebookPages.length === 0
                            selectByMouse: true
                            onAccepted: if (connectButton.enabled) connectButton.clicked()
                        }
                        Button {
                            id: connectButton
                            objectName: "settingsConnect"
                            text: root.account.connected ? qsTr("Validate and replace") : qsTr("Connect")
                            highlighted: true
                            Material.foreground: theme.accentForeground
                            enabled: token.text.trim().length > 0 && !root.manager.busy && root.manager.facebookPages.length === 0
                            onClicked: {
                                const value = token.text
                                token.text = ""
                                root.manager.connectToken(root.account.id, value)
                            }
                        }
                    }
                    ColumnLayout {
                        visible: root.account.id === "youtube" && (!root.account.connected || root.replacing)
                        Layout.fillWidth: true
                        Label { Layout.fillWidth: true; text: qsTr("Use your Google Cloud Desktop app OAuth client. Sign in in your browser and choose the channel to publish to."); wrapMode: Text.Wrap }
                        TextField { id: googleClient; objectName: "settingsYouTubeClient"; Layout.fillWidth: true; placeholderText: qsTr("Google OAuth client ID"); enabled: !root.manager.busy }
                        TextField { id: googleSecret; objectName: "settingsYouTubeSecret"; Layout.fillWidth: true; placeholderText: qsTr("Client secret"); echoMode: TextInput.Password; enabled: !root.manager.busy }
                        Button {
                            text: qsTr("Sign in with Google")
                            enabled: !root.manager.busy && googleClient.text.trim().length > 0 && googleSecret.text.trim().length > 0
                            onClicked: { backend.connectYouTube(googleClient.text.trim(), googleSecret.text); googleSecret.text = "" }
                        }
                        Button { text: qsTr("Open Google Cloud setup"); flat: true; onClicked: backend.openLink("https://console.cloud.google.com/apis/credentials") }
                    }
                    ColumnLayout {
                        visible: root.account.id === "facebook" && root.manager.facebookPages.length > 0
                        Layout.fillWidth: true
                        Label { text: qsTr("Choose a Facebook Page"); font.bold: true }
                        Repeater {
                            model: root.manager.facebookPages
                            Button {
                                required property var modelData
                                Layout.fillWidth: true
                                text: modelData.name + " · " + modelData.id
                                onClicked: { const id = modelData.id; Qt.callLater(function() { root.manager.selectFacebookPage(id) }) }
                            }
                        }
                        Button { text: qsTr("Cancel selection"); onClicked: root.manager.cancelSelection() }
                    }
                    Flow {
                        visible: root.account.api && root.account.id !== "youtube"
                        Layout.fillWidth: true
                        spacing: 8
                        Button { text: qsTr("Meta app setup"); flat: true; onClicked: backend.openLink("https://developers.facebook.com/apps/") }
                        Button { text: qsTr("Generate Facebook token"); flat: true; onClicked: backend.openLink("https://developers.facebook.com/tools/explorer/") }
                        Button { text: qsTr("Check / extend token"); flat: true; onClicked: backend.openLink("https://developers.facebook.com/tools/debug/accesstoken/") }
                    }
                    Label {
                        visible: root.account.api && root.account.id !== "youtube"
                        Layout.fillWidth: true
                        text: qsTr("Generate the token in your own Meta app and grant only the access you need. Use Meta's debugger to extend it when available, then replace it here. Reelcut cannot extend Facebook tokens itself. Instagram Login tokens renew automatically when supported; Unified Login tokens need renewal through Meta.")
                        wrapMode: Text.Wrap
                        color: "#b3b3bd"
                        font.pixelSize: 12
                    }
                    Item { Layout.preferredHeight: 8 }
                }
            }
            ScrollView {
                id: schedulingScroll
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                contentWidth: availableWidth
                ColumnLayout {
                    width: schedulingScroll.availableWidth
                    spacing: 12
                    Label { text: qsTr("Instagram scheduling"); font.pixelSize: 20; font.bold: true }
                    Label { text: backend.schedulerReady ? qsTr("Scheduler configured") : qsTr("Not configured"); color: theme.accent }
                    Label {
                        Layout.fillWidth: true
                        text: qsTr("Facebook and YouTube schedule through their platform APIs. Instagram uses your Convex scheduler so reels publish even with Reelcut closed. Deploy it from the scheduler folder and set REELCUT_SECRET, then enter its URL and key here. Saving replaces the current configuration.")
                        wrapMode: Text.Wrap
                        color: "#b3b3bd"
                    }
                    Label { text: qsTr("Deployment URL") }
                    TextField { id: schedulerUrl; objectName: "settingsSchedulerUrl"; Layout.fillWidth: true; placeholderText: "https://….convex.cloud" }
                    Label { text: qsTr("Scheduler key") }
                    TextField { id: schedulerKey; objectName: "settingsSchedulerKey"; Layout.fillWidth: true; echoMode: TextInput.Password; placeholderText: "REELCUT_SECRET" }
                    Button {
                        text: qsTr("Save scheduler")
                        enabled: schedulerUrl.text.trim().startsWith("https://") && schedulerKey.text.trim().length >= 16
                        highlighted: true
                        Material.foreground: theme.accentForeground
                        onClicked: { backend.setupScheduler(schedulerUrl.text.trim(), schedulerKey.text.trim()); schedulerKey.text = "" }
                    }
                    Label { Layout.fillWidth: true; text: backend.schedulerError; visible: text.length > 0; wrapMode: Text.Wrap; color: "#ff8a80" }
                    Label { Layout.fillWidth: true; text: qsTr("Scheduler keys do not report an expiry. Rotate the key in your scheduler and replace it here when needed."); wrapMode: Text.Wrap; color: "#b3b3bd" }
                }
            }
        }
    }
}
