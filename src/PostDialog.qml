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
    signal manageAccounts(string platform)
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
    property bool scheduleFromCalendar: false
    // Publish straight away: only when picked. By default a post goes into
    // the next open slots of the schedule instead (Timing on the Schedule page).
    property bool publishNow: false
    property int queueCount: 1
    readonly property bool canSchedule: publishing && (!hasInstagram || backend.schedulerReady)
    readonly property bool scheduling: canSchedule && scheduleLater
    readonly property bool queueing: publishing && !scheduleLater && !publishNow
    // Ticks each minute while open, so the slot preview drops slots that pass.
    property int clock: 0
    Timer { interval: 60000; repeat: true; running: root.visible; onTriggered: root.clock++ }
    // The slots the reel would go into: platform, publishAt, label.
    readonly property var queueSlots: {
        root.clock; backend.calendarPosts; backend.schedulePreferences
        return queueing && canSchedule ? backend.nextOpenSlots(selected, queueCount) : []
    }
    readonly property bool queueReady: queueing && canSchedule && queueSlots.length === selected.length * queueCount
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
        scheduleFromCalendar = false
        scheduleLater = false
        publishNow = false
        queueCount = 1
        selected = [p || (backend.platform === "tiktok" ? "tiktok" : backend.platform)]
        stage = "write"
        captionField.text = backend.postCaption
        tagsField.text = backend.postHashtags
        titleField.text = backend.title
        whenField.text = backend.suggestWhen()
        if (backend.schedulerReady)
            backend.refreshScheduled()
        open()
        if (hasYouTube) titleField.forceActiveFocus()
        else captionField.forceActiveFocus()
    }
    function openScheduled(p, publishAt) {
        if (["instagram", "facebook", "youtube"].indexOf(p) < 0 || !isFinite(publishAt) || publishAt <= Date.now())
            return
        openFor(p)
        useApi = true
        scheduleFromCalendar = true
        scheduleLater = true
        whenField.text = Qt.formatDateTime(new Date(publishAt), "yyyy-MM-dd HH:mm")
        if (!connected(p)) startConnect(p)
        else if (hasInstagram && !backend.schedulerReady) stage = "scheduler"
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
    onClosed: backend.accountSettings.cancelSelection()

    function automationDraft() {
        return {visible:visible,stage:stage,platforms:selected,title:titleField.text,caption:captionField.text,hashtags:tagsField.text,
            useApi:useApi,scheduleLater:scheduleLater,publishNow:publishNow,queueCount:queueCount,
            queueSlots:queueSlots,when:whenField.text,check:check,whenCheck:whenCheck,progressText:progressText,
            progressValue:progressValue,results:results,file:file,steps:steps,submitEnabled:next.enabled,submitLabel:next.text,
            scheduleFromCalendar:scheduleFromCalendar,publishedOk:publishedOk,postLink:postLink.toString()}
    }
    function automationSetDraft(fields) {
        if (!visible || stage !== "write") return {isError:true,error:"Open the Post draft before editing it."}
        const allowed=["title","caption","hashtags","platforms","useApi","scheduleLater","publishNow","queueCount","when"]
        if (!fields || typeof fields !== "object" || Array.isArray(fields) || !Object.keys(fields).every(function(k){return allowed.indexOf(k)>=0})) return {isError:true,error:"Unknown Post draft field."}
        for (const key of Object.keys(fields)) {
            if (["title","caption","hashtags","when"].indexOf(key)>=0 && typeof fields[key] !== "string") return {isError:true,error:key+" must be a string."}
            if (["useApi","scheduleLater","publishNow"].indexOf(key)>=0 && typeof fields[key] !== "boolean") return {isError:true,error:key+" must be boolean."}
            if (key==="queueCount" && (typeof fields[key] !== "number" || fields[key]<1 || fields[key]>30 || Math.floor(fields[key])!==fields[key])) return {isError:true,error:"queueCount must be a whole number from 1 to 30."}
        }
        if (fields.platforms !== undefined && (!Array.isArray(fields.platforms) || fields.platforms.length===0 || !fields.platforms.every(function(p){return ["instagram","facebook","youtube","tiktok"].indexOf(p)>=0}))) return {isError:true,error:"Choose valid posting platforms."}
        if (fields.platforms !== undefined && fields.platforms.indexOf("tiktok")>=0 && fields.platforms.length>1) return {isError:true,error:"TikTok must be posted on its own, as in the UI."}
        if (fields.title !== undefined) titleField.text=fields.title
        if (fields.caption !== undefined) captionField.text=fields.caption
        if (fields.hashtags !== undefined) tagsField.text=fields.hashtags
        if (fields.platforms !== undefined) selected=["instagram","facebook","youtube","tiktok"].filter(function(p){return fields.platforms.indexOf(p)>=0})
        if (fields.useApi !== undefined) useApi=fields.useApi
        if (fields.scheduleLater !== undefined) { scheduleFromCalendar=false; scheduleLater=fields.scheduleLater; if (scheduleLater) publishNow=false }
        if (fields.publishNow !== undefined) { publishNow=fields.publishNow; if (publishNow) { scheduleFromCalendar=false; scheduleLater=false } }
        if (fields.queueCount !== undefined) queueCount=fields.queueCount
        if (fields.when !== undefined) whenField.text=fields.when
        return automationDraft()
    }
    function automationSubmit() {
        if (!visible || stage !== "write" || !next.enabled) return {isError:true,error:"The Post draft is not ready. Inspect its validation messages first."}
        next.clicked();return automationDraft()
    }

    function startConnect(id) {
        backend.accountSettings.cancelSelection()
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
            stage = scheduleFromCalendar && hasInstagram && !backend.schedulerReady ? "scheduler" : "write"
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
                visible: root.stage === "write" && root.hasYouTube
                Layout.fillWidth: true
                spacing: 4
                RowLayout {
                    Layout.fillWidth: true
                    Label { text: qsTr("YouTube Shorts title") }
                    Item { Layout.fillWidth: true }
                    Label { text: titleField.text.length + " / 100"; color: "#9a9aa2"; font.pixelSize: 12 }
                }
                TextField {
                    id: titleField
                    objectName: "postTitle"
                    Layout.fillWidth: true
                    maximumLength: 100
                    placeholderText: qsTr("Add a title for your Short")
                }
            }
            ColumnLayout {
                visible: root.stage === "write"
                Layout.fillWidth: true
                spacing: 4
                Label { text: root.hasYouTube && root.selected.length === 1 ? qsTr("Description (optional)") : root.hasYouTube ? qsTr("Caption / YouTube description") : qsTr("Caption") }
                TextArea {
                    id: captionField
                    objectName: "postCaption"
                    Layout.fillWidth: true
                    Layout.minimumHeight: 110
                    wrapMode: TextEdit.Wrap
                    placeholderText: root.hasYouTube && root.selected.length === 1
                                     ? qsTr("Add details or hashtags for viewers who open the description")
                                     : qsTr("What's this reel about? Enter starts a new line.")
                }
            }
            Label {
                visible: root.stage === "write" && root.hasYouTube
                Layout.fillWidth: true
                text: qsTr("The Shorts preview shows your title. The description and hashtags are saved with the video for viewers to open.")
                color: "#9a9aa2"
                font.pixelSize: 12
                wrapMode: Text.WordWrap
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
                        // Leaving "write" destroys this row, so act once its click is over.
                        onClicked: {
                            const id = modelData
                            const connect = !root.connected(id)
                            Qt.callLater(function() {
                                if (connect)
                                    root.startConnect(id)
                                else if (id === "instagram")
                                    backend.disconnectInstagram()
                                else if (id === "facebook")
                                    backend.disconnectFacebook()
                                else
                                    backend.disconnectYouTube()
                            })
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
                    RadioButton {
                        objectName: "postQueue"
                        text: qsTr("Next open slots")
                        checked: root.queueing
                        onClicked: { root.scheduleFromCalendar = false; root.scheduleLater = false; root.publishNow = false }
                    }
                    RadioButton {
                        objectName: "postScheduleLater"
                        text: qsTr("At a set time")
                        enabled: root.canSchedule
                        checked: root.scheduleLater && root.canSchedule
                        onClicked: {
                            root.scheduleFromCalendar = false
                            root.publishNow = false
                            root.scheduleLater = true
                            whenField.forceActiveFocus()
                        }
                    }
                    RadioButton {
                        objectName: "postNow"
                        text: qsTr("Publish now")
                        checked: root.publishNow && !root.scheduleLater
                        onClicked: { root.scheduleFromCalendar = false; root.scheduleLater = false; root.publishNow = true }
                    }
                    Item { Layout.fillWidth: true }
                    Button {
                        visible: !root.canSchedule
                        flat: true
                        text: qsTr("Set up…")
                        onClicked: root.stage = "scheduler"
                    }
                }
                // How many slots, and which ones it'll take.
                ColumnLayout {
                    visible: root.queueing
                    Layout.fillWidth: true
                    Layout.leftMargin: 32
                    spacing: 6
                    RowLayout {
                        spacing: 8
                        Label { text: qsTr("Slots on each platform"); font.pixelSize: 12 }
                        SpinBox {
                            objectName: "postQueueCount"
                            from: 1; to: 30
                            value: root.queueCount
                            editable: true
                            onValueModified: root.queueCount = value
                        }
                    }
                    Label {
                        objectName: "postQueueSlots"
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        font.pixelSize: 12
                        color: root.queueReady ? theme.accent : "#ff8a80"
                        text: !root.canSchedule
                              ? (root.hasInstagram ? qsTr("Set up the scheduler to queue posts on Instagram.")
                                                   : qsTr("Connect these platforms to queue posts."))
                              : root.queueSlots.length === 0
                              ? qsTr("No open slots fit. Check Timing on the Schedule page.")
                              : root.selected.map(function(id) {
                                    const times = root.queueSlots.filter(function(s) { return s.platform === id })
                                                                 .map(function(s) { return s.label })
                                    return root.label(id) + ": " + (times.length ? times.join(" · ") : qsTr("no open slot"))
                                }).join("\n")
                                + (root.queueReady ? "" : "\n" + qsTr("Not enough open slots for that many. Try fewer."))
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
                text: root.queueing
                      ? qsTr("Add to queue exports the reel once and schedules it into the next open slots of your daily "
                             + "schedule; when today's slots are used up it rolls on to the next day. Nothing is posted now.")
                      : root.scheduling
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

            Label {
                visible: root.stage === "write" && root.scheduleFromCalendar && !root.canSchedule
                Layout.fillWidth: true
                text: qsTr("Connect the selected account and set up scheduling before confirming this slot.")
                color: "#ffbd69"
                wrapMode: Text.Wrap
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
                                      + " · " + ((modelData.caption || "").split("\n")[0] || modelData.title || "")
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
                            // The list is rebuilt from the change, this row with it.
                            onClicked: { const id = modelData.id; Qt.callLater(function() { backend.retryScheduled(id) }) }
                        }
                        Button {
                            visible: !held && modelData.status !== "publishing"
                            flat: true
                            text: modelData.status === "failed" ? qsTr("Remove") : qsTr("Cancel")
                            onClicked: {
                                const id = modelData.id
                                const failed = modelData.status === "failed"
                                Qt.callLater(function() {
                                    if (failed) backend.removeScheduled(id)
                                    else backend.cancelScheduled(id)
                                })
                            }
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
                objectName: "postSchedulerKey"
                visible: root.stage === "scheduler"
                Layout.fillWidth: true
                placeholderText: qsTr("Key (REELCUT_SECRET)")
                echoMode: TextInput.Password
            }

            // ---- Connect ----
            Button {
                visible: root.stage === "write" || root.stage === "connect"
                text: qsTr("Manage accounts in Settings")
                flat: true
                onClicked: root.manageAccounts(root.stage === "connect" ? root.connectFor : root.selected[0] || "instagram")
            }
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
                             + "4. Paste its client ID and secret here, then sign in in the browser. Choose the YouTube channel you want to post to, including its Brand Account if applicable.<br><br>Check the connected channel name before posting. Switching channels on youtube.com does not change Reelcut’s connection; disconnect and sign in again to change it.<br><br>"
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
                    // Choosing leaves "connect", which destroys this button.
                    onClicked: { const id = modelData.id; Qt.callLater(function() { backend.chooseFacebookPage(id) }) }
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
                        visible: modelData.ok && (modelData.link || "").length > 0
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
                    text: root.queueing ? qsTr("Add to queue")
                          : root.scheduleFromCalendar || root.scheduling ? qsTr("Schedule") : root.publishing ? qsTr("Publish") : qsTr("Next")
                    highlighted: true
                    Material.foreground: theme.accentForeground
                    enabled: root.check.ok && !root.blocked && backend.clips.length > 0 && !backend.busy
                             && (!root.scheduleFromCalendar || root.scheduling)
                             && (!root.scheduling || root.whenCheck.ok)
                             && (!root.queueing || root.queueReady)
                    onClicked: {
                        if (root.scheduleFromCalendar && !root.scheduling) return
                        root.stage = "exporting"
                        root.results = []
                        if (root.queueing)
                            backend.queuePost(root.selected, captionField.text, tagsField.text, titleField.text, root.queueCount)
                        else if (root.publishing)
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
