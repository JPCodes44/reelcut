import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Post to Instagram or TikTok: type the caption and hashtags, press Next.
//
// Instagram, with an account connected, is published through Instagram's
// official API (src/instagramapi.h): no browser involved. Everything else is
// done by hand, with Reelcut doing the parts around it: the reel exported,
// the site opened, the video shown to drag in and the caption copied to
// paste. Nothing is filled into a website for you; only an API may post.
Popup {
    id: root
    objectName: "postDialog"
    property string platform: backend.platform === "tiktok" ? "tiktok" : "instagram"
    // "write", "connect", "scheduler" (set up), "exporting", "publishing", "published",
    // "scheduled" or "ready" (by hand)
    property string stage: "write"
    property string file: ""
    property string steps: ""
    // Publish through Instagram's API when an account is connected.
    property bool useApi: true
    readonly property bool canUseApi: platform === "instagram" && backend.instagramUser.length > 0
    readonly property bool publishing: canUseApi && useApi
    property string progressText: ""
    property real progressValue: -1
    property bool publishedOk: false
    property url postLink
    // Post later instead of now: through Reelcut's scheduler (src/postscheduler.h).
    property bool scheduleLater: false
    readonly property bool scheduling: publishing && scheduleLater && backend.schedulerReady
    readonly property var when: backend.parseWhen(whenField.text)
    // Posts on the scheduler still to go out, or that didn't.
    readonly property var pending: backend.scheduledPosts.filter(function(p) {
        return p.status === "scheduled" || p.status === "publishing" || p.status === "failed"
    }).sort(function(a, b) { return a.publishAt - b.publishAt })
    readonly property var check: backend.checkPost(platform, captionField.text, tagsField.text)
    readonly property string platformLabel: platform === "tiktok" ? "TikTok" : "Instagram"

    function openFor(p) {
        if (p) platform = p
        stage = "write"
        captionField.text = backend.postCaption
        tagsField.text = backend.postHashtags
        if (!root.when.ok)
            whenField.text = backend.suggestWhen()
        if (backend.schedulerReady)
            backend.refreshScheduled()
        open()
        captionField.forceActiveFocus()
    }

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(parent.width - 48, 560)
    height: Math.min(parent.height - 48, implicitHeight)
    modal: true
    padding: 24
    closePolicy: stage === "exporting" || stage === "publishing" ? Popup.NoAutoClose
                                                                  : Popup.CloseOnEscape | Popup.CloseOnPressOutside
    background: Rectangle { color: "#1c1c21"; radius: 14; border.color: "#3a3a40" }

    Connections {
        target: backend
        function onPostOpened(platform, file, steps) {
            root.file = file
            root.steps = steps
            root.stage = "ready"
        }
        function onPostProgress(text, fraction) {
            root.stage = "publishing"
            root.progressText = text
            root.progressValue = fraction
        }
        function onPostPublished(ok, message, link) {
            root.publishedOk = ok
            root.progressText = message
            root.postLink = link
            root.stage = "published"
        }
        function onPostScheduled(ok, message) {
            root.publishedOk = ok
            root.progressText = message
            root.stage = "scheduled"
        }
        function onInstagramConnected(ok, message) {
            connectError.text = ok ? "" : message
            if (ok) {
                tokenField.text = ""
                root.stage = "write"
            }
        }
        // An export that stops without posting (failed or cancelled) goes back to writing.
        function onJobChanged() {
            if (root.stage === "exporting" && !backend.busy)
                Qt.callLater(function() { if (root.stage === "exporting") root.stage = "write" })
        }
    }

    contentItem: ScrollView {
        contentWidth: availableWidth
        clip: true
        ColumnLayout {
            width: parent.width
            spacing: 12

            Label {
                text: root.stage === "connect" ? qsTr("Connect Instagram")
                      : root.stage === "published" ? (root.publishedOk ? qsTr("Posted on Instagram") : qsTr("Not posted"))
                      : root.stage === "scheduled" ? (root.publishedOk ? qsTr("Scheduled") : qsTr("Not scheduled"))
                      : root.stage === "scheduler" ? qsTr("Set up scheduling")
                      : root.stage === "ready" ? qsTr("Ready to post on %1").arg(root.platformLabel)
                      : qsTr("Post your reel")
                Layout.fillWidth: true
                font.pixelSize: 20
                font.bold: true
                wrapMode: Text.Wrap
            }

            // ---- Write ----
            RowLayout {
                visible: root.stage === "write"
                Layout.fillWidth: true
                spacing: 8
                Repeater {
                    model: [{ id: "instagram", label: "Instagram" }, { id: "tiktok", label: "TikTok" }]
                    Button {
                        required property var modelData
                        objectName: "postTo-" + modelData.id
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        text: modelData.label
                        checkable: true
                        checked: root.platform === modelData.id
                        highlighted: checked
                        flat: !checked
                        onClicked: root.platform = modelData.id
                    }
                }
            }
            // Each field label sits tight above its field.
            ColumnLayout {
                visible: root.stage === "write"
                Layout.fillWidth: true
                spacing: 4
                Label { text: qsTr("Caption") }
                TextArea {
                    id: captionField
                    objectName: "postCaption"
                    Layout.fillWidth: true
                    Layout.minimumHeight: 110
                    wrapMode: TextEdit.Wrap
                    placeholderText: qsTr("What's this reel about? Enter starts a new line.")
                }
            }
            ColumnLayout {
                visible: root.stage === "write"
                Layout.fillWidth: true
                spacing: 4
                Label { text: qsTr("Hashtags") }
                TextField {
                    id: tagsField
                    objectName: "postHashtags"
                    Layout.fillWidth: true
                    placeholderText: qsTr("travel food, #sunset… spaces or commas between them")
                    onAccepted: if (next.enabled) next.clicked()
                }
            }
            // The tags as they'll be posted.
            Flow {
                visible: root.stage === "write" && root.check.tags.length > 0
                Layout.fillWidth: true
                spacing: 8
                Repeater {
                    model: root.check.tags
                    Rectangle {
                        required property string modelData
                        width: tag.implicitWidth + 16
                        height: tag.implicitHeight + 8
                        radius: height / 2
                        color: "#2a2a35"
                        Label { id: tag; anchors.centerIn: parent; text: modelData; color: theme.accent; font.pixelSize: 12 }
                    }
                }
            }
            Label {
                visible: root.stage === "write"
                Layout.fillWidth: true
                text: root.check.maxHashtags > 0
                      ? qsTr("%1 / %2 characters · %3 / %4 hashtags").arg(root.check.chars).arg(root.check.maxChars)
                            .arg(root.check.hashtags).arg(root.check.maxHashtags)
                      : qsTr("%1 / %2 characters · %3 hashtags").arg(root.check.chars).arg(root.check.maxChars)
                            .arg(root.check.hashtags)
                color: root.check.ok ? "#9a9aa2" : "#ff8a80"
                wrapMode: Text.Wrap
                font.pixelSize: 12
            }
            Repeater {
                model: root.stage === "write" ? root.check.problems : []
                Label {
                    required property string modelData
                    Layout.fillWidth: true
                    text: modelData
                    color: "#ff8a80"
                    wrapMode: Text.Wrap
                    font.pixelSize: 12
                }
            }

            // How it'll be posted.
            ColumnLayout {
                visible: root.stage === "write" && root.platform === "instagram"
                Layout.fillWidth: true
                spacing: 4
                CheckBox {
                    objectName: "postUseApi"
                    visible: root.canUseApi
                    Layout.fillWidth: true
                    text: qsTr("Publish directly as @%1 (Instagram's API)").arg(backend.instagramUser)
                    checked: root.useApi
                    onToggled: root.useApi = checked
                }
                RowLayout {
                    visible: root.publishing
                    Layout.fillWidth: true
                    spacing: 8
                    CheckBox {
                        objectName: "postScheduleLater"
                        Layout.fillWidth: true
                        text: qsTr("Schedule for later")
                        enabled: backend.schedulerReady
                        checked: root.scheduleLater && backend.schedulerReady
                        onToggled: {
                            root.scheduleLater = checked
                            if (checked) whenField.forceActiveFocus()
                        }
                    }
                    Button {
                        visible: !backend.schedulerReady
                        flat: true
                        text: qsTr("Set up…")
                        onClicked: root.stage = "scheduler"
                    }
                }
                ColumnLayout {
                    visible: root.scheduling
                    Layout.fillWidth: true
                    Layout.leftMargin: 32
                    spacing: 6
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 6
                        TextField {
                            id: whenField
                            objectName: "postWhen"
                            Layout.fillWidth: true
                            placeholderText: qsTr("tomorrow 9am · fri 18:30 · 2026-11-05 12:00")
                            onAccepted: if (next.enabled) next.clicked()
                        }
                        Repeater {
                            model: [{ label: "−1h", minutes: -60 }, { label: "+1h", minutes: 60 }, { label: "+1d", minutes: 1440 }]
                            Button {
                                required property var modelData
                                flat: true
                                implicitWidth: 52
                                text: modelData.label
                                onClicked: whenField.text = backend.suggestWhen(whenField.text, modelData.minutes)
                            }
                        }
                    }
                    Label {
                        objectName: "postWhenLabel"
                        Layout.fillWidth: true
                        text: root.when.ok ? root.when.label : root.when.problem
                        color: root.when.ok ? theme.accent : "#ff8a80"
                        wrapMode: Text.Wrap
                        font.pixelSize: 12
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    Label {
                        Layout.fillWidth: true
                        text: root.canUseApi ? qsTr("Connected to Instagram.")
                                             : qsTr("Connect an Instagram Creator or Business account to publish straight from Reelcut.")
                        color: "#9a9aa2"
                        wrapMode: Text.Wrap
                        font.pixelSize: 12
                    }
                    Button {
                        objectName: "instagramConnect"
                        flat: true
                        text: root.canUseApi ? qsTr("Disconnect") : qsTr("Connect…")
                        onClicked: {
                            if (root.canUseApi)
                                backend.disconnectInstagram()
                            else {
                                connectError.text = ""
                                root.stage = "connect"
                            }
                        }
                    }
                }
            }
            Label {
                visible: root.stage === "write"
                Layout.fillWidth: true
                text: root.scheduling
                      ? qsTr("Schedule exports the reel and uploads it to your scheduler, which publishes it at that "
                             + "time, even with Reelcut closed and the computer off.")
                      : root.publishing
                      ? qsTr("Next exports the reel and publishes it to Instagram with this caption.")
                      : root.platform === "tiktok"
                        ? qsTr("TikTok only lets audited apps post publicly, so this one's by hand: Next exports the reel, "
                               + "opens TikTok, shows the video to drag in and copies the caption to paste.")
                        : qsTr("Next exports the reel, opens Instagram, shows the video to drag in and copies the caption "
                               + "to paste.")
                color: "#9a9aa2"
                wrapMode: Text.Wrap
                font.pixelSize: 12
            }

            // Upcoming scheduled posts.
            ColumnLayout {
                visible: (root.stage === "write" || root.stage === "scheduled") && root.platform === "instagram"
                         && root.pending.length > 0
                Layout.fillWidth: true
                Layout.topMargin: 4
                spacing: 4
                Label { text: qsTr("Scheduled"); font.bold: true }
                Repeater {
                    model: root.pending.slice(0, 6)
                    RowLayout {
                        required property var modelData
                        objectName: "scheduledPost"
                        Layout.fillWidth: true
                        spacing: 8
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 0
                            Label {
                                Layout.fillWidth: true
                                text: Qt.formatDateTime(new Date(modelData.publishAt), "ddd MMM d, h:mm AP")
                                      + " · " + (modelData.caption.split("\n")[0] || modelData.title)
                                elide: Text.ElideRight
                                font.pixelSize: 13
                            }
                            Label {
                                Layout.fillWidth: true
                                visible: modelData.status !== "scheduled"
                                text: modelData.status === "publishing" ? qsTr("Publishing now…")
                                                                        : qsTr("Failed: %1").arg(modelData.error)
                                color: modelData.status === "failed" ? "#ff8a80" : "#9a9aa2"
                                wrapMode: Text.Wrap
                                font.pixelSize: 11
                            }
                        }
                        Button {
                            visible: modelData.status === "failed"
                            flat: true
                            text: qsTr("Retry")
                            onClicked: backend.retryScheduled(modelData.id)
                        }
                        Button {
                            visible: modelData.status !== "publishing"
                            flat: true
                            text: modelData.status === "failed" ? qsTr("Remove") : qsTr("Cancel")
                            onClicked: modelData.status === "failed" ? backend.removeScheduled(modelData.id)
                                                                     : backend.cancelScheduled(modelData.id)
                        }
                    }
                }
                Label {
                    visible: backend.schedulerError.length > 0
                    Layout.fillWidth: true
                    text: qsTr("Scheduler: %1").arg(backend.schedulerError)
                    color: "#ff8a80"
                    wrapMode: Text.Wrap
                    font.pixelSize: 11
                }
            }

            // ---- Set up the scheduler ----
            Label {
                visible: root.stage === "scheduler"
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                textFormat: Text.StyledText
                text: qsTr("Instagram can't schedule through its API, so Reelcut uses a small backend of your own on "
                           + "Convex that posts at the time you pick. Once, from the <b>scheduler</b> folder of "
                           + "Reelcut's source: <b>npx convex deploy</b>, then <b>npx convex env set --prod "
                           + "REELCUT_SECRET</b> with a long random key. Paste the deployment address and that key here.")
            }
            TextField {
                id: schedulerUrl
                visible: root.stage === "scheduler"
                Layout.fillWidth: true
                placeholderText: "https://….convex.cloud"
            }
            TextField {
                id: schedulerKey
                visible: root.stage === "scheduler"
                Layout.fillWidth: true
                placeholderText: qsTr("Key (REELCUT_SECRET)")
                echoMode: TextInput.Password
            }

            // ---- Connect Instagram ----
            Label {
                visible: root.stage === "connect"
                Layout.fillWidth: true
                textFormat: Text.StyledText
                wrapMode: Text.Wrap
                onLinkActivated: function(link) { backend.openLink(link) }
                text: qsTr("Reelcut publishes through Instagram's official API, which needs a token for your account. "
                           + "Once:<br><br>"
                           + "1. In the Instagram app, switch to a <b>Creator</b> or <b>Business</b> account (Settings → Account type).<br>"
                           + "2. At <a href=\"https://developers.facebook.com/apps\">developers.facebook.com/apps</a>, create an app "
                           + "and add the <b>Instagram</b> product.<br>"
                           + "3. Under <b>API setup with Unified login</b> (or <b>Instagram login</b> for older apps), add your account and press <b>Generate token</b>.<br>"
                           + "4. Paste the token here.<br><br>"
                           + "It's kept in a file only you can read. Older Instagram tokens renew automatically; for Unified Login, paste a new token from Meta when it expires.")
            }
            TextField {
                id: tokenField
                objectName: "instagramToken"
                visible: root.stage === "connect"
                Layout.fillWidth: true
                placeholderText: qsTr("Access token")
                echoMode: TextInput.Password
                onAccepted: connectButton.clicked()
            }
            Label {
                id: connectError
                visible: root.stage === "connect" && text.length > 0
                Layout.fillWidth: true
                color: "#ff8a80"
                wrapMode: Text.Wrap
                font.pixelSize: 12
            }

            // ---- Exporting / publishing ----
            ColumnLayout {
                visible: root.stage === "exporting" || root.stage === "publishing"
                Layout.fillWidth: true
                spacing: 8
                Label {
                    objectName: "postProgressText"
                    Layout.fillWidth: true
                    text: root.stage === "exporting" ? (backend.busyText || qsTr("Exporting…")) : root.progressText
                    wrapMode: Text.Wrap
                }
                ProgressBar {
                    Layout.fillWidth: true
                    indeterminate: root.stage === "publishing" && root.progressValue < 0
                    value: root.stage === "exporting" ? backend.progress : Math.max(0, root.progressValue)
                }
            }

            // ---- Published ----
            Label {
                visible: root.stage === "published"
                Layout.fillWidth: true
                text: root.publishedOk ? root.progressText
                                       : root.progressText + "\n\n" + qsTr("Instagram is open with the video shown and the caption copied, so you can post it by hand.")
                color: root.publishedOk ? "white" : "#ff8a80"
                wrapMode: Text.Wrap
            }

            // ---- Scheduled ----
            Label {
                objectName: "postScheduledText"
                visible: root.stage === "scheduled"
                Layout.fillWidth: true
                text: root.progressText
                color: root.publishedOk ? "white" : "#ff8a80"
                wrapMode: Text.Wrap
            }

            // ---- Ready (by hand) ----
            Label {
                visible: root.stage === "ready"
                Layout.fillWidth: true
                text: qsTr("%1 is open in your browser, %2 is selected in the file window, and the caption is copied.")
                          .arg(root.platformLabel).arg(root.file.split("/").pop())
                wrapMode: Text.Wrap
            }
            Repeater {
                model: root.stage === "ready" ? root.steps.split("\n") : []
                RowLayout {
                    required property string modelData
                    required property int index
                    Layout.fillWidth: true
                    spacing: 12
                    Rectangle {
                        Layout.alignment: Qt.AlignTop
                        Layout.preferredWidth: 24
                        Layout.preferredHeight: 24
                        radius: 12
                        color: theme.accent
                        Label { anchors.centerIn: parent; text: index + 1; color: theme.accentForeground; font.bold: true }
                    }
                    Label { Layout.fillWidth: true; text: modelData; wrapMode: Text.Wrap }
                }
            }
            RowLayout {
                visible: root.stage === "ready" || (root.stage === "published" && !root.publishedOk)
                Layout.fillWidth: true
                spacing: 8
                Button { Layout.fillWidth: true; Layout.preferredWidth: 1; text: qsTr("Show video again"); onClicked: backend.showPostVideo() }
                Button { Layout.fillWidth: true; Layout.preferredWidth: 1; text: qsTr("Copy caption again"); onClicked: backend.copyPostText() }
            }

            // ---- Buttons ----
            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 8
                spacing: 8
                Item { Layout.fillWidth: true }
                Button {
                    visible: root.stage !== "publishing"
                    text: root.stage === "exporting" ? qsTr("Stop")
                          : root.stage === "connect" || root.stage === "scheduler" ? qsTr("Back")
                          : root.stage === "ready" || root.stage === "published" || root.stage === "scheduled"
                            ? qsTr("Done") : qsTr("Cancel")
                    flat: true
                    onClicked: {
                        if (root.stage === "exporting")
                            backend.cancelJob()
                        else if (root.stage === "connect" || root.stage === "scheduler")
                            root.stage = "write"
                        else
                            root.close()
                    }
                }
                Button {
                    visible: root.stage === "published" && root.publishedOk && root.postLink.toString().length > 0
                    text: qsTr("Open post")
                    highlighted: true
                    Material.foreground: theme.accentForeground
                    onClicked: backend.openLink(root.postLink)
                }
                Button {
                    visible: root.stage === "scheduler"
                    text: qsTr("Save")
                    highlighted: true
                    Material.foreground: theme.accentForeground
                    enabled: schedulerUrl.text.trim().startsWith("https://") && schedulerKey.text.trim().length >= 16
                    onClicked: {
                        backend.setupScheduler(schedulerUrl.text, schedulerKey.text)
                        schedulerKey.text = ""
                        root.scheduleLater = true
                        root.stage = "write"
                    }
                }
                Button {
                    id: connectButton
                    objectName: "instagramConnectGo"
                    visible: root.stage === "connect"
                    text: qsTr("Connect")
                    highlighted: true
                    Material.foreground: theme.accentForeground
                    enabled: tokenField.text.trim().length > 0
                    onClicked: {
                        connectError.text = qsTr("Checking with Instagram…")
                        backend.connectInstagram(tokenField.text)
                    }
                }
                Button {
                    id: next
                    objectName: "postNext"
                    visible: root.stage === "write"
                    text: root.scheduling ? qsTr("Schedule") : root.publishing ? qsTr("Publish") : qsTr("Next")
                    highlighted: true
                    Material.foreground: theme.accentForeground
                    enabled: root.check.ok && backend.clips.length > 0 && !backend.busy
                             && (!root.scheduling || root.when.ok)
                    onClicked: {
                        root.stage = "exporting"
                        backend.post(root.platform, captionField.text, tagsField.text, root.publishing,
                                     root.scheduling ? root.when.ms : 0)
                        // If it couldn't start (a missing clip, say), the message bar says why.
                        Qt.callLater(function() { if (root.stage === "exporting" && !backend.busy) root.stage = "write" })
                    }
                }
            }
        }
    }
}
