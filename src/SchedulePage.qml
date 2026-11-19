pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import "ScheduleLogic.js" as Plan

Rectangle {
    id: page
    objectName: "schedulePage"
    signal closeRequested()
    signal scheduleRequested(string platform, double publishAt)

    property real selectedDate: Plan.dayStart(Date.now())
    property real now: Date.now()
    property bool timingOpen: false
    property string notice: ""
    property var selectedPost: null
    property var scheduledRecords: backend.calendarPosts
    property var publishedReels: typeof stats !== "undefined" && stats ? stats.reels : []
    readonly property var preferences: backend.schedulePreferences
    readonly property var dailyPosts: Plan.dayPosts(selectedDate, scheduledRecords, publishedReels, now)
    readonly property int dailyCount: dailyPosts.length
    readonly property int targetSlotCount: Plan.platforms.reduce(function(n,p) {
        var pref=page.preferences[p];
        return n + (pref ? Plan.slots(page.selectedDate,pref.startMinute,pref.spacingMinutes).length : 0)
    },0)
    readonly property bool hasReel: backend.clips.length > 0
    readonly property bool refreshing: typeof stats !== "undefined" && stats ? stats.busy : false
    readonly property bool dark: Material.theme === Material.Dark
    readonly property color bg: dark ? "#0e0e10" : "#f3f3f6"
    readonly property color panel: dark ? "#17171a" : "#ffffff"
    readonly property color line: dark ? "#2a2a2f" : "#dadae0"
    readonly property color ink: dark ? "#ececf0" : "#1b1b20"
    readonly property color dim: dark ? "#a3a3ad" : "#60606b"
    readonly property color warning: dark ? "#ffc06a" : "#925000"
    readonly property color danger: dark ? "#ff8a80" : "#b22b2b"
    readonly property real headerWidth: 214
    // Every tile in the longest lane fits across (posts plus open slots, often
    // more than ten); only very narrow windows scroll each row.
    readonly property int maxCards: Plan.platforms.reduce(function(n,p) { return Math.max(n, cardsFor(p).length) }, 10)
    // Page margins 44, header, divider 1, row border 2, list margins 24.
    readonly property real laneWidth: width-44-headerWidth-1-2-24
    readonly property real tileWidth: Math.max(84, Math.min(160, (laneWidth-(maxCards-1)*10)/maxCards))
    readonly property real tileHeight: tileWidth*16/9
    readonly property var labels: ({instagram:qsTr("Instagram"),facebook:qsTr("Facebook"),youtube:qsTr("YouTube"),tiktok:qsTr("TikTok")})
    color: bg

    function platformColor(p) { return {instagram:"#d858ae",facebook:"#558fff",youtube:"#ef6654",tiktok:"#25c4cf"}[p] || page.dim }
    function icon(p) { return {instagram:"camera",facebook:"thumbUp",youtube:"reel",tiktok:"music"}[p] || "reel" }
    function statusText(s) {
        return {planned:qsTr("Planned slot"),scheduled:qsTr("Scheduled"),publishing:qsTr("Publishing"),
                published:qsTr("Published"),awaiting:qsTr("Awaiting confirmation"),failed:qsTr("Failed"),cancelled:qsTr("Cancelled")}[s] || s
    }
    function statusColor(s,p) {
        return s==="failed" ? danger : s==="awaiting" ? warning : s==="published" ? (dark ? "#7bd8b2" : "#18794e")
               : s==="planned" || s==="cancelled" ? dim : platformColor(p)
    }
    function targets(p) {
        var pref=preferences[p];
        return pref ? Plan.slots(selectedDate,pref.startMinute,pref.spacingMinutes) : []
    }
    function cardsFor(p) {
        var actual=dailyPosts.filter(function(post){return post.platform===p;});
        var empty=targets(p).filter(function(slot){
            return !actual.some(function(post){return post.minute===slot.minute && post.status!=="cancelled";});
        }).map(function(slot){
            return Object.assign({},slot,{platform:p,id:"slot-"+slot.slot,status:"planned",source:"slot"});
        });
        return Plan.layoutCards(actual.concat(empty))
    }
    function chooseSlot(platform, ms) {
        notice=""
        if (!isFinite(ms) || ms<=now) { notice=qsTr("Choose a future slot. Past or unavailable times cannot be scheduled."); return }
        if (platform==="tiktok") {
            notice=qsTr("TikTok has no posting API, so this slot is a reminder: at %1, use Post → TikTok and upload it in the browser.").arg(Plan.timeText(new Date(ms).getHours()*60+new Date(ms).getMinutes()))
            return
        }
        if (!hasReel) { notice=qsTr("Open or prepare a reel in the editor, then choose a slot to schedule it."); return }
        if (backend.busy) { notice=qsTr("Wait for the current export or upload to finish."); return }
        scheduleRequested(platform,ms)
    }
    function automationDetailVisible() { return postDetail.visible }
    function automationTimingState() {
        const result={}
        for (let i=0;i<timingRows.count;++i) { const row=timingRows.itemAt(i); if(row) result[row.modelData]=row.automationDraft() }
        return result
    }
    function automationTiming(fields, apply) {
        if (!fields || typeof fields !== "object" || Array.isArray(fields) || !Object.keys(fields).every(function(p){return Plan.platforms.indexOf(p)>=0})) return {isError:true,error:"Unknown timing platform."}
        for (const p of Object.keys(fields)) {
            const value=fields[p]
            if (!value || typeof value !== "object" || Array.isArray(value) || !Object.keys(value).every(function(k){return ["start","spacingMinutes"].indexOf(k)>=0})) return {isError:true,error:"Unknown timing field."}
            if (value.start !== undefined && (typeof value.start !== "string" || Plan.readTime(value.start)<0)) return {isError:true,error:"Use HH:mm for the first slot."}
            if (value.spacingMinutes !== undefined && [60,90,120].indexOf(value.spacingMinutes)<0) return {isError:true,error:"Spacing must be 60, 90 or 120 minutes."}
        }
        const before=automationTimingState()
        if (apply) {
            for (const p of Plan.platforms) {
                const v=fields[p]||{},old=before[p]
                const start=Plan.readTime(v.start !== undefined?v.start:old.start)
                const gap=v.spacingMinutes !== undefined?v.spacingMinutes:old.spacingMinutes
                if (start<0 || start+9*gap>=1440) return {isError:true,error:"All ten slots must fit in the day."}
            }
        }
        for (let i=0;i<timingRows.count;++i) { const row=timingRows.itemAt(i); if(row && fields[row.modelData]) row.automationSet(fields[row.modelData]) }
        timingOpen=true
        if (apply) {
            if(!applyTiming.enabled)return {isError:true,error:"Slot timing is not valid."}
            applyTiming.clicked()
        }
        return {draft:automationTimingState(),notice:notice,preferences:preferences}
    }
    function automationDetail(platform, id) {
        const card=dailyPosts.filter(function(p){return p.platform===platform && String(p.id)===String(id)})[0]
        if (!card) return false
        selectedPost=card;postDetail.open();return true
    }
    function closeDetail() {
        if (!postDetail.visible) return false
        postDetail.close()
        return true
    }
    function refresh() {
        if (backend.schedulerReady) backend.refreshScheduled()
        if (typeof stats!=="undefined" && stats && !stats.busy) stats.refresh()
    }
    function revealDay() {
        for (var i=0;i<platformRows.count;++i) { var row=platformRows.itemAt(i); if (row) row.reveal() }
    }
    function previewFor(post) {
        if (!post || post.source==="slot") return ""
        if (post.thumbnail) return post.thumbnail
        return post.video ? backend.videoFrame(post.video, 1000) : ""
    }
    onSelectedDateChanged: { notice=""; Qt.callLater(revealDay) }
    onVisibleChanged: if(visible) { now=Date.now(); refresh(); Qt.callLater(revealDay) }
    Component.onCompleted: Qt.callLater(revealDay)
    Timer { interval:30000; running:page.visible; repeat:true; onTriggered:page.now=Date.now() }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 22
        spacing: 14
        RowLayout {
            Layout.fillWidth:true
            spacing:10
            ColumnLayout {
                spacing:3
                Label { text:qsTr("Daily schedule"); color:page.ink; font.pixelSize:24; font.bold:true }
                Label { text:qsTr("10 videos per platform, in order through the day · your local time"); color:page.dim; font.pixelSize:12 }
            }
            Item { Layout.fillWidth:true }
            ToolButton { objectName:"schedulePreviousDay"; text:"‹"; font.pixelSize:26; Accessible.name:qsTr("Previous day"); onClicked:page.selectedDate=Plan.addDays(page.selectedDate,-1) }
            ColumnLayout {
                spacing:2
                Label { text:Qt.formatDateTime(new Date(page.selectedDate),"dddd, MMM d"); color:page.ink; font.bold:true; font.pixelSize:16; Layout.alignment:Qt.AlignHCenter }
                Label { text:Qt.formatDateTime(new Date(page.selectedDate),"yyyy · t"); color:page.dim; font.pixelSize:11; Layout.alignment:Qt.AlignHCenter }
            }
            ToolButton { objectName:"scheduleNextDay"; text:"›"; font.pixelSize:26; Accessible.name:qsTr("Next day"); onClicked:page.selectedDate=Plan.addDays(page.selectedDate,1) }
            Button { text:qsTr("Today"); flat:true; onClicked:page.selectedDate=Plan.dayStart(Date.now()) }
            Button { objectName:"scheduleTiming"; text:qsTr("Timing"); checkable:true; checked:page.timingOpen; onClicked:page.timingOpen=!page.timingOpen }
            Button { text:page.refreshing ? qsTr("Refreshing…") : qsTr("Refresh"); enabled:!page.refreshing; flat:true; onClicked:page.refresh() }
            ToolButton { text:"✕"; Accessible.name:qsTr("Close Schedule"); onClicked:page.closeRequested() }
        }

        Rectangle {
            visible:page.timingOpen
            Layout.fillWidth:true
            implicitHeight:timingLayout.implicitHeight+32
            radius:12; color:page.panel; border.color:page.line
            ColumnLayout {
                id:timingLayout
                anchors.fill:parent; anchors.margins:16; spacing:14
                Label { text:qsTr("Plan empty slots. Existing scheduled posts keep their times."); color:page.dim; font.pixelSize:12 }
                // One block per platform: name on top, then first slot and spacing.
                GridLayout {
                    Layout.fillWidth:true
                    columns:Math.max(1, Math.min(4, Math.floor((width+20)/280)))
                    columnSpacing:20; rowSpacing:14
                    Repeater {
                        id:timingRows
                        model:Plan.platforms
                        ColumnLayout {
                            id:timingRow
                            required property string modelData
                            readonly property int start:Plan.readTime(startInput.text)
                            readonly property int gap:[60,90,120][spacingInput.currentIndex]
                            readonly property bool valid:start>=0 && start+9*gap<1440
                            function automationDraft() { return {start:startInput.text,spacingMinutes:gap} }
                            function automationSet(value) {
                                if (value.start !== undefined) startInput.text=value.start
                                if (value.spacingMinutes !== undefined) spacingInput.currentIndex=[60,90,120].indexOf(value.spacingMinutes)
                            }
                            Layout.fillWidth:true; Layout.preferredWidth:1; spacing:6
                            RowLayout {
                                spacing:8
                                Rectangle {
                                    width:20; height:20; radius:5; color:page.platformColor(timingRow.modelData)
                                    SocialIcon { anchors.centerIn:parent; width:13; height:13; name:page.icon(timingRow.modelData) }
                                }
                                Label { text:page.labels[timingRow.modelData]; color:page.ink; font.bold:true; font.pixelSize:13 }
                                Label {
                                    visible:!timingRow.valid
                                    text:qsTr("Doesn't fit"); color:page.warning; font.pixelSize:11
                                }
                            }
                            RowLayout {
                                Layout.fillWidth:true; spacing:8
                                ColumnLayout {
                                    spacing:2
                                    Label { text:qsTr("First slot"); color:page.dim; font.pixelSize:11 }
                                    TextField {
                                        id:startInput
                                        objectName:"scheduleStart-"+timingRow.modelData
                                        Layout.preferredWidth:96
                                        horizontalAlignment:TextInput.AlignHCenter
                                        text:Plan.timeText(page.preferences[timingRow.modelData].startMinute)
                                        selectByMouse:true
                                        inputMethodHints:Qt.ImhTime
                                        Accessible.name:qsTr("%1 first slot, 24-hour time").arg(page.labels[timingRow.modelData])
                                    }
                                }
                                ColumnLayout {
                                    Layout.fillWidth:true; spacing:2
                                    Label { text:qsTr("Spacing"); color:page.dim; font.pixelSize:11 }
                                    ComboBox {
                                        id:spacingInput
                                        objectName:"scheduleGap-"+timingRow.modelData
                                        Layout.fillWidth:true; Layout.minimumWidth:150
                                        model:[qsTr("Every 1 hour"),qsTr("Every 1.5 hours"),qsTr("Every 2 hours")]
                                        currentIndex:Math.max(0,[60,90,120].indexOf(page.preferences[timingRow.modelData].spacingMinutes))
                                        Accessible.name:qsTr("%1 slot spacing").arg(page.labels[timingRow.modelData])
                                    }
                                }
                            }
                        }
                    }
                }
                RowLayout {
                    Layout.fillWidth:true; spacing:16
                    Label {
                        Layout.fillWidth:true; wrapMode:Text.Wrap
                        text:qsTr("Use HH:mm. All ten slots must fit before midnight; with two-hour spacing, start before 06:00.")
                        color:page.dim; font.pixelSize:11
                    }
                    Button {
                        id: applyTiming
                        objectName:"scheduleApplyTiming"; text:qsTr("Apply")
                        enabled:{ for(var i=0;i<timingRows.count;++i) { var row=timingRows.itemAt(i); if(!row || !row.valid) return false } return true }
                        onClicked:{
                            var next={};
                            for(var i=0;i<timingRows.count;++i){var row=timingRows.itemAt(i);next[row.modelData]={startMinute:row.start,spacingMinutes:row.gap};}
                            backend.schedulePreferences=next;
                            page.notice=Plan.platforms.every(function(p) { var saved=backend.schedulePreferences[p]; return saved.startMinute===next[p].startMinute && saved.spacingMinutes===next[p].spacingMinutes })
                                ? qsTr("Slot timing saved. Already scheduled posts were not moved.")
                                : qsTr("Could not save slot timing. Your previous settings are still in use.")
                            Qt.callLater(page.revealDay)
                        }
                    }
                }
            }
        }
        RowLayout {
            Layout.fillWidth:true
            Label {
                objectName:"scheduleNotice"; Layout.fillWidth:true; wrapMode:Text.Wrap
                text:page.notice || (page.hasReel ? qsTr("Current reel: %1 · choose an empty slot to schedule it.").arg(backend.title)
                                                  : qsTr("Open or prepare a reel in the editor, then choose an empty slot."))
                color:page.notice ? page.warning : page.dim; font.pixelSize:12
            }
            Label { text:qsTr("Planning slots do not publish automatically"); color:page.dim; font.pixelSize:11 }
        }
        Label {
            visible:backend.schedulerError.length>0
            Layout.fillWidth:true; wrapMode:Text.Wrap
            text:qsTr("Instagram scheduler: %1").arg(backend.schedulerError); color:page.warning; font.pixelSize:12
        }
        Flickable {
            id:timeScroll
            objectName:"scheduleTimeline"
            Layout.fillWidth:true; Layout.fillHeight:true
            contentWidth:width; contentHeight:rowsColumn.implicitHeight
            clip:true; boundsBehavior:Flickable.StopAtBounds
            ScrollBar.vertical:ScrollBar { policy:ScrollBar.AsNeeded }
            ColumnLayout {
                id:rowsColumn
                width:timeScroll.width
                spacing:12
                Repeater {
                    id:platformRows
                    model:Plan.platforms
                    Rectangle {
                        id:platformRow
                        required property string modelData
                        readonly property var cards:page.cardsFor(modelData)
                        readonly property var summary:Plan.platformSummary(modelData,page.dailyPosts,page.targets(modelData))
                        function reveal() {
                            var at=0
                            for (var i=0;i<cards.length;++i) { if (cards[i].ms>page.now) { at=i; break } at=i }
                            tiles.positionViewAtIndex(Math.max(0,at-1),ListView.Beginning)
                        }
                        Layout.fillWidth:true
                        Layout.preferredHeight:page.tileHeight+58
                        radius:12; color:page.panel; border.color:page.line
                        RowLayout {
                            anchors.fill:parent; anchors.margins:1; spacing:0
                            Item {
                                id:columnHeader
                                objectName:"scheduleColumn-"+platformRow.modelData
                                Layout.preferredWidth:page.headerWidth; Layout.fillHeight:true
                                ColumnLayout {
                                    anchors.fill:parent; anchors.margins:14; spacing:6
                                    RowLayout {
                                        spacing:8
                                        Rectangle {
                                            width:26; height:26; radius:7; color:page.platformColor(platformRow.modelData)
                                            SocialIcon { anchors.centerIn:parent; width:17; height:17; name:page.icon(platformRow.modelData) }
                                        }
                                        Label { text:page.labels[platformRow.modelData]; color:page.ink; font.bold:true; font.pixelSize:15; Layout.fillWidth:true; elide:Text.ElideRight }
                                    }
                                    Label { text:qsTr("%1 / 10").arg(platformRow.summary.covered); color:page.ink; font.bold:true; font.pixelSize:22 }
                                    Label { text:qsTr("%1 scheduled · %2 published").arg(platformRow.summary.scheduled).arg(platformRow.summary.published); color:page.dim; font.pixelSize:11 }
                                    Label { visible:platformRow.summary.failed>0; text:qsTr("%1 failed").arg(platformRow.summary.failed); color:page.danger; font.pixelSize:11 }
                                    Label { visible:platformRow.summary.awaiting>0; text:qsTr("%1 unconfirmed").arg(platformRow.summary.awaiting); color:page.warning; font.pixelSize:11 }
                                    Label { visible:platformRow.modelData==="tiktok"; text:qsTr("Reminders only · posted by hand"); color:page.dim; font.pixelSize:11; Layout.fillWidth:true; wrapMode:Text.Wrap }
                                    Item { Layout.fillHeight:true }
                                }
                            }
                            Rectangle { Layout.preferredWidth:1; Layout.fillHeight:true; color:page.line }
                            ListView {
                                id:tiles
                                objectName:"scheduleRow-"+platformRow.modelData
                                Layout.fillWidth:true; Layout.fillHeight:true
                                orientation:ListView.Horizontal
                                leftMargin:12; rightMargin:12; spacing:10
                                clip:true; boundsBehavior:Flickable.StopAtBounds
                                model:platformRow.cards
                                ScrollBar.horizontal:ScrollBar { policy:ScrollBar.AsNeeded }
                                delegate:Button {
                                    id:postCard
                                    required property var modelData
                                    readonly property string lanePlatform:platformRow.modelData
                                    readonly property bool empty:modelData.source==="slot"
                                    readonly property bool unavailable:empty && !modelData.valid
                                    readonly property bool past:(modelData.ms||0)<=page.now
                                    readonly property color tint:page.statusColor(modelData.status,lanePlatform)
                                    readonly property string preview:page.previewFor(modelData)
                                    enabled:!unavailable
                                    objectName:"scheduleCard-"+lanePlatform+"-"+modelData.id
                                    width:page.tileWidth; height:tiles.height
                                    // Material sets left/right padding itself, so padding:0 alone
                                    // left the card shifted 24 px and the last one cut off.
                                    padding:0; leftPadding:0; rightPadding:0
                                    opacity:empty && past ? 0.5 : 1
                                    Accessible.name:page.labels[lanePlatform]+" "+Plan.timeText(modelData.minute)+" "+page.statusText(modelData.status)+" "+(modelData.title||"")
                                    background:Item {}
                                    contentItem:ColumnLayout {
                                        spacing:6
                                        RowLayout {
                                            Layout.topMargin:8; Layout.fillWidth:true; spacing:6
                                            Label { text:Plan.timeText(postCard.modelData.minute); color:page.ink; font.bold:true; font.pixelSize:13 }
                                            Label {
                                                Layout.fillWidth:true; horizontalAlignment:Text.AlignRight; elide:Text.ElideRight; font.pixelSize:10
                                                text:postCard.empty ? (postCard.unavailable ? qsTr("Unavailable") : postCard.past ? qsTr("Unfilled") : qsTr("Open")) : page.statusText(postCard.modelData.status)
                                                color:postCard.tint
                                            }
                                        }
                                        Rectangle {
                                            id:frame
                                            Layout.preferredWidth:page.tileWidth; Layout.preferredHeight:page.tileHeight
                                            radius:10; clip:true
                                            color:postCard.empty ? (postCard.hovered && !postCard.past ? (page.dark ? "#202026" : "#eeeef3") : "transparent")
                                                  : (page.dark ? "#000000" : "#20202a")
                                            border.width:postCard.empty ? 1 : 2
                                            border.color:postCard.empty ? page.line : Qt.rgba(postCard.tint.r,postCard.tint.g,postCard.tint.b,0.85)
                                            Image {
                                                id:cardThumb
                                                anchors.fill:parent; anchors.margins:frame.border.width
                                                visible:postCard.preview.length>0 && status!==Image.Error
                                                source:postCard.preview
                                                sourceSize.width:Math.ceil(page.tileWidth*2)
                                                fillMode:Image.PreserveAspectCrop; asynchronous:true
                                            }
                                            Rectangle {
                                                objectName:"scheduleFallback-"+postCard.lanePlatform+"-"+postCard.modelData.id
                                                visible:!postCard.empty && !cardThumb.visible
                                                anchors.fill:parent; anchors.margins:frame.border.width; radius:9
                                                color:Qt.darker(page.platformColor(postCard.lanePlatform),1.6)
                                                SocialIcon { anchors.centerIn:parent; width:30; height:30; name:page.icon(postCard.lanePlatform) }
                                            }
                                            Label {
                                                visible:postCard.empty
                                                anchors.centerIn:parent; width:parent.width-16
                                                horizontalAlignment:Text.AlignHCenter; wrapMode:Text.Wrap
                                                text:postCard.unavailable ? qsTr("Time unavailable on this date")
                                                     : postCard.past ? qsTr("Slot %1").arg(postCard.modelData.slot+1)
                                                     : postCard.lanePlatform==="tiktok" ? qsTr("Slot %1\nreminder").arg(postCard.modelData.slot+1)
                                                     : qsTr("+ Add reel\nslot %1").arg(postCard.modelData.slot+1)
                                                color:page.dim; font.pixelSize:12
                                            }
                                            Rectangle {
                                                visible:!postCard.empty
                                                anchors.left:parent.left; anchors.right:parent.right; anchors.bottom:parent.bottom
                                                anchors.margins:frame.border.width
                                                height:titleLabel.implicitHeight+16
                                                gradient:Gradient {
                                                    GradientStop { position:0; color:"#00000000" }
                                                    GradientStop { position:1; color:"#cc000000" }
                                                }
                                                Label {
                                                    id:titleLabel
                                                    anchors.left:parent.left; anchors.right:parent.right; anchors.bottom:parent.bottom; anchors.margins:7
                                                    maximumLineCount:2; wrapMode:Text.Wrap; elide:Text.ElideRight
                                                    text:postCard.modelData.status==="failed" ? (postCard.modelData.error||qsTr("Failed"))
                                                         : String(postCard.modelData.title||postCard.modelData.caption||qsTr("Untitled reel")).replace(/\s+/g," ")
                                                    color:postCard.modelData.status==="failed" ? "#ffb4ab" : "#ffffff"; font.pixelSize:11
                                                }
                                            }
                                        }
                                        Item { Layout.fillHeight:true }
                                    }
                                    onClicked:{
                                        if(empty) page.chooseSlot(lanePlatform,modelData.ms)
                                        else {page.selectedPost=modelData;postDetail.open()}
                                    }
                                    ToolTip.visible:hovered
                                    ToolTip.delay:600
                                    ToolTip.text:unavailable ? qsTr("This wall-clock time does not exist because the clocks change.")
                                        : empty ? (past ? qsTr("This planning slot is in the past.")
                                                        : lanePlatform==="tiktok" ? qsTr("TikTok is posted by hand; this slot is a reminder.")
                                                        : qsTr("Schedule the current reel here; you confirm in the Post dialog."))
                                        : page.statusText(modelData.status)+" · "+(modelData.title||modelData.caption||"")
                                }
                            }
                        }
                    }
                }
            }
        }
        Label {
            Layout.fillWidth:true; font.pixelSize:11; color:page.dim; wrapMode:Text.Wrap
            text:qsTr("Scheduled posts come from Reelcut. Published reels come from Stats. Past platform schedules remain unconfirmed until a matching published reel is seen.")
        }
    }
    Popup {
        id:postDetail
        parent:Overlay.overlay
        anchors.centerIn:parent
        width:Math.min(460,parent ? parent.width-40 : 460)
        padding:20; modal:true
        background:Rectangle { color:page.panel; radius:14; border.color:page.line }
        contentItem:ColumnLayout {
            spacing:12
            Image {
                visible:status===Image.Ready
                source:page.previewFor(page.selectedPost)
                Layout.alignment:Qt.AlignHCenter
                Layout.preferredWidth:160; Layout.preferredHeight:284
                sourceSize.width:320
                fillMode:Image.PreserveAspectCrop; asynchronous:true
            }
            Label { text:page.selectedPost ? page.labels[page.selectedPost.platform]+" · "+page.statusText(page.selectedPost.status) : ""; color:page.ink; font.bold:true; font.pixelSize:18; Layout.fillWidth:true; wrapMode:Text.Wrap }
            Label { text:page.selectedPost ? Qt.formatDateTime(new Date(page.selectedPost.ms),"ddd, MMM d · HH:mm t") : ""; color:page.dim }
            Label { text:page.selectedPost ? (page.selectedPost.title||page.selectedPost.caption||qsTr("Untitled reel")) : ""; color:page.ink; Layout.fillWidth:true; wrapMode:Text.Wrap }
            Label { visible:text.length>0; text:page.selectedPost ? (page.selectedPost.error||"") : ""; color:page.danger; Layout.fillWidth:true; wrapMode:Text.Wrap }
            Label { visible:page.selectedPost && page.selectedPost.status==="awaiting"; text:qsTr("The scheduled time has passed, but Reelcut has not confirmed publication. Refresh Stats or check the platform."); color:page.warning; Layout.fillWidth:true; wrapMode:Text.Wrap }
            RowLayout {
                Item { Layout.fillWidth:true }
                Button { visible:page.selectedPost && (page.selectedPost.link||"").length>0; text:qsTr("Open on platform"); onClicked:backend.openLink(page.selectedPost.link) }
                Button { text:qsTr("Close"); onClicked:postDetail.close() }
            }
        }
    }
}
