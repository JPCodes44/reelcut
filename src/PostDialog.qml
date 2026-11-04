import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Post a reel: pick where, type the caption and hashtags, press Next.
//
// Instagram, Facebook (a Page) and YouTube, with an account connected, are
// published through their official APIs (src/publisher.h), and can be picked
// together: one export goes to each in turn. Anything else, TikTok always, is
// done by hand, with Reelcut doing the parts around it: the reel exported,
// the site opened, the video shown to drag in and the caption copied to
// paste. Nothing is filled into a website for you; only an API may post.
Popup {
    id: root
    objectName: "postDialog"
    // Where to post. Instagram, Facebook and YouTube combine; TikTok goes alone.
    property var selected: ["instagram"]
    readonly property string platform: selected.length > 0 ? selected[0] : "instagram"
    // "write", "connect" (connectFor), "scheduler" (set up), "exporting",
    // "publishing", "published", "scheduled" or "ready" (by hand)
    property string stage: "write"
    property string connectFor: "instagram"
    property string file: ""
    property string steps: ""
    // Publish Instagram through its API when an account is connected.
    property bool useApi: true
    readonly property bool canUseApi: backend.instagramUser.length > 0
    readonly property var accounts: ({
        instagram: backend.instagramUser.length > 0 ? "@" + backend.instagramUser : "",
        facebook: backend.facebookPage,
        youtube: backend.youtubeChannel
    })
    function connected(id) { return (accounts[id] || "").length > 0 }
    function viaApi(id) { return connected(id) && (id !== "instagram" || useApi) }
    function label(id) { return { instagram: "Instagram", facebook: "Facebook", youtube: "YouTube", tiktok: "TikTok" }[id] || id }
    readonly property bool publishing: selected.length > 0 && selected.every(viaApi)
    // Several platforms only go through their APIs.
    readonly property var unconnected: selected.filter(function(id) { return !viaApi(id) })
    readonly property bool blocked: selected.length > 1 && !publishing
    readonly property bool hasYouTube: selected.indexOf("youtube") >= 0
    readonly property bool hasInstagram: selected.indexOf("instagram") >= 0
    property string progressText: ""
    property real progressValue: -1
    property bool publishedOk: false
    property url postLink
    // How each platform of the last post went: platform, ok, message, link.
    property var results: []
    // Post later instead of now. Facebook and YouTube hold the post
    // themselves; Instagram needs Reelcut's scheduler (src/postscheduler.h).
    property bool scheduleLater: false
    readonly property bool canSchedule: publishing && (!hasInstagram || backend.schedulerReady)
    readonly property bool scheduling: canSchedule && scheduleLater
    readonly property var when: backend.parseWhen(whenField.text)
    readonly property var whenCheck: when.ok ? backend.checkWhen(selected, when.ms) : ({ ok: false, problem: when.problem })
    // Posts still to go out, or that didn't: the scheduler's and those Facebook and YouTube hold.
    readonly property var pending: backend.scheduledPosts.filter(function(p) {
        return p.status === "scheduled" || p.status === "publishing" || p.status === "failed"
    }).sort(function(a, b) { return a.publishAt - b.publishAt })
    readonly property var check: publishing ? backend.checkPosts(selected, captionField.text, tagsField.text, titleField.text)
                                            : backend.checkPost(platform, captionField.text, tagsField.text)
    readonly property string platformLabel: label(platform)
    property var facebookPages: []

    function openFor(p) {
        selected = [p || (backend.platform === "tiktok" ? "tiktok" : backend.platform)]
        stage = "write"
        captionField.text = backend.postCaption
        tagsField.text = backend.postHashtags
        titleField.text = backend.title
        if (!root.when.ok)
            whenField.text = backend.suggestWhen()
        if (backend.schedulerReady)
            backend.refreshScheduled()
        open()
        captionField.forceActiveFocus()
    }
    // TikTok can't be combined: picking it clears the rest, and the reverse.
    // The last one picked can't be cleared.
    function toggle(id) {
        var now = selected.slice()
        var at = now.indexOf(id)
        if (at >= 0) {
            if (now.length > 1) now.splice(at, 1)
        } else if (id === "tiktok" || now.indexOf("tiktok") >= 0) {
            now = [id]
        } else {
            now.push(id)
        }
        selected = ["instagram", "facebook", "youtube", "tiktok"].filter(function(p) { return now.indexOf(p) >= 0 })
    }
    function startConnect(id) {
        connectFor = id
        connectError.text = ""
        facebookPages = []
        stage = "connect"
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
        function onPlatformPosted(platform, ok, message, link) {
            root.results = root.results.concat([{ platform: platform, ok: ok, message: message, link: link.toString() }])
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
        function onInstagramConnected(ok, message) { root.connectDone("instagram", ok, message) }
        function onFacebookConnected(ok, message) { root.connectDone("facebook", ok, message) }
        function onYoutubeConnected(ok, message) { root.connectDone("youtube", ok, message) }
        function onFacebookPages(pages) {
            root.facebookPages = pages
            connectError.text = qsTr("Pick the Page to post to.")
        }
        // An export that stops without posting (failed or cancelled) goes back to writing.
        function onJobChanged() {
            if (root.stage === "exporting" && !backend.busy)
                Qt.callLater(function() { if (root.stage === "exporting") root.stage = "write" })
        }
    }
    function connectDone(id, ok, message) {
        if (stage !== "connect" || connectFor !== id)
            return
        connectError.text = ok ? "" : message
        if (ok) {
            tokenField.text = ""
            secretField.text = ""
            facebookPages = []
            stage = "write"
        }
    }

    contentItem: ScrollView {
        contentWidth: availableWidth
        clip: true
        ColumnLayout {
            width: parent.width
            spacing: 12

            Label {
                text: root.stage === "connect" ? qsTr("Connect %1").arg(root.label(root.connectFor))
                      : root.stage === "published"
                        ? (root.publishedOk ? (root.selected.length === 1 ? qsTr("Posted on %1").arg(root.platformLabel) : qsTr("Posted"))
                                            : (root.results.some(function(r) { return r.ok }) ? qsTr("Not posted everywhere") : qsTr("Not posted")))
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
                    model: backend.platforms
                    Button {
                        required property var modelData
                        objectName: "postTo-" + modelData.id
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        // Several can be on at once: say which, not only by color.
                        text: (checked ? "✓ " : "") + modelData.label
                        checkable: true
                        checked: root.selected.indexOf(modelData.id) >= 0
                        highlighted: checked
                        flat: !checked
                        onClicked: root.toggle(modelData.id)
                    }
                }
            }
            Label {
                visible: root.stage === "write" && root.selected.length === 1 && root.platform !== "tiktok"
                Layout.fillWidth: true
                text: qsTr("Pick more to post to several at once.")
                color: "#9a9aa2"
                font.pixelSize: 12
            }
            // Each field label sits tight above its field.
            ColumnLayout {
                visible: root.stage === "write" && root.publishing && root.hasYouTube
                Layout.fillWidth: true
                spacing: 4
                Label { text: qsTr("YouTube title") }
                TextField {
                    id: titleField
                    objectName: "postTitle"
                    Layout.fillWidth: true
                    maximumLength: 100
                    placeholderText: qsTr("What viewers see under the Short")
                }
            }
            ColumnLayout {
                visible: root.stage === "write"
                Layout.fillWidth: true
                spacing: 4
                Label { text: root.hasYouTube && root.selected.length === 1 ? qsTr("Description") : qsTr("Caption") }
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

            // The accounts posting goes through, one row per platform picked.
            Repeater {
                model: root.stage === "write" ? root.selected.filter(function(id) { return id !== "tiktok" }) : []
                RowLayout {
                    required property string modelData
                    Layout.fillWidth: true
                    spacing: 8
                    Label {
                        Layout.fillWidth: true
                        text: root.connected(modelData)
                              ? qsTr("%1: %2").arg(root.label(modelData)).arg(root.accounts[modelData])
                              : modelData === "instagram" ? qsTr("Connect an Instagram Creator or Business account to publish straight from Reelcut.")
                              : modelData === "facebook" ? qsTr("Connect a Facebook Page to publish straight from Reelcut.")
                              : qsTr("Connect your YouTube channel to publish straight from Reelcut.")
                        color: "#9a9aa2"
                        wrapMode: Text.Wrap
                        font.pixelSize: 12
                    }
                    Button {
                        objectName: modelData + "Connect"
                        flat: true
                        text: root.connected(modelData) ? qsTr("Disconnect") : qsTr("Connect…")
                        onClicked: {
                            if (!root.connected(modelData))
                                root.startConnect(modelData)
                            else if (modelData === "instagram")
                                backend.disconnectInstagram()
                            else if (modelData === "facebook")
                                backend.disconnectFacebook()
                            else
                                backend.disconnectYouTube()
                        }
                    }
                }
            }
            CheckBox {
                objectName: "postUseApi"
                visible: root.stage === "write" && root.hasInstagram && root.canUseApi
                Layout.fillWidth: true
                text: qsTr("Publish directly as @%1 (Instagram's API)").arg(backend.instagramUser)
                checked: root.useApi
                onToggled: root.useApi = checked
            }
            Label {
                visible: root.stage === "write" && root.blocked
                Layout.fillWidth: true
                text: qsTr("Connect %1 to post to several at once.")
                          .arg(root.unconnected.map(function(id) { return root.label(id) }).join(", "))
                color: "#ff8a80"
                wrapMode: Text.Wrap
                font.pixelSize: 12
            }

            // When.
            ColumnLayout {
                visible: root.stage === "write" && root.publishing
                Layout.fillWidth: true
                spacing: 4
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    CheckBox {
                        objectName: "postScheduleLater"
                        Layout.fillWidth: true
                        text: qsTr("Schedule for later")
                        enabled: root.canSchedule
                        checked: root.scheduleLater && root.canSchedule
                        onToggled: {
                            root.scheduleLater = checked
                            if (checked) whenField.forceActiveFocus()
                        }
                    }
                    Button {
                        visible: !root.canSchedule
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
                        text: !root.when.ok ? root.when.problem : root.whenCheck.ok ? root.when.label : root.whenCheck.problem
                        color: root.whenCheck.ok ? theme.accent : "#ff8a80"
                        wrapMode: Text.Wrap
                        font.pixelSize: 12
                    }
                }
            }
            Label {
                visible: root.stage === "write" && !root.blocked
                Layout.fillWidth: true
                text: root.scheduling
                      ? (root.hasInstagram
                         ? qsTr("Schedule exports the reel and hands it over: your scheduler publishes it on Instagram "
                                + "at that time, even with Reelcut closed and the computer off.")
                         : qsTr("Schedule exports the reel and uploads it now; %1 publishes it at that time, even with "
                                + "Reelcut closed.").arg(root.selected.map(function(id) { return root.label(id) }).join(" and ")))
                      : root.publishing
                      ? qsTr("Next exports the reel and publishes it to %1 with this caption.")
                            .arg(root.selected.map(function(id) { return root.label(id) }).join(", "))
                      : root.platform === "tiktok"
                        ? qsTr("TikTok only lets audited apps post publicly, so this one's by hand: Next exports the reel, "
                               + "opens TikTok, shows the video to drag in and copies the caption to paste.")
                        : qsTr("Next exports the reel, opens %1, shows the video to drag in and copies the caption "
                               + "to paste.").arg(root.platformLabel)
                color: "#9a9aa2"
                wrapMode: Text.Wrap
                font.pixelSize: 12
            }

            // Upcoming scheduled posts.
            ColumnLayout {
                visible: (root.stage === "write" || root.stage === "scheduled") && root.pending.length > 0
                Layout.fillWidth: true
                Layout.topMargin: 4
                spacing: 4
                Label { text: qsTr("Scheduled"); font.bold: true }
                Repeater {
                    model: root.pending.slice(0, 6)
                    RowLayout {
                        required property var modelData
                        readonly property bool held: modelData.platform !== "instagram"
                        objectName: "scheduledPost"
                        Layout.fillWidth: true
                        spacing: 8
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 0
                            Label {
                                Layout.fillWidth: true
                                text: root.label(modelData.platform) + " · "
                                      + Qt.formatDateTime(new Date(modelData.publishAt), "ddd MMM d, h:mm AP")
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
                        // Facebook and YouTube hold these: change them there.
                        Button {
                            visible: held && (modelData.link || "").length > 0
                            flat: true
                            text: qsTr("Open")
                            onClicked: backend.openLink(modelData.link)
                        }
                        Button {
                            visible: !held && modelData.status === "failed"
                            flat: true
                            text: qsTr("Retry")
                            onClicked: backend.retryScheduled(modelData.id)
                        }
                        Button {
                            visible: !held && modelData.status !== "publishing"
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

            // ---- Connect ----
            Label {
                visible: root.stage === "connect"
                Layout.fillWidth: true
                textFormat: Text.StyledText
                wrapMode: Text.Wrap
                onLinkActivated: function(link) { backend.openLink(link) }
                text: root.connectFor === "facebook"
                      ? qsTr("Reelcut posts reels to a Facebook <b>Page</b> through Meta's API (it can't post to a personal profile). "
                             + "Once:<br><br>"
                             + "1. At <a href=\"https://developers.facebook.com/apps\">developers.facebook.com/apps</a>, open your app "
                             + "(the same one as Instagram works) and add the <b>pages_show_list</b>, <b>pages_read_engagement</b> "
                             + "and <b>pages_manage_posts</b> permissions.<br>"
                             + "2. Generate a user token with them in the Graph API Explorer, choosing your Page.<br>"
                             + "3. Paste it here, or use your Instagram connection if its token already has them.<br><br>"
                             + "Reelcut keeps only the Page's own token, in a file only you can read.")
                      : root.connectFor === "youtube"
                      ? qsTr("Google only lets apps upload through an OAuth client of your own. Once:<br><br>"
                             + "1. At <a href=\"https://console.cloud.google.com/apis/library/youtube.googleapis.com\">console.cloud.google.com</a>, "
                             + "make a project and enable the <b>YouTube Data API v3</b>.<br>"
                             + "2. Set up the <b>OAuth consent screen</b> (External) and add yourself as a test user. "
                             + "Publish the app to <b>In production</b>, or Google signs you out every 7 days.<br>"
                             + "3. Under <b>Credentials</b>, create an <b>OAuth client ID</b> of type <b>Desktop app</b>.<br>"
                             + "4. Paste its client ID and secret here, then sign in in the browser.<br><br>"
                             + "They're kept in a file only you can read.")
                      : qsTr("Reelcut publishes through Instagram's official API, which needs a token for your account. "
                             + "Once:<br><br>"
                             + "1. In the Instagram app, switch to a <b>Creator</b> or <b>Business</b> account (Settings → Account type).<br>"
                             + "2. At <a href=\"https://developers.facebook.com/apps\">developers.facebook.com/apps</a>, create an app "
                             + "and add the <b>Instagram</b> product.<br>"
                             + "3. Under <b>API setup with Unified login</b> (or <b>Instagram login</b> for older apps), add your account and press <b>Generate token</b>.<br>"
                             + "4. Paste the token here.<br><br>"
                             + "It's kept in a file only you can read. Older Instagram tokens renew automatically; for Unified Login, paste a new token from Meta when it expires.")
            }
            Button {
                objectName: "facebookUseInstagram"
                visible: root.stage === "connect" && root.connectFor === "facebook" && root.facebookPages.length === 0
                         && backend.canConnectFacebookWithInstagram()
                Layout.fillWidth: true
                text: qsTr("Use my Instagram connection")
                onClicked: {
                    connectError.text = qsTr("Checking with Facebook…")
                    backend.connectFacebookWithInstagram()
                }
            }
            TextField {
                id: tokenField
                objectName: root.connectFor + "Token"
                visible: root.stage === "connect" && root.connectFor !== "youtube" && root.facebookPages.length === 0
                Layout.fillWidth: true
                placeholderText: root.connectFor === "youtube" ? qsTr("Client ID") : qsTr("Access token")
                echoMode: TextInput.Password
                onAccepted: if (connectButton.enabled) connectButton.clicked()
            }
            TextField {
                id: clientIdField
                objectName: "youtubeClientId"
                visible: root.stage === "connect" && root.connectFor === "youtube"
                Layout.fillWidth: true
                placeholderText: qsTr("Client ID (….apps.googleusercontent.com)")
            }
            TextField {
                id: secretField
                objectName: "youtubeClientSecret"
                visible: root.stage === "connect" && root.connectFor === "youtube"
                Layout.fillWidth: true
                placeholderText: qsTr("Client secret")
                echoMode: TextInput.Password
                onAccepted: if (connectButton.enabled) connectButton.clicked()
            }
            // Several Pages: pick one.
            Repeater {
                model: root.stage === "connect" && root.connectFor === "facebook" ? root.facebookPages : []
                Button {
                    required property var modelData
                    objectName: "facebookPage-" + modelData.id
                    Layout.fillWidth: true
                    text: modelData.name
                    onClicked: backend.chooseFacebookPage(modelData.id)
                }
            }
            Label {
                id: connectError
                visible: root.stage === "connect" && text.length > 0
                Layout.fillWidth: true
                color: text.endsWith("…") || text === qsTr("Pick the Page to post to.") ? "#9a9aa2" : "#ff8a80"
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
                // Platforms already done, while the rest go.
                Repeater {
                    model: root.stage === "publishing" ? root.results : []
                    Label {
                        required property var modelData
                        Layout.fillWidth: true
                        text: (modelData.ok ? "✓ " : "✗ ") + root.label(modelData.platform)
                        color: modelData.ok ? "#9a9aa2" : "#ff8a80"
                        font.pixelSize: 12
                    }
                }
            }

            // ---- Published / scheduled: one line per platform ----
            Repeater {
                model: root.stage === "published" || root.stage === "scheduled" ? root.results : []
                RowLayout {
                    required property var modelData
                    objectName: "postResult"
                    Layout.fillWidth: true
                    spacing: 8
                    Label {
                        Layout.fillWidth: true
                        text: (root.results.length > 1 ? root.label(modelData.platform) + ": " : "") + modelData.message
                        color: modelData.ok ? "white" : "#ff8a80"
                        wrapMode: Text.Wrap
                    }
                    Button {
                        visible: modelData.ok && modelData.link.length > 0
                        flat: true
                        text: qsTr("Open")
                        onClicked: backend.openLink(modelData.link)
                    }
                }
            }
            Label {
                visible: root.stage === "published" && !root.publishedOk
                Layout.fillWidth: true
                text: qsTr("%1 is open with the video shown and the caption copied, so you can post it there by hand.")
                          .arg(root.label((root.results.filter(function(r) { return !r.ok })[0] || { platform: root.platform }).platform))
                color: "#ff8a80"
                wrapMode: Text.Wrap
            }
            Label {
                objectName: "postScheduledText"
                visible: root.stage === "scheduled" && root.results.length === 0
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
                    objectName: root.connectFor + "ConnectGo"
                    visible: root.stage === "connect" && root.facebookPages.length === 0
                    text: root.connectFor === "youtube" ? qsTr("Sign in with Google") : qsTr("Connect")
                    highlighted: true
                    Material.foreground: theme.accentForeground
                    enabled: root.connectFor === "youtube"
                             ? clientIdField.text.trim().length > 0 && secretField.text.trim().length > 0
                             : tokenField.text.trim().length > 0
                    onClicked: {
                        if (root.connectFor === "youtube") {
                            connectError.text = qsTr("Finish signing in in your browser…")
                            backend.connectYouTube(clientIdField.text, secretField.text)
                        } else if (root.connectFor === "facebook") {
                            connectError.text = qsTr("Checking with Facebook…")
                            backend.connectFacebook(tokenField.text)
                        } else {
                            connectError.text = qsTr("Checking with Instagram…")
                            backend.connectInstagram(tokenField.text)
                        }
                    }
                }
                Button {
                    id: next
                    objectName: "postNext"
                    visible: root.stage === "write"
                    text: root.scheduling ? qsTr("Schedule") : root.publishing ? qsTr("Publish") : qsTr("Next")
                    highlighted: true
                    Material.foreground: theme.accentForeground
                    enabled: root.check.ok && !root.blocked && backend.clips.length > 0 && !backend.busy
                             && (!root.scheduling || root.whenCheck.ok)
                    onClicked: {
                        root.stage = "exporting"
                        root.results = []
                        if (root.publishing)
                            backend.postTo(root.selected, captionField.text, tagsField.text, titleField.text,
                                           root.scheduling ? root.when.ms : 0)
                        else
                            backend.post(root.platform, captionField.text, tagsField.text, false, 0)
                        // If it couldn't start (a missing clip, say), the message bar says why.
                        Qt.callLater(function() { if (root.stage === "exporting" && !backend.busy) root.stage = "write" })
                    }
                }
            }
        }
    }
}
