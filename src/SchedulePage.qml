pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import QtMultimedia
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
        return n + (pref ? Plan.slots(page.selectedDate,pref.startMinute,pref.spacingMinutes,pref.slotsPerDay).length : 0)
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
                published:qsTr("Published"),awaiting:qsTr("Awaiting confirmation"),failed:qsTr("Failed"),cancelled:qsTr("Cancelled"),
                placeholder:qsTr("Placeholder · to film")}[s] || s
    }
    function statusColor(s,p) {
        return s==="failed" ? danger : s==="awaiting" || s==="placeholder" ? warning : s==="published" ? (dark ? "#7bd8b2" : "#18794e")
               : s==="planned" || s==="cancelled" ? dim : platformColor(p)
    }
    function targets(p) {
        var pref=preferences[p];
        return pref ? Plan.slots(selectedDate,pref.startMinute,pref.spacingMinutes,pref.slotsPerDay) : []
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
            if (!value || typeof value !== "object" || Array.isArray(value) || !Object.keys(value).every(function(k){return ["start","spacingMinutes","slotsPerDay"].indexOf(k)>=0})) return {isError:true,error:"Unknown timing field."}
            if (value.start !== undefined && (typeof value.start !== "string" || Plan.readTime(value.start)<0)) return {isError:true,error:"Use HH:mm for the first slot."}
            if (value.spacingMinutes !== undefined && [60,90,120].indexOf(value.spacingMinutes)<0) return {isError:true,error:"Spacing must be 60, 90 or 120 minutes."}
            if (value.slotsPerDay !== undefined && (!Number.isInteger(value.slotsPerDay) || value.slotsPerDay<1 || value.slotsPerDay>10)) return {isError:true,error:"Slots per day must be 1 to 10."}
        }
        const before=automationTimingState()
        if (apply) {
            for (const p of Plan.platforms) {
                const v=fields[p]||{},old=before[p]
                const start=Plan.readTime(v.start !== undefined?v.start:old.start)
                const gap=v.spacingMinutes !== undefined?v.spacingMinutes:old.spacingMinutes
                const count=v.slotsPerDay !== undefined?v.slotsPerDay:old.slotsPerDay
                if (start<0 || start+(count-1)*gap>=1440) return {isError:true,error:"Every slot must fit in the day."}
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
        // A placeholder shows its reference reel until the real one exists.
        var file=post.video || post.reference || ""
        return file ? backend.videoFrame(file, 1000) : ""
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
            // Takes the spare width, and gives it back first on narrow windows.
            ColumnLayout {
                spacing:3
                Layout.fillWidth:true
                Layout.minimumWidth:0
                Label { text:qsTr("Daily schedule"); color:page.ink; font.pixelSize:24; font.bold:true; Layout.fillWidth:true; elide:Text.ElideRight }
                Label { text:qsTr("Up to 10 videos per platform, in order through the day · your local time"); color:page.dim; font.pixelSize:12; Layout.fillWidth:true; elide:Text.ElideRight }
            }
            ToolButton { objectName:"schedulePreviousDay"; text:"‹"; font.pixelSize:26; Accessible.name:qsTr("Previous day"); onClicked:page.selectedDate=Plan.addDays(page.selectedDate,-1) }
            // The date opens a month calendar to jump straight to any day.
            AbstractButton {
                id: dateButton
                objectName:"scheduleDateButton"
                Accessible.name:qsTr("Choose a date")
                hoverEnabled:true
                padding:4; leftPadding:2; rightPadding:2
                implicitWidth:dateLabels.implicitWidth+2*padding
                implicitHeight:dateLabels.implicitHeight+2*padding
                onClicked:datePicker.openAt(page.selectedDate)
                background: Rectangle {
                    radius:8
                    color:dateButton.hovered || datePicker.visible ? (page.dark ? "#ffffff14" : "#0000000d") : "transparent"
                }
                contentItem: ColumnLayout {
                    id: dateLabels
                    spacing:2
                    Label { text:Qt.formatDateTime(new Date(page.selectedDate),"dddd, MMM d"); color:page.ink; font.bold:true; font.pixelSize:16; Layout.alignment:Qt.AlignHCenter }
                    Label { text:Qt.formatDateTime(new Date(page.selectedDate),"yyyy · t"); color:page.dim; font.pixelSize:11; Layout.alignment:Qt.AlignHCenter }
                }
            }
            ToolButton { objectName:"scheduleNextDay"; text:"›"; font.pixelSize:26; Accessible.name:qsTr("Next day"); onClicked:page.selectedDate=Plan.addDays(page.selectedDate,1) }
            Button { text:qsTr("Today"); flat:true; onClicked:page.selectedDate=Plan.dayStart(Date.now()) }
            ToolButton {
                objectName:"scheduleUndo"; text:"↶"; font.pixelSize:20; enabled:backend.canUndoSchedule
                Accessible.name:qsTr("Undo")
                ToolTip.visible:hovered; ToolTip.delay:400
                ToolTip.text:backend.canUndoSchedule ? qsTr("Undo %1 (Ctrl+Z)").arg(backend.scheduleUndoText) : qsTr("Nothing to undo")
                onClicked:backend.undoSchedule()
            }
            ToolButton {
                objectName:"scheduleRedo"; text:"↷"; font.pixelSize:20; enabled:backend.canRedoSchedule
                Accessible.name:qsTr("Redo")
                ToolTip.visible:hovered; ToolTip.delay:400
                ToolTip.text:backend.canRedoSchedule ? qsTr("Redo %1 (Ctrl+Y)").arg(backend.scheduleRedoText) : qsTr("Nothing to redo")
                onClicked:backend.redoSchedule()
            }
            Button { objectName:"scheduleRemix"; text:qsTr("UGC remix"); flat:true; onClicked:remixPopup.open() }
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
                            readonly property int count:countInput.value
                            readonly property bool valid:start>=0 && start+(count-1)*gap<1440
                            function automationDraft() { return {start:startInput.text,spacingMinutes:gap,slotsPerDay:count} }
                            function automationSet(value) {
                                if (value.start !== undefined) startInput.text=value.start
                                if (value.spacingMinutes !== undefined) spacingInput.currentIndex=[60,90,120].indexOf(value.spacingMinutes)
                                if (value.slotsPerDay !== undefined) countInput.value=value.slotsPerDay
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
                                ColumnLayout {
                                    spacing:2
                                    Label { text:qsTr("Per day"); color:page.dim; font.pixelSize:11 }
                                    SpinBox {
                                        id:countInput
                                        objectName:"scheduleCount-"+timingRow.modelData
                                        from:1; to:10
                                        value:page.preferences[timingRow.modelData].slotsPerDay || 10
                                        Layout.preferredWidth:110
                                        Accessible.name:qsTr("%1 slots per day").arg(page.labels[timingRow.modelData])
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
                        text:qsTr("Use HH:mm. Every slot must fit before midnight. YouTube uploads each Short when you schedule it, so keep it to about 6 a day.")
                        color:page.dim; font.pixelSize:11
                    }
                    Button {
                        id: applyTiming
                        objectName:"scheduleApplyTiming"; text:qsTr("Apply")
                        enabled:{ for(var i=0;i<timingRows.count;++i) { var row=timingRows.itemAt(i); if(!row || !row.valid) return false } return true }
                        onClicked:{
                            var next={};
                            for(var i=0;i<timingRows.count;++i){var row=timingRows.itemAt(i);next[row.modelData]={startMinute:row.start,spacingMinutes:row.gap,slotsPerDay:row.count};}
                            backend.schedulePreferences=next;
                            page.notice=Plan.platforms.every(function(p) { var saved=backend.schedulePreferences[p]; return saved.startMinute===next[p].startMinute && saved.spacingMinutes===next[p].spacingMinutes && saved.slotsPerDay===next[p].slotsPerDay })
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
                                    Label { text:qsTr("%1 / %2").arg(platformRow.summary.covered).arg(platformRow.summary.target); color:page.ink; font.bold:true; font.pixelSize:22 }
                                    Label { text:qsTr("%1 scheduled · %2 published").arg(platformRow.summary.scheduled).arg(platformRow.summary.published); color:page.dim; font.pixelSize:11 }
                                    Label { visible:(platformRow.summary.placeholders||0)>0; text:qsTr("%1 placeholders to film").arg(platformRow.summary.placeholders||0); color:page.warning; font.pixelSize:11 }
                                    Label { visible:platformRow.summary.failed>0; text:qsTr("%1 failed").arg(platformRow.summary.failed); color:page.danger; font.pixelSize:11 }
                                    Label { visible:platformRow.summary.awaiting>0; text:qsTr("%1 unconfirmed").arg(platformRow.summary.awaiting); color:page.warning; font.pixelSize:11 }
                                    Label { visible:platformRow.modelData==="tiktok"; text:qsTr("Reminders only · posted by hand"); color:page.dim; font.pixelSize:11; Layout.fillWidth:true; wrapMode:Text.Wrap }
                                    Label { visible:platformRow.modelData==="youtube"; text:qsTr("Uploads when scheduled · YouTube blocks after about 10 uploads a day"); color:page.dim; font.pixelSize:11; Layout.fillWidth:true; wrapMode:Text.Wrap }
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
                                            // Placeholders show the original reel, flagged so it's never mistaken for the post.
                                            Rectangle {
                                                visible:postCard.modelData.status==="placeholder"
                                                anchors.top:parent.top; anchors.left:parent.left; anchors.margins:6
                                                width:refBadge.implicitWidth+10; height:refBadge.implicitHeight+4; radius:4
                                                color:page.warning
                                                Label { id:refBadge; anchors.centerIn:parent; text:qsTr("REFERENCE"); color:"#1b1b20"; font.bold:true; font.pixelSize:9 }
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
    // Month calendar behind the header date: pick any day to jump to it.
    Popup {
        id: datePicker
        objectName:"scheduleDatePicker"
        property int year:new Date().getFullYear()
        property int month:new Date().getMonth()
        readonly property var postDays:Plan.postDays(page.scheduledRecords, page.publishedReels)
        function openAt(ms) {
            var d=new Date(ms); year=d.getFullYear(); month=d.getMonth(); open()
        }
        function shiftMonth(by) {
            var d=new Date(year, month+by, 1); year=d.getFullYear(); month=d.getMonth()
        }
        // For automation and tests: jump to a local calendar date (month 0-based).
        function choose(y, m, d) {
            page.selectedDate=Plan.calendarStart(y, m, d)
            close()
        }
        parent:dateButton
        x:Math.round((dateButton.width-width)/2)
        y:dateButton.height+6
        width:300
        padding:14
        modal:false
        focus:true
        closePolicy:Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background:Rectangle { color:page.panel; radius:12; border.color:page.line }
        contentItem:ColumnLayout {
            spacing:8
            RowLayout {
                Layout.fillWidth:true
                ToolButton { text:"‹"; font.pixelSize:20; Accessible.name:qsTr("Previous month"); onClicked:datePicker.shiftMonth(-1) }
                Label {
                    Layout.fillWidth:true
                    horizontalAlignment:Text.AlignHCenter
                    text:Qt.formatDate(new Date(datePicker.year, datePicker.month, 1), "MMMM yyyy")
                    color:page.ink; font.bold:true; font.pixelSize:15
                }
                ToolButton { text:"›"; font.pixelSize:20; Accessible.name:qsTr("Next month"); onClicked:datePicker.shiftMonth(1) }
            }
            DayOfWeekRow {
                Layout.fillWidth:true
                locale:grid.locale
                delegate:Label {
                    required property string shortName
                    text:shortName; color:page.dim; font.pixelSize:11
                    horizontalAlignment:Text.AlignHCenter
                }
            }
            MonthGrid {
                id: grid
                objectName:"scheduleMonthGrid"
                Layout.fillWidth:true
                // MonthGrid doesn't size itself from its cells: six rows of 34px.
                Layout.preferredHeight:6*34+5*spacing
                year:datePicker.year
                month:datePicker.month
                onClicked:function(date) { datePicker.choose(date.getFullYear(), date.getMonth(), date.getDate()) }
                delegate:Rectangle {
                    id: dayCell
                    required property var model
                    readonly property var d:model.date
                    readonly property bool selected:{
                        var s=new Date(page.selectedDate)
                        return d.getFullYear()===s.getFullYear() && d.getMonth()===s.getMonth() && d.getDate()===s.getDate()
                    }
                    readonly property bool hasPosts:datePicker.postDays[d.getFullYear()+"-"+d.getMonth()+"-"+d.getDate()]===true
                    implicitHeight:34
                    radius:8
                    color:selected ? Material.accent : (cellMouse.containsMouse ? (page.dark ? "#ffffff14" : "#0000000d") : "transparent")
                    border.width:model.today && !selected ? 1 : 0
                    border.color:Material.accent
                    Label {
                        anchors.centerIn:parent
                        text:dayCell.model.day
                        color:dayCell.selected ? "white" : page.ink
                        opacity:dayCell.model.month===datePicker.month ? 1 : 0.35
                        font.bold:dayCell.model.today || dayCell.selected
                    }
                    Rectangle {
                        visible:dayCell.hasPosts
                        width:4; height:4; radius:2
                        anchors.horizontalCenter:parent.horizontalCenter
                        anchors.bottom:parent.bottom; anchors.bottomMargin:4
                        color:dayCell.selected ? "white" : Material.accent
                    }
                    // Hover only; MonthGrid itself turns the press into clicked(date).
                    MouseArea { id: cellMouse; anchors.fill:parent; hoverEnabled:true; acceptedButtons:Qt.NoButton; cursorShape:Qt.PointingHandCursor }
                }
            }
            Button {
                Layout.alignment:Qt.AlignHCenter
                text:qsTr("Today"); flat:true
                onClicked:{ var t=new Date(); datePicker.choose(t.getFullYear(), t.getMonth(), t.getDate()) }
            }
        }
    }

    // Copies one prompt for Claude Code that remakes a profile's reels as placeholders.
    Popup {
        id:remixPopup
        objectName:"scheduleRemixPopup"
        parent:Overlay.overlay
        anchors.centerIn:parent
        width:Math.min(560,parent ? parent.width-40 : 560)
        padding:20; modal:true
        property string mode:"profile"
        readonly property string prompt:Plan.remixPrompt(mode==="reels" ? remixReels.text : remixSource.text,remixBrand.text,remixLanguage.text,mode)
        onModeChanged:copied=false
        property bool copied:false
        onOpened:copied=false
        background:Rectangle { color:page.panel; radius:14; border.color:page.line }
        // QML has no clipboard API; a hidden text field copies for us.
        TextEdit { id:clipboardHelper; visible:false }
        contentItem:ColumnLayout {
            spacing:10
            Label { text:qsTr("UGC remix prompt"); color:page.ink; font.bold:true; font.pixelSize:18 }
            Label {
                Layout.fillWidth:true; wrapMode:Text.Wrap; color:page.dim; font.pixelSize:12
                text:qsTr("Paste it into Claude Code. It downloads the reels, studies each one, and adds a placeholder per reel to the next open Instagram slots, with the original as the reference and a step-by-step guide of the clips to film. Nothing is posted.")
            }
            RowLayout {
                spacing:6
                Label { text:qsTr("Remix"); color:page.dim; font.pixelSize:11 }
                Button { objectName:"scheduleRemixProfile"; text:qsTr("A whole profile"); flat:true; checkable:true; checked:remixPopup.mode==="profile"; onClicked:remixPopup.mode="profile" }
                Button { objectName:"scheduleRemixReels"; text:qsTr("Individual reels"); flat:true; checkable:true; checked:remixPopup.mode==="reels"; onClicked:remixPopup.mode="reels" }
            }
            TextField {
                id:remixSource; objectName:"scheduleRemixSource"
                visible:remixPopup.mode==="profile"
                Layout.fillWidth:true; placeholderText:"https://www.tiktok.com/@account"; selectByMouse:true
                onTextChanged:remixPopup.copied=false
            }
            ColumnLayout {
                visible:remixPopup.mode==="reels"
                Layout.fillWidth:true; spacing:4
                Label { text:qsTr("Reel links, one per line"); color:page.dim; font.pixelSize:11 }
                // A plain box: Material floats a placeholder into the border, which overlaps typed links.
                Rectangle {
                    Layout.fillWidth:true; Layout.preferredHeight:96
                    radius:6; color:"transparent"
                    border.width:remixReels.activeFocus ? 2 : 1
                    border.color:remixReels.activeFocus ? Material.accent : page.line
                    ScrollView {
                        anchors.fill:parent; anchors.margins:2
                        TextArea {
                            id:remixReels; objectName:"scheduleRemixReelLinks"
                            wrapMode:TextEdit.NoWrap; selectByMouse:true
                            color:page.ink; font.pixelSize:12
                            leftPadding:8; rightPadding:8; topPadding:6; bottomPadding:6
                            background:Item {}
                            onTextChanged:remixPopup.copied=false
                        }
                    }
                }
                Label { text:qsTr("e.g. https://www.tiktok.com/@account/video/… or https://www.instagram.com/reel/…"); color:page.dim; font.pixelSize:11; Layout.fillWidth:true; elide:Text.ElideRight }
            }
            RowLayout {
                Layout.fillWidth:true; spacing:10
                ColumnLayout {
                    Layout.fillWidth:true; spacing:2
                    Label { text:qsTr("For brand"); color:page.dim; font.pixelSize:11 }
                    TextField { id:remixBrand; objectName:"scheduleRemixBrand"; Layout.fillWidth:true; text:"TextInk (textink.ca)"; selectByMouse:true; onTextChanged:remixPopup.copied=false }
                }
                ColumnLayout {
                    spacing:2
                    Label { text:qsTr("Hook language"); color:page.dim; font.pixelSize:11 }
                    TextField { id:remixLanguage; objectName:"scheduleRemixLanguage"; Layout.preferredWidth:140; text:"English"; selectByMouse:true; onTextChanged:remixPopup.copied=false }
                }
            }
            ScrollView {
                Layout.fillWidth:true; Layout.preferredHeight:220
                TextArea {
                    objectName:"scheduleRemixPrompt"
                    readOnly:true; wrapMode:TextEdit.Wrap; selectByMouse:true
                    text:remixPopup.prompt; font.pixelSize:11; color:page.ink
                }
            }
            RowLayout {
                Layout.fillWidth:true
                Label { visible:remixPopup.copied; text:qsTr("Copied. Paste it into Claude Code."); color:page.dim; font.pixelSize:12 }
                Item { Layout.fillWidth:true }
                Button {
                    objectName:"scheduleRemixCopy"
                    text:qsTr("Copy prompt"); highlighted:true
                    onClicked:{ clipboardHelper.text=remixPopup.prompt; clipboardHelper.selectAll(); clipboardHelper.copy(); remixPopup.copied=true }
                }
                Button { text:qsTr("Close"); flat:true; onClicked:remixPopup.close() }
            }
        }
    }

    Popup {
        id:postDetail
        parent:Overlay.overlay
        anchors.centerIn:parent
        width:Math.min(placeholder ? 860 : 460,parent ? parent.width-40 : 460)
        height:placeholder && parent ? Math.min(implicitHeight, parent.height-40) : implicitHeight
        padding:20; modal:true
        background:Rectangle { color:page.panel; radius:14; border.color:page.line }
        // The reel's exported file, when it's still on disk: play it right here.
        readonly property bool placeholder:page.selectedPost !== null && page.selectedPost.status==="placeholder"
        // A placeholder plays its reference reel: the original to copy, not what gets posted.
        readonly property string videoFile:!page.selectedPost ? ""
            : page.selectedPost.video ? String(page.selectedPost.video)
            : placeholder && page.selectedPost.reference ? String(page.selectedPost.reference) : ""
        property string copied:""
        onOpened: { copied=""; if (videoFile.length>0) { reelPlayer.source="file://"+videoFile; reelPlayer.play() } }
        onClosed: { reelPlayer.stop(); reelPlayer.source="" }
        MediaDevices { id:reelDevices }
        MediaPlayer {
            id:reelPlayer
            objectName:"scheduleReelPlayer"
            videoOutput:reelVideo
            loops:MediaPlayer.Infinite
            audioOutput:AudioOutput { device:reelDevices.defaultAudioOutput; muted:reelMute.checked }
        }
        contentItem:GridLayout {
            columns:postDetail.placeholder ? 2 : 1
            columnSpacing:20; rowSpacing:12
          ColumnLayout {
            Layout.alignment:Qt.AlignTop
            spacing:12
            Rectangle {
                visible:postDetail.videoFile.length>0
                Layout.alignment:Qt.AlignHCenter
                Layout.preferredHeight:Math.min(480, (page.Window.height||800)-(postDetail.placeholder ? 200 : 300))
                Layout.preferredWidth:Layout.preferredHeight*9/16
                radius:10; color:"black"; clip:true
                // The still covers the gap until the first frame decodes.
                Image {
                    anchors.fill:parent
                    visible:reelPlayer.position===0
                    source:postDetail.videoFile.length>0 ? page.previewFor(page.selectedPost) : ""
                    sourceSize.width:540
                    fillMode:Image.PreserveAspectFit; asynchronous:true
                }
                VideoOutput {
                    id:reelVideo
                    objectName:"scheduleReelVideo"
                    anchors.fill:parent
                    fillMode:VideoOutput.PreserveAspectFit
                }
                MouseArea {
                    anchors.fill:parent
                    cursorShape:Qt.PointingHandCursor
                    onClicked:reelPlayer.playbackState===MediaPlayer.PlayingState ? reelPlayer.pause() : reelPlayer.play()
                }
                Label {
                    anchors.centerIn:parent
                    visible:reelPlayer.playbackState!==MediaPlayer.PlayingState
                    text:"▶"; color:"white"; font.pixelSize:44; opacity:0.85
                }
                Rectangle {
                    objectName:"scheduleReferenceFlag"
                    visible:postDetail.placeholder && !page.selectedPost.video
                    anchors.top:parent.top; anchors.left:parent.left; anchors.right:parent.right
                    height:flagText.implicitHeight+12
                    color:page.warning
                    Label {
                        id:flagText
                        anchors.centerIn:parent; width:parent.width-12
                        horizontalAlignment:Text.AlignHCenter; wrapMode:Text.Wrap
                        text:qsTr("REFERENCE · original reel to copy. Not what gets posted.")
                        color:"#1b1b20"; font.bold:true; font.pixelSize:11
                    }
                }
            }
            RowLayout {
                visible:postDetail.videoFile.length>0
                Layout.fillWidth:true; spacing:6
                ToolButton {
                    objectName:"scheduleReelPlay"
                    text:reelPlayer.playbackState===MediaPlayer.PlayingState ? "❚❚" : "▶"
                    Accessible.name:reelPlayer.playbackState===MediaPlayer.PlayingState ? qsTr("Pause") : qsTr("Play")
                    onClicked:reelPlayer.playbackState===MediaPlayer.PlayingState ? reelPlayer.pause() : reelPlayer.play()
                }
                Slider {
                    id:reelSeek
                    objectName:"scheduleReelSeek"
                    Layout.fillWidth:true
                    from:0; to:Math.max(1, reelPlayer.duration)
                    value:pressed ? value : reelPlayer.position
                    enabled:reelPlayer.seekable
                    onMoved:reelPlayer.position=value
                    Accessible.name:qsTr("Position")
                }
                Label {
                    function clock(ms) { var s=Math.floor(ms/1000); return Math.floor(s/60)+":"+String(s%60).padStart(2,"0") }
                    text:clock(reelPlayer.position)+" / "+clock(reelPlayer.duration)
                    color:page.dim; font.pixelSize:11
                }
                ToolButton {
                    id:reelMute
                    objectName:"scheduleReelMute"
                    checkable:true
                    text:checked ? "🔇" : "🔊"
                    Accessible.name:checked ? qsTr("Unmute") : qsTr("Mute")
                }
            }
            Image {
                visible:postDetail.videoFile.length===0 && status===Image.Ready
                source:postDetail.videoFile.length===0 ? page.previewFor(page.selectedPost) : ""
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
                Button { visible:!postDetail.placeholder; text:qsTr("Close"); onClicked:postDetail.close() }
            }
          }
          // The plan for a placeholder: what gets posted, and the clips to film for it.
          ColumnLayout {
            visible:postDetail.placeholder
            Layout.alignment:Qt.AlignTop
            Layout.fillWidth:true; Layout.fillHeight:true
            Layout.preferredWidth:380
            spacing:10
            Label { text:qsTr("This slot will post"); color:page.dim; font.pixelSize:11; font.bold:true }
            Label {
                objectName:"schedulePlaceholderNote"
                Layout.fillWidth:true; wrapMode:Text.Wrap; color:page.ink
                text:postDetail.placeholder ? (page.selectedPost.note||qsTr("The finished reel, once it's made.")) : ""
            }
            Label {
                visible:postDetail.placeholder && (page.selectedPost.caption||"").length>0
                Layout.fillWidth:true; wrapMode:Text.Wrap; color:page.dim; font.pixelSize:12
                text:postDetail.placeholder ? qsTr("Caption: %1").arg(page.selectedPost.caption||"") : ""
            }
            Label {
                Layout.fillWidth:true; wrapMode:Text.Wrap; color:page.warning; font.pixelSize:12
                text:qsTr("Nothing posts from a placeholder. Make the reel, then schedule it into this slot after removing the placeholder.")
            }
            Rectangle { Layout.fillWidth:true; height:1; color:page.line }
            Label { text:qsTr("Step by step: clips to film"); color:page.dim; font.pixelSize:11; font.bold:true }
            ScrollView {
                id:guideScroll
                Layout.fillWidth:true; Layout.fillHeight:true
                contentWidth:availableWidth
                Layout.preferredHeight:Math.min(guideColumn.implicitHeight, 420)
                clip:true
                ColumnLayout {
                    id:guideColumn
                    width:guideScroll.availableWidth
                    spacing:8
                    Repeater {
                        objectName:"schedulePlaceholderGuide"
                        model:postDetail.placeholder ? (page.selectedPost.guide||[]) : []
                        RowLayout {
                            id:guideStep
                            required property int index
                            required property string modelData
                            Layout.fillWidth:true; spacing:8
                            Label { text:(guideStep.index+1)+"."; color:page.warning; font.bold:true; Layout.alignment:Qt.AlignTop }
                            Label { text:guideStep.modelData; color:page.ink; wrapMode:Text.Wrap; Layout.fillWidth:true; font.pixelSize:13 }
                        }
                    }
                }
            }
            // Where the filmed clips go: one click opens it (made if missing).
            RowLayout {
                visible:postDetail.placeholder && (page.selectedPost.folder||"").length>0
                Layout.fillWidth:true; spacing:10
                Button {
                    objectName:"scheduleOpenClipFolder"
                    text:qsTr("📂 Open clips folder")
                    highlighted:true
                    onClicked:backend.openPlaceholderFolder(page.selectedPost.id)
                }
                Label {
                    Layout.fillWidth:true
                    text:postDetail.placeholder ? (page.selectedPost.folder||"") : ""
                    color:page.dim; font.pixelSize:11; font.underline:folderHover.hovered
                    elide:Text.ElideMiddle
                    ToolTip.visible:folderHover.hovered; ToolTip.text:text
                    HoverHandler { id:folderHover; cursorShape:Qt.PointingHandCursor }
                    TapHandler { onTapped:backend.openPlaceholderFolder(page.selectedPost.id) }
                }
            }
            // Hand-off to Claude: the edit prompt built from this slot's plan, and the original's link.
            RowLayout {
                visible:postDetail.placeholder
                Layout.fillWidth:true; spacing:8
                TextEdit { id:detailClipboard; visible:false }
                Button {
                    objectName:"scheduleCopyEditPrompt"
                    text:qsTr("Copy Claude edit prompt"); highlighted:true
                    onClicked:{ detailClipboard.text=Plan.editPrompt(page.selectedPost); detailClipboard.selectAll(); detailClipboard.copy(); postDetail.copied=qsTr("Prompt copied. Paste it into Claude Code.") }
                }
                Button {
                    objectName:"scheduleCopyOriginalLink"
                    visible:postDetail.placeholder && (page.selectedPost.referenceUrl||"").length>0
                    text:qsTr("Copy original link")
                    onClicked:{ detailClipboard.text=page.selectedPost.referenceUrl; detailClipboard.selectAll(); detailClipboard.copy(); postDetail.copied=qsTr("Link copied.") }
                }
                Label { Layout.fillWidth:true; text:postDetail.copied; color:page.dim; font.pixelSize:11; elide:Text.ElideRight }
            }
            RowLayout {
                Layout.fillWidth:true
                Button {
                    visible:postDetail.placeholder && (page.selectedPost.referenceUrl||"").length>0
                    text:qsTr("Open original"); flat:true
                    onClicked:backend.openLink(page.selectedPost.referenceUrl)
                }
                Item { Layout.fillWidth:true }
                Button {
                    objectName:"scheduleRemovePlaceholder"
                    text:qsTr("Remove placeholder"); flat:true
                    onClicked:{ if (backend.removePlaceholder(page.selectedPost.id)) postDetail.close() }
                }
                Button { text:qsTr("Close"); onClicked:postDetail.close() }
            }
          }
        }
    }
}
