import QtQuick

// A deliberately narrow façade: no arbitrary property paths, QObject traversal,
// JavaScript evaluation, or reads of token/client-secret/scheduler-key fields.
QtObject {
    id: api
    objectName: "automationSurface"
    property var windowItem
    property var previewItem
    property var timelineItem
    property var inspectorItem
    property var statsItem
    property var scheduleItem
    property var postItem
    property var settingsItem
    property var confirmationItem

    function fail(message) { return {isError:true,error:message} }
    function keys(value, allowed) {
        return value && typeof value === "object" && !Array.isArray(value)
               && Object.keys(value).every(function(k) { return allowed.indexOf(k) >= 0 })
    }
    function finite(value) { return typeof value === "number" && isFinite(value) }
    function idlePost() { return postItem.stage !== "exporting" && postItem.stage !== "publishing" }
    function snapshot() {
        return {
            page: settingsItem.visible ? "settings" : postItem.visible ? "post" : windowItem.statsVisible ? "stats" : windowItem.scheduleVisible ? "schedule" : "editor",
            playhead:windowItem.playhead, playing:previewItem.playing,
            selection:{kind:windowItem.selectionKind,clip:windowItem.selectedClip,layer:windowItem.selectedLayer,overlay:windowItem.selectedOverlay,effect:windowItem.selectedEffect},
            options:{safeZones:windowItem.safeZones,showAppUi:previewItem.showAppUi,downloadsVisible:windowItem.downloadsVisible,helpVisible:windowItem.helpVisible,
                inspectorTab:inspectorItem.currentIndex,clipWheelsOpen:inspectorItem.clipWheelsOpen,lookWheelsOpen:inspectorItem.lookWheelsOpen,
                timelineZoom:timelineItem.zoom,timelineScroll:timelineItem.automationScroll,musicHeight:timelineItem.musicH},
            stats:{platform:statsItem.platform,periodDays:statsItem.periodDays,sortKey:statsItem.sortKey,sortDesc:statsItem.sortDesc,topMetric:statsItem.topMetric,
                rows:statsItem.sorted,selected:statsItem.selected,growth:statsItem.growth,growthDaily:statsItem.growthDaily},
            schedule:{selectedDate:scheduleItem.selectedDate,dailyPosts:scheduleItem.dailyPosts,dailyCount:scheduleItem.dailyCount,targetSlotCount:scheduleItem.targetSlotCount,
                selectedPost:scheduleItem.selectedPost,notice:scheduleItem.notice,timingOpen:scheduleItem.timingOpen,detailVisible:scheduleItem.automationDetailVisible(),timingDraft:scheduleItem.automationTimingState()},
            post:postItem.automationDraft(), settings:{visible:settingsItem.visible,account:settingsItem.account,tab:settingsItem.automationTabName()},
            previewCaptionEditorVisible:previewItem.automationPostEditorVisible(),
            confirmation:{visible:confirmationItem.visible}
        }
    }
    function dispatch(action, args) {
        try { return execute(action,JSON.parse(JSON.stringify(args))) }
        catch (error) { return fail("UI action failed: "+String(error)) }
    }
    function execute(action, args) {
        if (action === "state") return snapshot()
        if (action === "navigate") {
            if (["editor","stats","schedule","settings","downloads","help"].indexOf(args.page)<0) return fail("Unknown page.")
            if (args.platform !== "" && ["instagram","facebook","youtube","tiktok"].indexOf(args.platform)<0) return fail("Unknown platform.")
            if (!idlePost()) return fail("Wait for publishing or cancel the job explicitly first.")
            postItem.close()
            if (args.page !== "settings") settingsItem.close()
            windowItem.statsVisible=args.page === "stats"
            windowItem.scheduleVisible=args.page === "schedule"
            if (args.page === "settings") settingsItem.openFor(args.platform)
            if (args.page === "downloads") windowItem.downloadsVisible=true
            if (args.page === "help") windowItem.helpVisible=true
        } else if (action === "seek") {
            if (!finite(args.seconds)) return fail("seconds must be finite.")
            windowItem.seek(args.seconds)
        } else if (action === "playback") {
            if (["play","pause","toggle","refresh","step_forward","step_backward"].indexOf(args.action)<0) return fail("Unknown playback action.")
            if (args.action === "step_forward") windowItem.step(1/30)
            else if (args.action === "step_backward") windowItem.step(-1/30)
            else if (args.action === "play") previewItem.play()
            else if (args.action === "pause") previewItem.pause()
            else if (args.action === "toggle") previewItem.toggle()
            else previewItem.refresh()
        } else if (action === "select") {
            const counts={clip:backend.clips.length,layer:backend.layers.length,overlay:backend.overlays.length,effect:backend.effects.length}
            if (["clip","layer","overlay","effect","captions","music","none"].indexOf(args.kind)<0) return fail("Unknown selection kind.")
            if (counts[args.kind] !== undefined && (!Number.isInteger(args.index) || args.index<0 || args.index>=counts[args.kind])) return fail("Selection index is out of range.")
            if (args.kind === "none") windowItem.selectionKind="none"
            else if (args.kind === "music") inspectorItem.currentIndex=4
            else windowItem.select(args.kind,args.index)
        } else if (action === "deleteSelection") {
            windowItem.deleteSelection()
        } else if (action === "setOptions") {
            const options=args.options
            const bools=["safeZones","showAppUi","downloadsVisible","helpVisible","clipWheelsOpen","lookWheelsOpen","scheduleTimingOpen"]
            const numeric=["inspectorTab","timelineZoom","timelineScroll","musicHeight"]
            if (!keys(options,bools.concat(numeric))) return fail("Unknown editor view option.")
            for (const key of Object.keys(options)) {
                if (bools.indexOf(key)>=0 && typeof options[key] !== "boolean") return fail(key+" must be boolean.")
                if (numeric.indexOf(key)>=0 && !finite(options[key])) return fail(key+" must be finite.")
            }
            if (options.inspectorTab !== undefined && (!Number.isInteger(options.inspectorTab)||options.inspectorTab<0||options.inspectorTab>5)) return fail("inspectorTab must be 0..5.")
            if (options.timelineZoom !== undefined && (options.timelineZoom<1||options.timelineZoom>40)) return fail("timelineZoom must be 1..40.")
            if (options.timelineScroll !== undefined && options.timelineScroll<0) return fail("timelineScroll cannot be negative.")
            if (options.musicHeight !== undefined && (options.musicHeight<52||options.musicHeight>320)) return fail("musicHeight must be 52..320.")
            for (const key of Object.keys(options)) {
                if (key === "showAppUi") previewItem.showAppUi=options[key]
                else if (key === "inspectorTab") inspectorItem.currentIndex=options[key]
                else if (key === "clipWheelsOpen" || key === "lookWheelsOpen") inspectorItem[key]=options[key]
                else if (key === "timelineZoom") timelineItem.zoom=options[key]
                else if (key === "timelineScroll") timelineItem.automationScroll=Math.min(options[key],timelineItem.automationMaxScroll)
                else if (key === "musicHeight") timelineItem.musicH=options[key]
                else if (key === "scheduleTimingOpen") scheduleItem.timingOpen=options[key]
                else windowItem[key]=options[key]
            }
        } else if (action === "settingsTab") {
            if (["accounts","scheduling"].indexOf(args.tab)<0) return fail("Unknown Settings tab.")
            if (!settingsItem.visible) settingsItem.openFor("")
            settingsItem.automationTab(args.tab)
        } else if (action === "scheduleTiming") {
            return scheduleItem.automationTiming(args.fields,args.apply)
        } else if (action === "statsFilter") {
            const options=args.options
            if (!keys(options,["platform","periodDays","sortKey","sortDesc","topMetric"])) return fail("Unknown Stats filter.")
            if (options.platform !== undefined && ["all","instagram","facebook","youtube"].indexOf(options.platform)<0) return fail("Unknown Stats platform.")
            if (options.periodDays !== undefined && [0,7,28,90].indexOf(options.periodDays)<0) return fail("periodDays must be 0, 7, 28 or 90.")
            const metrics=statsItem.metricInfo.map(function(m){return m.key}).concat(["engagementRate"])
            if (options.sortKey !== undefined && ["published","caption","platform"].concat(metrics).indexOf(options.sortKey)<0) return fail("Unknown Stats sort key.")
            if (options.topMetric !== undefined && statsItem.topOptions.map(function(m){return m.key}).indexOf(options.topMetric)<0) return fail("Unknown top metric.")
            if (options.sortDesc !== undefined && typeof options.sortDesc !== "boolean") return fail("sortDesc must be boolean.")
            for (const key of Object.keys(options)) statsItem[key]=options[key]
        } else if (action === "statsSelect") {
            const reel=statsItem.allReels.filter(function(r){return r.platform===args.platform && String(r.id)===String(args.id)})[0]
            if (!reel) return fail("No matching reel is loaded. Refresh Stats first.")
            statsItem.select(reel)
        } else if (action === "statsCloseDetail") {
            statsItem.selectedKey=""
        } else if (action === "scheduleDay") {
            const match=/^(\d{4})-(\d{2})-(\d{2})$/.exec(args.date)
            if (!match) return fail("Use a local date in YYYY-MM-DD format.")
            const year=Number(match[1]),month=Number(match[2])-1,day=Number(match[3])
            const date=new Date(0);date.setFullYear(year,month,day);date.setHours(0,0,0,0)
            if (year<1||date.getFullYear()!==year||date.getMonth()!==month||date.getDate()!==day) return fail("Invalid calendar date.")
            scheduleItem.selectedDate=date.getTime()
        } else if (action === "scheduleSlots") {
            if (["instagram","facebook","youtube"].indexOf(args.platform)<0) return fail("Unknown scheduling platform.")
            return {date:scheduleItem.selectedDate,slots:scheduleItem.targets(args.platform),cards:scheduleItem.cardsFor(args.platform)}
        } else if (action === "scheduleChoose") {
            if (["instagram","facebook","youtube"].indexOf(args.platform)<0||!finite(args.publishAtMs)) return fail("Invalid platform or time.")
            scheduleItem.chooseSlot(args.platform,args.publishAtMs)
            if (scheduleItem.notice !== "") return fail(scheduleItem.notice)
        } else if (action === "scheduleDetail") {
            if (!scheduleItem.automationDetail(args.platform,args.id)) return fail("No matching calendar card on this day.")
        } else if (action === "scheduleCloseDetail") {
            scheduleItem.closeDetail()
        } else if (action === "postOpen") {
            if (["instagram","facebook","youtube","tiktok"].indexOf(args.platform)<0) return fail("Unknown platform.")
            if (!idlePost()) return fail("A publishing operation is already running.")
            postItem.openFor(args.platform)
        } else if (action === "postDraft") {
            return postItem.automationSetDraft(args.fields)
        } else if (action === "postSubmit") {
            return postItem.automationSubmit()
        } else if (action === "postBack") {
            if (["connect","scheduler"].indexOf(postItem.stage)<0) return fail("Post is not in a setup step.")
            backend.accountSettings.cancelSelection();postItem.stage="write"
        } else if (action === "closeDialogs") {
            if (!idlePost()) return fail("Cancel the active publishing job explicitly before closing Post.")
            postItem.close();settingsItem.close()
        } else if (action === "previewPostEditor") {
            previewItem.automationPostEditor(args.open)
        } else if (action === "escape") {
            if (settingsItem.visible) settingsItem.close()
            else if (postItem.visible) { if (!idlePost()) return fail("Publishing is in progress.");postItem.close() }
            else if (windowItem.scheduleVisible) { if (!scheduleItem.closeDetail()) windowItem.scheduleVisible=false }
            else if (windowItem.statsVisible) { if (statsItem.selectedKey !== "") statsItem.selectedKey=""; else windowItem.statsVisible=false }
            else {windowItem.helpVisible=false;windowItem.selectionKind="none"}
        } else if (action === "resolveConfirmation") {
            if (!confirmationItem.visible) return fail("No confirmation is open.")
            if (args.accept) confirmationItem.accept();else confirmationItem.reject()
        } else if (action === "quit") {
            if (backend.busy) return fail("Cancel or finish the active job before quitting.")
            if (backend.dirty && !args.discard_unsaved) return fail("Save the project, or explicitly allow discarding unsaved edits.")
            windowItem.closeConfirmed=true
            Qt.callLater(function(){windowItem.close()})
            return {closing:true}
        } else return fail("Unknown UI action.")
        return snapshot()
    }
}
