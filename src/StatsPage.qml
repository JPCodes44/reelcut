pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import "StatsFormat.js" as Fmt

// How the reels on every connected account are doing: totals, account views
// per day, the top reels by any metric, and every reel in a sortable table
// with a detail panel. Built from the `stats` context property only
// (src/stats.h), and safe when it's missing or half-filled.
Rectangle {
    id: page
    objectName: "statsPage"

    signal closeRequested()
    signal manageAccount(string platform)

    // ---- Colors: dark like the editor, or light ----
    readonly property bool dark: Material.theme === Material.Dark
    readonly property color bg: dark ? "#0e0e10" : "#f3f3f6"
    readonly property color panel: dark ? "#17171a" : "#ffffff"
    readonly property color cardColor: dark ? "#17171a" : "#ffffff"
    readonly property color line: dark ? "#2a2a2f" : "#dadae0"
    readonly property color textColor: dark ? "#ececf0" : "#1b1b20"
    readonly property color dim: dark ? "#9a9aa2" : "#5a5a64"
    readonly property color grid: dark ? "#25252b" : "#e8e8ed"
    readonly property color axis: dark ? "#4a4a52" : "#b4b4bc"
    readonly property color tipColor: dark ? "#232328" : "#ffffff"
    readonly property color tipBorder: dark ? "#3a3a40" : "#c8c8d0"
    readonly property color hoverColor: dark ? "#1f1f24" : "#eeeef3"
    readonly property color trackColor: dark ? "#202026" : "#ececf1"
    readonly property color warn: dark ? "#ffb74d" : "#9a5200"
    readonly property color danger: dark ? "#ff8a80" : "#c62828"
    readonly property color accent: typeof theme !== "undefined" && theme ? theme.accent : "#FFD60A"
    readonly property color accentSoft: Qt.rgba(accent.r, accent.g, accent.b, dark ? 0.16 : 0.22)
    color: bg

    // ---- What `stats` gives, guarded ----
    readonly property bool available: typeof stats !== "undefined" && stats !== null
    readonly property var allReels: available && stats.reels ? stats.reels : []
    readonly property var accounts: available && stats.accounts ? stats.accounts : []
    readonly property var metricInfo: available && stats.metricInfo && stats.metricInfo.length > 0
                                      ? stats.metricInfo : Fmt.defaultMetricInfo
    readonly property bool busy: available && stats.busy === true
    readonly property string status: available && stats.status ? String(stats.status) : ""
    readonly property real refreshedMs: {
        if (!available || !stats.refreshed) return NaN
        var t = stats.refreshed instanceof Date ? stats.refreshed.getTime() : Number(stats.refreshed)
        return isFinite(t) && t > 0 ? t : NaN
    }

    // ---- Filters ----
    property string platform: "all"
    property int periodDays: 28      // 0 for all time
    property string sortKey: "published"
    property bool sortDesc: true
    property string topMetric: "views"
    property real now: Date.now()
    Timer { interval: 30000; running: page.visible; repeat: true; onTriggered: page.now = Date.now() }

    readonly property var shownPlatforms: platform === "all" ? Fmt.platforms : [platform]

    // ---- Derived lists, rebuilt only when something they show changed ----
    property var filtered: []
    property var sorted: []
    property var columns: []
    property var reelIndex: ({})
    property string signature: ""

    onAllReelsChanged: Qt.callLater(rebuild)
    onPlatformChanged: Qt.callLater(rebuild)
    onPeriodDaysChanged: Qt.callLater(rebuild)
    onSortKeyChanged: Qt.callLater(rebuild)
    onSortDescChanged: Qt.callLater(rebuild)
    onMetricInfoChanged: Qt.callLater(rebuild)
    onNowChanged: Qt.callLater(rebuild)
    Component.onCompleted: {
        rebuild()
        if (visible) Qt.callLater(maybeRefresh)
    }

    function key(r) { return r ? String(r.platform) + ":" + String(r.id) : "" }
    function valueOf(r, k) {
        if (!r) return NaN
        if (k === "engagementRate") return Fmt.toNum(r.engagementRate)
        return r.metrics ? Fmt.toNum(r.metrics[k]) : NaN
    }
    function captionOf(r) {
        if (!r) return ""
        var first = r.platform === "youtube" ? (r.title || r.caption) : (r.caption || r.title)
        var s = String(first || "").replace(/\s+/g, " ").trim()
        return s !== "" ? s : qsTr("(no caption)")
    }

    function rebuild() {
        var list = allReels && typeof allReels.length === "number" ? allReels : []
        var cutoff = periodDays > 0 ? now - periodDays * Fmt.DAY : -Infinity
        var index = {}
        var out = []
        for (var i = 0; i < list.length; ++i) {
            var r = list[i]
            if (!r || !r.platform) continue
            index[key(r)] = r
            if (platform !== "all" && r.platform !== platform) continue
            var pub = Fmt.toNum(r.published)
            // Unknown dates stay in, rather than vanishing from every period.
            if (!isNaN(pub) && pub > 0 && pub < cutoff) continue
            out.push(r)
        }
        reelIndex = index

        // A refresh re-sends the list on every progress step; the table
        // (and its scroll position) only changes when its rows do.
        var sig = sortKey + (sortDesc ? "-" : "+") + "|" + out.map(function(r) {
            return key(r) + JSON.stringify(r.metrics || {}) + r.engagementRate + r.thumbnail + r.caption + r.title + r.published
        }).join("|")
        if (sig === signature)
            return
        signature = sig

        var cols = metricInfo.filter(function(m) {
            return m && m.key && out.some(function(r) { return !isNaN(valueOf(r, m.key)) })
        }).map(function(m) { return { key: m.key, label: m.label || m.key, unit: m.unit || "count" } })
        if (out.some(function(r) { return !isNaN(Fmt.toNum(r.engagementRate)) }))
            cols.push({ key: "engagementRate", label: qsTr("Engagement"), unit: "ratio" })

        var k = sortKey, desc = sortDesc
        var keyed = out.map(function(r, n) {
            var v
            if (k === "caption") v = captionOf(r).toLowerCase()
            else if (k === "platform") v = Fmt.name(r.platform)
            else if (k === "published") v = Fmt.toNum(r.published)
            else v = valueOf(r, k)
            return { r: r, v: v, n: n }
        })
        keyed.sort(function(a, b) {
            var am = typeof a.v === "number" && isNaN(a.v), bm = typeof b.v === "number" && isNaN(b.v)
            // Missing values go last whichever way it's sorted.
            if (am !== bm) return am ? 1 : -1
            if (!am && a.v !== b.v) return (a.v < b.v ? -1 : 1) * (desc ? -1 : 1)
            return a.n - b.n
        })

        var y = list_.contentY
        filtered = out
        columns = cols
        sorted = keyed.map(function(e) { return e.r })
        if (topMetric !== "views" && !cols.some(function(c) { return c.key === topMetric }))
            topMetric = "views"
        // Keep the reader where they were.
        Qt.callLater(function() { list_.contentY = Math.min(y, Math.max(list_.originY, list_.contentHeight + list_.originY - list_.height)) })
    }

    function sortBy(k) {
        if (sortKey === k) {
            sortDesc = !sortDesc
        } else {
            sortKey = k
            sortDesc = k !== "caption" && k !== "platform"
        }
    }

    function accountFor(p) {
        for (var i = 0; i < accounts.length; ++i)
            if (accounts[i] && accounts[i].platform === p) return accounts[i]
        return null
    }
    function clip(points) {
        var pts = Fmt.cleanPoints(points)
        if (periodDays <= 0) return pts
        var cutoff = Fmt.startOfDay(now - periodDays * Fmt.DAY)
        return pts.filter(function(p) { return p.t >= cutoff })
    }

    // ---- Summary ----
    function total(k) {
        var s = 0, n = 0
        for (var i = 0; i < filtered.length; ++i) {
            var v = valueOf(filtered[i], k)
            if (!isNaN(v)) { s += v; ++n }
        }
        return { value: n > 0 ? s : NaN, n: n }
    }
    function mean(k) {
        var t = total(k)
        return { value: t.n > 0 ? t.value / t.n : NaN, n: t.n }
    }
    // Account views per day, added up over the platforms shown.
    readonly property var combinedViews: {
        var byDay = {}
        for (var i = 0; i < shownPlatforms.length; ++i) {
            var a = accountFor(shownPlatforms[i])
            if (!a || !a.connected) continue
            var pts = clip(a.viewSeries)
            for (var j = 0; j < pts.length; ++j) {
                var d = Fmt.startOfDay(pts[j].t)
                byDay[d] = (byDay[d] || 0) + pts[j].v
            }
        }
        return Object.keys(byDay).map(function(d) { return { t: Number(d), v: byDay[d] } })
                                 .sort(function(a, b) { return a.t - b.t })
    }
    readonly property var cards: {
        var reels = filtered.length
        function counted(label, k, extra) {
            var t = total(k)
            return Object.assign({ label: label, value: t.value, unit: "count", platform: "",
                                   note: t.n === 0 ? qsTr("Not available") : t.n < reels ? qsTr("from %1 of %2 reels").arg(t.n).arg(reels)
                                                                           : reels === 1 ? qsTr("from 1 reel") : qsTr("from %1 reels").arg(reels),
                                   series: [] }, extra || {})
        }
        function averaged(label, k, unit, help) {
            var m = mean(k)
            return { label: label, value: m.value, unit: unit, platform: "", series: [], help: help,
                     note: m.n === 0 ? qsTr("Not available") : m.n === 1 ? qsTr("from 1 reel") : qsTr("average of %1 reels").arg(m.n) }
        }
        var out = [
            counted(qsTr("Views"), "views", { series: combinedViews, help: qsTr("Line: account views per day") }),
            counted(qsTr("Likes"), "likes"),
            counted(qsTr("Comments"), "comments"),
            counted(qsTr("Shares"), "shares"),
            counted(qsTr("Saves"), "saves"),
            averaged(qsTr("Avg watch time"), "avgWatch", "seconds"),
            averaged(qsTr("Avg engagement"), "engagementRate", "ratio", qsTr("(likes + comments + shares + saves) ÷ views"))
        ]
        for (var i = 0; i < shownPlatforms.length; ++i) {
            var a = accountFor(shownPlatforms[i])
            if (!a || !a.connected) continue
            var series = clip(a.followerSeries)
            out.push({ label: qsTr("%1 followers").arg(Fmt.name(a.platform)), value: Fmt.toNum(a.followers), unit: "count",
                       platform: a.platform, series: series,
                       note: a.account ? String(a.account) : "",
                       help: series.length > 0 ? qsTr("Line: followers per day") : "" })
        }
        return out
    }

    // ---- Charts ----
    readonly property var viewLines: shownPlatforms.map(function(p) {
        var a = accountFor(p)
        if (!a || !a.connected) return null
        return { name: Fmt.name(p), color: Fmt.color(p), marker: Fmt.marker(p), icon: Fmt.icon(p), points: clip(a.viewSeries) }
    }).filter(function(s) { return s !== null })

    readonly property var topOptions: {
        var out = columns.slice()
        if (!out.some(function(c) { return c.key === "views" }))
            out.unshift({ key: "views", label: qsTr("Views"), unit: "count" })
        return out
    }
    readonly property var topColumn: topOptions.filter(function(c) { return c.key === topMetric })[0] || topOptions[0]
    readonly property var topReels: {
        var k = topMetric
        return filtered.filter(function(r) { return !isNaN(valueOf(r, k)) })
                       .sort(function(a, b) { return valueOf(b, k) - valueOf(a, k) })
                       .slice(0, 10)
    }

    // ---- Opening, refreshing, closing ----
    function maybeRefresh() {
        if (!available || busy || typeof stats.refresh !== "function")
            return
        if (isNaN(refreshedMs) || Date.now() - refreshedMs > 15 * 60000)
            stats.refresh()
    }
    onVisibleChanged: if (visible) { now = Date.now(); maybeRefresh(); list_.forceActiveFocus() }

    // Esc closes the detail panel first; returns whether it did.
    function closeDetail() {
        if (selectedKey === "") return false
        selectedKey = ""
        return true
    }

    function ago(ms) {
        if (isNaN(ms)) return qsTr("Not refreshed yet")
        var minutes = Math.floor(Math.max(0, now - ms) / 60000)
        if (minutes < 1) return qsTr("Updated just now")
        if (minutes < 60) return qsTr("Updated %n min ago", "", minutes)
        if (minutes < 48 * 60) return qsTr("Updated %n h ago", "", Math.floor(minutes / 60))
        return qsTr("Updated %1").arg(Fmt.date(ms))
    }

    // ---- Detail ----
    property string selectedKey: ""
    readonly property var selected: selectedKey !== "" ? (reelIndex[selectedKey] || null) : null
    property var growth: []
    property bool growthDaily: false
    function select(r) {
        if (!r || !r.platform || r.id === undefined || r.id === null || String(r.id) === "") return
        selectedKey = key(r)
        growthDaily = false
        growth = historyViews(r)
        if (available && typeof stats.loadDaily === "function")
            stats.loadDaily(String(r.platform), String(r.id))
    }
    function historyViews(r) {
        if (!available || typeof stats.history !== "function") return []
        var h = stats.history(String(r.platform), String(r.id)) || []
        var out = []
        for (var i = 0; i < h.length; ++i) {
            var v = h[i] && h[i].metrics ? Fmt.toNum(h[i].metrics.views) : NaN
            var t = h[i] ? Fmt.toNum(h[i].t) : NaN
            if (!isNaN(v) && !isNaN(t)) out.push({ t: t, v: v })
        }
        return out
    }
    Connections {
        target: page.available ? stats : null
        ignoreUnknownSignals: true
        function onDailyLoaded(platform, id, points) {
            if (page.selectedKey !== String(platform) + ":" + String(id)) return
            page.growthDaily = platform === "youtube"
            page.growth = Fmt.cleanPoints(points)
        }
    }

    // ---- Layout ----
    readonly property int rowH: 64
    readonly property int thumbW: 34
    readonly property int platformW: 120
    readonly property int dateW: 108
    readonly property int metricW: 96
    readonly property int captionMin: 220
    readonly property real tableW: list_.width
    readonly property real metricTotal: columns.length * metricW
    readonly property real captionX: 12 + thumbW + 12
    readonly property real captionW: Math.max(captionMin, tableW - captionX - 12 - platformW - dateW - metricTotal - 12)
    readonly property real platformX: captionX + captionW + 12
    readonly property real dateX: platformX + platformW
    readonly property real metricX: dateX + dateW
    readonly property real metricViewW: Math.max(0, tableW - metricX - 12)
    readonly property real maxMetricScroll: Math.max(0, metricTotal - metricViewW)
    property real metricScroll: 0
    // Where the column headings sit in the overview, for the floating copy.
    property real tableTop: 0
    onMaxMetricScrollChanged: metricScroll = Math.min(metricScroll, maxMetricScroll)

    // The editor underneath gets no clicks, wheels or hovers through the page.
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
        hoverEnabled: true
        onWheel: function(wheel) { wheel.accepted = true }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ---- 1. Header ----
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: headerRow.implicitHeight + 20
            // Above the scrolled content, whatever its clipping lets through.
            z: 1
            color: page.panel
            RowLayout {
                id: headerRow
                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 12
                spacing: 16
                ColumnLayout {
                    spacing: 0
                    Layout.preferredWidth: 190
                    Layout.minimumWidth: 140
                    Label { text: qsTr("Reel stats"); font.pixelSize: 20; font.bold: true; color: page.textColor }
                    Label {
                        objectName: "statsStatus"
                        Layout.fillWidth: true
                        text: page.busy && page.status !== "" ? page.status : page.ago(page.refreshedMs)
                        color: page.dim
                        font.pixelSize: 12
                        elide: Text.ElideRight
                        ToolTip.visible: statusHover.hovered && truncated
                        ToolTip.text: text
                        HoverHandler { id: statusHover }
                    }
                }
                Flow {
                    Layout.fillWidth: true
                    spacing: 6
                    Repeater {
                        model: ["all"].concat(Fmt.platforms)
                        delegate: Chip {
                            required property string modelData
                            objectName: "statsPlatform-" + modelData
                            platformId: modelData === "all" ? "" : modelData
                            text: modelData === "all" ? qsTr("All") : Fmt.name(modelData)
                            checked: page.platform === modelData
                            onClicked: page.platform = modelData
                        }
                    }
                    Item { width: 14; height: 34 }
                    Repeater {
                        model: [{ d: 7, l: qsTr("7 days") }, { d: 28, l: qsTr("28 days") }, { d: 90, l: qsTr("90 days") }, { d: 0, l: qsTr("All time") }]
                        delegate: Chip {
                            required property var modelData
                            objectName: "statsPeriod-" + modelData.d
                            text: modelData.l
                            checked: page.periodDays === modelData.d
                            onClicked: page.periodDays = modelData.d
                            ToolTip.visible: hovered
                            ToolTip.delay: 600
                            ToolTip.text: modelData.d > 0 ? qsTr("Reels published in the last %n days", "", modelData.d) : qsTr("Every reel")
                        }
                    }
                }
                BusyIndicator {
                    visible: page.busy
                    running: page.busy
                    Layout.preferredWidth: 32
                    Layout.preferredHeight: 32
                }
                Button {
                    objectName: "statsRefresh"
                    text: page.busy ? qsTr("Refreshing…") : qsTr("Refresh")
                    enabled: page.available && !page.busy
                    flat: true
                    onClicked: if (page.available && typeof stats.refresh === "function") stats.refresh()
                }
                ToolButton {
                    objectName: "statsClose"
                    text: "✕"
                    ToolTip.visible: hovered
                    ToolTip.delay: 400
                    ToolTip.text: qsTr("Close stats (Esc)")
                    onClicked: page.closeRequested()
                }
            }
        }
        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: page.line }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true

                ListView {
                    id: list_
                    objectName: "statsReels"
                    anchors.fill: parent
                    // A strip below for the sideways scrollbar, when the metrics don't fit.
                    anchors.bottomMargin: page.maxMetricScroll > 0 ? 12 : 0
                    clip: true
                    visible: page.available
                    model: page.sorted
                    boundsBehavior: Flickable.StopAtBounds
                    reuseItems: true
                    cacheBuffer: page.rowH * 6
                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                    header: overviewComponent
                    footer: Item {
                        width: list_.width
                        height: page.sorted.length === 0 ? 120 : 24
                        Label {
                            anchors.centerIn: parent
                            width: parent.width - 48
                            visible: page.sorted.length === 0
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.Wrap
                            color: page.dim
                            font.pixelSize: 14
                            text: page.busy && page.allReels.length === 0 ? qsTr("Loading reels…")
                                  : page.accounts.length > 0 && !page.accounts.some(function(a) { return a && a.connected })
                                    ? qsTr("Connect Instagram, Facebook or YouTube in Settings → Connected accounts to see your reels here.")
                                  : page.allReels.length === 0 ? qsTr("No reels yet. Press Refresh to load them.")
                                  : qsTr("No reels in this period. Try a longer one.")
                        }
                    }
                    delegate: reelRow

                    // Touchpads and Shift+wheel move the metric columns sideways.
                    WheelHandler {
                        target: null
                        orientation: Qt.Horizontal
                        onWheel: function(event) {
                            page.metricScroll = Math.max(0, Math.min(page.maxMetricScroll, page.metricScroll - event.angleDelta.x / 2))
                        }
                    }
                }

                // The column headings stay on top once the table scrolls under them.
                TableHeader {
                    width: list_.width
                    visible: page.available && list_.headerItem !== null
                             && list_.contentY > list_.headerItem.y + page.tableTop
                }

                ScrollBar {
                    orientation: Qt.Horizontal
                    anchors.bottom: parent.bottom
                    height: 12
                    x: page.metricX
                    width: page.metricViewW
                    policy: ScrollBar.AlwaysOn
                    visible: page.available && page.maxMetricScroll > 0
                    size: page.metricTotal > 0 ? page.metricViewW / page.metricTotal : 1
                    position: page.metricTotal > 0 ? page.metricScroll / page.metricTotal : 0
                    onPositionChanged: if (active) page.metricScroll = Math.max(0, Math.min(page.maxMetricScroll, position * page.metricTotal))
                }

                Label {
                    anchors.centerIn: parent
                    visible: !page.available
                    text: qsTr("Stats aren't available in this build.")
                    color: page.dim
                    font.pixelSize: 15
                }
            }

            Rectangle { visible: page.selected !== null; Layout.fillHeight: true; Layout.preferredWidth: 1; color: page.line }

            // ---- 7. Reel detail ----
            Rectangle {
                id: detail
                objectName: "statsDetail"
                visible: page.selected !== null
                Layout.fillHeight: true
                Layout.preferredWidth: Math.min(400, Math.max(320, page.width * 0.3))
                color: page.panel
                readonly property var reel: page.selected

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 0
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.margins: 12
                        Layout.leftMargin: 16
                        Label { text: qsTr("Reel"); font.bold: true; font.pixelSize: 15; color: page.textColor; Layout.fillWidth: true }
                        ToolButton {
                            objectName: "statsDetailClose"
                            text: "✕"
                            ToolTip.visible: hovered
                            ToolTip.delay: 400
                            ToolTip.text: qsTr("Close (Esc)")
                            onClicked: page.selectedKey = ""
                        }
                    }
                    ScrollView {
                        id: detailScroll
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        contentWidth: availableWidth
                        clip: true
                        ColumnLayout {
                            width: detailScroll.availableWidth
                            spacing: 14
                            RowLayout {
                                Layout.fillWidth: true
                                Layout.leftMargin: 16
                                Layout.rightMargin: 16
                                spacing: 14
                                Thumb {
                                    Layout.preferredWidth: 126
                                    Layout.preferredHeight: 224
                                    source: detail.reel && detail.reel.thumbnail ? String(detail.reel.thumbnail) : ""
                                    sourceHeight: 448
                                }
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    Layout.alignment: Qt.AlignTop
                                    spacing: 8
                                    PlatformTag { platformId: detail.reel ? detail.reel.platform : "" }
                                    Label {
                                        Layout.fillWidth: true
                                        text: detail.reel ? Fmt.longDate(detail.reel.published) : ""
                                        color: page.textColor
                                        font.pixelSize: 13
                                        wrapMode: Text.Wrap
                                    }
                                    Label {
                                        Layout.fillWidth: true
                                        visible: detail.reel !== null && Fmt.toNum(detail.reel.duration) > 0
                                        text: qsTr("Length %1").arg(detail.reel ? Fmt.clock(detail.reel.duration) : "")
                                        color: page.dim
                                        font.pixelSize: 12
                                    }
                                    Button {
                                        objectName: "statsOpenReel"
                                        visible: detail.reel !== null && !!detail.reel.permalink && String(detail.reel.permalink).length > 0
                                        text: qsTr("Open on %1 ↗").arg(detail.reel ? Fmt.name(detail.reel.platform) : "")
                                        flat: true
                                        Layout.leftMargin: -8
                                        onClicked: if (detail.reel && detail.reel.permalink) Qt.openUrlExternally(String(detail.reel.permalink))
                                    }
                                }
                            }
                            Label {
                                Layout.fillWidth: true
                                Layout.leftMargin: 16
                                Layout.rightMargin: 16
                                visible: text !== ""
                                text: detail.reel && detail.reel.title ? String(detail.reel.title) : ""
                                color: page.textColor
                                font.pixelSize: 15
                                font.bold: true
                                wrapMode: Text.Wrap
                            }
                            TextEdit {
                                Layout.fillWidth: true
                                Layout.leftMargin: 16
                                Layout.rightMargin: 16
                                readOnly: true
                                selectByMouse: true
                                textFormat: TextEdit.PlainText
                                text: detail.reel ? String(detail.reel.caption || (detail.reel.title ? "" : qsTr("(no caption)"))) : ""
                                visible: text !== ""
                                color: page.textColor
                                selectionColor: page.accent
                                font.pixelSize: 13
                                wrapMode: TextEdit.Wrap
                            }

                            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: page.line }
                            Label {
                                Layout.leftMargin: 16
                                text: qsTr("Metrics")
                                font.bold: true
                                font.pixelSize: 14
                                color: page.textColor
                            }
                            GridLayout {
                                Layout.fillWidth: true
                                Layout.leftMargin: 16
                                Layout.rightMargin: 16
                                columns: 2
                                columnSpacing: 12
                                rowSpacing: 6
                                Repeater {
                                    model: {
                                        var r = detail.reel
                                        if (!r) return []
                                        var out = page.metricInfo.filter(function(m) { return m && !isNaN(page.valueOf(r, m.key)) })
                                                                 .map(function(m) { return { label: m.label || m.key, value: page.valueOf(r, m.key), unit: m.unit || "count" } })
                                        if (!isNaN(Fmt.toNum(r.engagementRate)))
                                            out.push({ label: qsTr("Engagement rate"), value: Fmt.toNum(r.engagementRate), unit: "ratio" })
                                        return out
                                    }
                                    delegate: RowLayout {
                                        id: metricRow
                                        required property var modelData
                                        Layout.columnSpan: 2
                                        Layout.fillWidth: true
                                        Label { text: metricRow.modelData.label; color: page.dim; font.pixelSize: 13; Layout.fillWidth: true; elide: Text.ElideRight }
                                        Label {
                                            text: Fmt.formatExact(metricRow.modelData.value, metricRow.modelData.unit)
                                            color: page.textColor
                                            font.pixelSize: 13
                                            font.bold: true
                                            horizontalAlignment: Text.AlignRight
                                        }
                                    }
                                }
                            }
                            Label {
                                Layout.fillWidth: true
                                Layout.leftMargin: 16
                                Layout.rightMargin: 16
                                visible: detail.reel !== null && page.metricInfo.every(function(m) { return isNaN(page.valueOf(detail.reel, m.key)) })
                                text: qsTr("No metrics for this reel yet.")
                                color: page.dim
                                font.pixelSize: 13
                                wrapMode: Text.Wrap
                            }

                            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: page.line }
                            Label {
                                Layout.leftMargin: 16
                                text: page.growthDaily ? qsTr("Views per day") : qsTr("Views over time")
                                font.bold: true
                                font.pixelSize: 14
                                color: page.textColor
                            }
                            LineChart {
                                objectName: "statsGrowth"
                                Layout.fillWidth: true
                                Layout.leftMargin: 12
                                Layout.rightMargin: 12
                                Layout.preferredHeight: 220
                                legendVisible: false
                                showTime: !page.growthDaily
                                valueTitle: page.growthDaily ? qsTr("Views that day") : qsTr("Total views")
                                emptyText: qsTr("No views recorded yet")
                                series: detail.reel ? [{ name: qsTr("Views"), color: Fmt.color(detail.reel.platform),
                                                         marker: Fmt.marker(detail.reel.platform), points: page.growth }] : []
                                textColor: page.textColor
                                dimColor: page.dim
                                gridColor: page.grid
                                axisColor: page.axis
                                tooltipColor: page.tipColor
                                tooltipBorderColor: page.tipBorder
                                backgroundColor: page.panel
                            }
                            Label {
                                Layout.fillWidth: true
                                Layout.leftMargin: 16
                                Layout.rightMargin: 16
                                Layout.bottomMargin: 16
                                visible: page.growth.length < 2
                                text: qsTr("Reelcut records growth each time stats refresh.")
                                color: page.dim
                                font.pixelSize: 12
                                wrapMode: Text.Wrap
                            }
                        }
                    }
                }
            }
        }
    }

    // ---- 2–6: everything above the table rows, scrolled with them ----
    Component {
        id: overviewComponent
        Item {
            id: overview
            width: list_.width
            height: tableHeader.y + (tableHeader.visible ? tableHeader.height : 0)
            // Where the column headings sit, for the floating copy.
            Binding { target: page; property: "tableTop"; value: tableHeader.y }

            ColumnLayout {
                id: overviewColumn
                x: 16
                y: 16
                width: parent.width - 32
                spacing: 16

                // ---- 2. Account notices ----
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    visible: noticeRepeater.count > 0
                    Repeater {
                        id: noticeRepeater
                        model: page.accounts.filter(function(a) {
                            return a && page.shownPlatforms.indexOf(a.platform) >= 0
                                   && (!a.connected || (a.error && String(a.error) !== "") || (a.needsPermission && String(a.needsPermission) !== ""))
                        })
                        delegate: Rectangle {
                            id: notice
                            required property var modelData
                            readonly property bool problem: modelData.connected && !!modelData.error && String(modelData.error) !== ""
                            readonly property bool permission: modelData.connected && !!modelData.needsPermission && String(modelData.needsPermission) !== ""
                            objectName: "statsNotice-" + modelData.platform
                            Layout.fillWidth: true
                            Layout.preferredHeight: noticeColumn.implicitHeight + 16
                            radius: 8
                            color: page.cardColor
                            border.color: problem ? page.danger : permission ? page.warn : page.line
                            ColumnLayout {
                                id: noticeColumn
                                anchors.fill: parent
                                anchors.leftMargin: 12
                                anchors.rightMargin: 12
                                anchors.topMargin: 8
                                anchors.bottomMargin: 8
                                spacing: 4
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 10
                                    Badge { platformId: notice.modelData.platform }
                                    Label {
                                        objectName: "statsNoticeText-" + notice.modelData.platform
                                        Layout.fillWidth: true
                                        text: (notice.problem ? "⚠ " : notice.permission ? "🔒 " : "") + page.noticeText(notice.modelData)
                                        color: notice.problem ? page.danger : page.textColor
                                        wrapMode: Text.Wrap
                                        font.pixelSize: 13
                                        textFormat: Text.PlainText
                                    }
                                }
                                Flow {
                                    Layout.fillWidth: true
                                    spacing: 8
                                    Button {
                                        objectName: "statsAccountSettings-" + notice.modelData.platform
                                        text: qsTr("Open %1 settings").arg(Fmt.name(notice.modelData.platform))
                                        flat: true
                                        onClicked: page.manageAccount(notice.modelData.platform)
                                    }
                                    Button {
                                        objectName: "statsAppPermissions-" + notice.modelData.platform
                                        visible: notice.permission && notice.modelData.platform !== "youtube"
                                        text: qsTr("Enable app permissions")
                                        flat: true
                                        onClicked: backend.openLink("https://developers.facebook.com/apps/")
                                    }
                                    Button {
                                        objectName: "statsTokenTools-" + notice.modelData.platform
                                        text: notice.modelData.platform === "youtube" ? qsTr("Google OAuth setup") : qsTr("Meta token tools")
                                        flat: true
                                        onClicked: backend.openLink(notice.modelData.platform === "youtube"
                                            ? "https://console.cloud.google.com/apis/credentials"
                                            : "https://developers.facebook.com/tools/explorer/")
                                    }
                                    Button {
                                        objectName: "statsTokenDebugger-" + notice.modelData.platform
                                        visible: notice.modelData.platform !== "youtube"
                                        text: qsTr("Check expiry / extend token")
                                        flat: true
                                        onClicked: backend.openLink("https://developers.facebook.com/tools/debug/accesstoken/")
                                    }
                                }
                            }
                        }
                    }
                }

                // ---- 3. Summary cards ----
                GridLayout {
                    id: cardGrid
                    Layout.fillWidth: true
                    columns: Math.max(1, Math.floor((overviewColumn.width + columnSpacing) / (196 + columnSpacing)))
                    columnSpacing: 10
                    rowSpacing: 10
                    Repeater {
                        model: page.cards
                        delegate: Rectangle {
                            id: card
                            required property var modelData
                            objectName: "statsCard-" + modelData.label
                            Layout.fillWidth: true
                            Layout.preferredWidth: 196
                            Layout.preferredHeight: 122
                            radius: 10
                            color: page.cardColor
                            border.color: page.line
                            HoverHandler { id: cardHover }
                            ToolTip.visible: cardHover.hovered && !spark.hovering
                            ToolTip.delay: 500
                            ToolTip.text: modelData.label + ": " + Fmt.formatExact(modelData.value, modelData.unit)
                                          + (modelData.help ? "\n" + modelData.help : "")
                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: 12
                                spacing: 2
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 6
                                    Badge { visible: card.modelData.platform !== ""; platformId: card.modelData.platform; size: 18 }
                                    Label {
                                        Layout.fillWidth: true
                                        text: card.modelData.label
                                        color: page.dim
                                        font.pixelSize: 13
                                        elide: Text.ElideRight
                                    }
                                }
                                Label {
                                    text: Fmt.format(card.modelData.value, card.modelData.unit)
                                    color: isNaN(Fmt.toNum(card.modelData.value)) ? page.dim : page.textColor
                                    font.pixelSize: 26
                                    font.bold: true
                                }
                                Label {
                                    Layout.fillWidth: true
                                    text: card.modelData.note || ""
                                    color: page.dim
                                    font.pixelSize: 12
                                    elide: Text.ElideRight
                                }
                                Item { Layout.fillHeight: true }
                                Sparkline {
                                    id: spark
                                    readonly property bool hovering: hoverIndex >= 0
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 26
                                    visible: card.modelData.series && card.modelData.series.length > 0
                                    points: card.modelData.series || []
                                    color: card.modelData.platform !== "" ? Fmt.color(card.modelData.platform) : page.accentLine
                                    backgroundColor: page.cardColor
                                    unit: card.modelData.unit
                                }
                            }
                        }
                    }
                }

                // ---- 4 and 5. Charts ----
                GridLayout {
                    Layout.fillWidth: true
                    columns: overviewColumn.width >= 1040 ? 2 : 1
                    columnSpacing: 16
                    rowSpacing: 16

                    Section {
                        title: qsTr("Views per day")
                        subtitle: qsTr("Account views each day, by platform")
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.preferredWidth: 1
                        Layout.minimumHeight: 380
                        LineChart {
                            objectName: "statsViewsChart"
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            Layout.minimumHeight: 280
                            series: page.viewLines
                            valueTitle: qsTr("Views")
                            emptyText: page.viewLines.length === 0 ? qsTr("No connected accounts to show")
                                                                   : qsTr("No daily views from these accounts yet")
                            textColor: page.textColor
                            dimColor: page.dim
                            gridColor: page.grid
                            axisColor: page.axis
                            tooltipColor: page.tipColor
                            tooltipBorderColor: page.tipBorder
                            backgroundColor: page.cardColor
                        }
                    }

                    Section {
                        title: qsTr("Top reels")
                        subtitle: page.topReels.length === 0 ? qsTr("The best reels by any metric")
                                  : qsTr("Top %1 by %2").arg(page.topReels.length).arg(page.topColumn.label.toLowerCase())
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.preferredWidth: 1
                        action: ComboBox {
                            objectName: "statsTopMetric"
                            implicitWidth: 210
                            model: page.topOptions.map(function(c) { return c.label })
                            currentIndex: Math.max(0, page.topOptions.findIndex(function(c) { return c.key === page.topMetric }))
                            onActivated: function(i) { if (i >= 0 && i < page.topOptions.length) page.topMetric = page.topOptions[i].key }
                        }
                        BarChart {
                            objectName: "statsTopChart"
                            Layout.fillWidth: true
                            Layout.preferredHeight: implicitHeight
                            Layout.fillHeight: true
                            unit: page.topColumn.unit
                            valueTitle: page.topColumn.label
                            emptyText: qsTr("No reels with %1 in this period").arg(page.topColumn.label.toLowerCase())
                            items: page.topReels.map(function(r) {
                                return { label: page.captionOf(r), value: page.valueOf(r, page.topMetric),
                                         color: Fmt.color(r.platform), icon: Fmt.icon(r.platform), iconLabel: Fmt.name(r.platform) }
                            })
                            textColor: page.textColor
                            dimColor: page.dim
                            gridColor: page.grid
                            axisColor: page.axis
                            hoverColor: page.hoverColor
                            onActivated: function(i) { if (i >= 0 && i < page.topReels.length) page.select(page.topReels[i]) }
                        }
                    }
                }

                // ---- 6. Table title ----
                RowLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: 8
                    spacing: 10
                    Label { text: qsTr("All reels"); font.bold: true; font.pixelSize: 16; color: page.textColor }
                    Label {
                        Layout.fillWidth: true
                        text: (page.sorted.length === 1 ? qsTr("1 reel") : qsTr("%1 reels").arg(page.sorted.length)) + "  ·  " + qsTr("Click a heading to sort, a reel for details")
                        color: page.dim
                        font.pixelSize: 12
                        elide: Text.ElideRight
                    }
                }
            }
            TableHeader {
                id: tableHeader
                x: 0
                y: overviewColumn.y + overviewColumn.implicitHeight + 8
                width: list_.width
                visible: page.sorted.length > 0
            }
        }
    }

    function noticeText(a) {
        var n = Fmt.name(a.platform)
        if (!a.connected)
            return qsTr("Connect %1 in Settings → Connected accounts to see its reels.").arg(n)
        var parts = []
        if (a.needsPermission && String(a.needsPermission) !== "") {
            var perm = String(a.needsPermission)
            parts.push(a.platform === "youtube"
                ? qsTr("Reconnect YouTube in Settings → Connected accounts and allow YouTube Analytics (%1) to see watch time, shares and subscribers.").arg(perm)
                : qsTr("Add the %1 permission and reconnect %2 in Settings → Connected accounts to see views, reach and watch time.").arg(perm).arg(n))
        }
        if (a.error && String(a.error) !== "") {
            var error = String(a.error).replace(/in the Post dialog/g, qsTr("in Settings → Connected accounts"))
            parts.push(qsTr("%1: %2 After fixing the connection, press Refresh.").arg(n).arg(error.replace(/\s*$/, (/[.!?]\s*$/).test(error) ? "" : ".")))
        }
        return parts.join("  ")
    }
    readonly property color accentLine: dark ? accent : Qt.darker(accent, 1.6)

    // ---- Table pieces ----
    Component {
        id: reelRow
        Rectangle {
            id: row
            required property var modelData
            required property int index
            readonly property bool current: page.key(modelData) === page.selectedKey
            width: list_.width
            height: page.rowH
            color: current ? page.accentSoft : rowHover.hovered ? page.hoverColor : "transparent"
            HoverHandler { id: rowHover; cursorShape: Qt.PointingHandCursor }
            TapHandler { onTapped: page.select(row.modelData) }

            Rectangle { visible: row.current; width: 3; height: parent.height; color: page.accent }
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: page.line; opacity: 0.6 }

            Thumb {
                x: 12
                y: 4
                width: page.thumbW
                height: page.rowH - 8
                source: row.modelData.thumbnail ? String(row.modelData.thumbnail) : ""
                sourceHeight: 2 * (page.rowH - 8)
            }
            Label {
                x: page.captionX
                width: page.captionW
                anchors.verticalCenter: parent.verticalCenter
                text: page.captionOf(row.modelData)
                color: page.textColor
                font.pixelSize: 13
                wrapMode: Text.Wrap
                maximumLineCount: 2
                elide: Text.ElideRight
                textFormat: Text.PlainText
            }
            PlatformTag {
                x: page.platformX
                anchors.verticalCenter: parent.verticalCenter
                platformId: row.modelData.platform
            }
            Label {
                x: page.dateX
                width: page.dateW - 8
                anchors.verticalCenter: parent.verticalCenter
                text: Fmt.date(row.modelData.published)
                color: page.textColor
                font.pixelSize: 13
                HoverHandler { id: dateHover }
                ToolTip.visible: dateHover.hovered
                ToolTip.delay: 500
                ToolTip.text: Fmt.dateTime(row.modelData.published)
            }
            Item {
                x: page.metricX
                width: page.metricViewW
                height: parent.height
                clip: true
                Row {
                    x: -page.metricScroll
                    height: parent.height
                    Repeater {
                        model: page.columns
                        delegate: Item {
                            id: cell
                            required property var modelData
                            readonly property real value: page.valueOf(row.modelData, modelData.key)
                            width: page.metricW
                            height: page.rowH
                            Label {
                                anchors.fill: parent
                                anchors.rightMargin: 12
                                horizontalAlignment: Text.AlignRight
                                verticalAlignment: Text.AlignVCenter
                                text: Fmt.format(cell.value, cell.modelData.unit)
                                color: isNaN(cell.value) ? page.dim : page.textColor
                                font.pixelSize: 13
                                font.features: { "tnum": 1 }
                            }
                            HoverHandler { id: cellHover }
                            ToolTip.visible: cellHover.hovered && !isNaN(cell.value)
                            ToolTip.delay: 300
                            ToolTip.text: cell.modelData.label + ": " + Fmt.formatExact(cell.value, cell.modelData.unit)
                        }
                    }
                }
            }
        }
    }

    component TableHeader: Rectangle {
        height: 46
        color: page.panel
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: page.line }
        HeaderCell { x: page.captionX; width: page.captionW; sortKey: "caption"; label: qsTr("Reel"); alignRight: false }
        HeaderCell { x: page.platformX; width: page.platformW; sortKey: "platform"; label: qsTr("Platform"); alignRight: false }
        HeaderCell { x: page.dateX; width: page.dateW; sortKey: "published"; label: qsTr("Published"); alignRight: false }
        Item {
            x: page.metricX
            width: page.metricViewW
            height: parent.height
            clip: true
            Row {
                x: -page.metricScroll
                height: parent.height
                Repeater {
                    model: page.columns
                    delegate: HeaderCell {
                        required property var modelData
                        width: page.metricW
                        sortKey: modelData.key
                        label: modelData.label
                        alignRight: true
                    }
                }
            }
        }
    }

    component HeaderCell: Item {
        id: hc
        property string sortKey
        property string label
        property bool alignRight: true
        readonly property bool active: page.sortKey === sortKey
        height: 46
        Rectangle { anchors.fill: parent; color: hcHover.hovered ? page.hoverColor : "transparent" }
        Text {
            anchors.fill: parent
            anchors.rightMargin: hc.alignRight ? 12 : 4
            horizontalAlignment: hc.alignRight ? Text.AlignRight : Text.AlignLeft
            verticalAlignment: Text.AlignVCenter
            text: hc.label + (hc.active ? (page.sortDesc ? " ▼" : " ▲") : "")
            color: hc.active ? page.textColor : page.dim
            font.pixelSize: 12
            font.bold: hc.active
            wrapMode: Text.Wrap
            maximumLineCount: 2
            elide: Text.ElideRight
        }
        HoverHandler { id: hcHover; cursorShape: Qt.PointingHandCursor }
        TapHandler { onTapped: page.sortBy(hc.sortKey) }
        ToolTip.visible: hcHover.hovered
        ToolTip.delay: 500
        ToolTip.text: qsTr("Sort by %1").arg(hc.label.toLowerCase())
                      + (hc.active ? (page.sortDesc ? qsTr(" (highest first)") : qsTr(" (lowest first)")) : "")
    }

    // A platform's icon on its color, round.
    component Badge: Rectangle {
        property string platformId: ""
        property int size: 22
        implicitWidth: size
        implicitHeight: size
        width: size
        height: size
        radius: size / 2
        color: Fmt.color(platformId)
        // Drawn into a texture: the icon's curve renderer ignores the list's clipping.
        layer.enabled: true
        layer.smooth: true
        layer.textureSize: Qt.size(width * 2, height * 2)
        SocialIcon { anchors.centerIn: parent; width: parent.size * 0.62; height: width; name: Fmt.icon(parent.platformId) }
    }
    // Icon and name, so the platform never reads by color alone.
    component PlatformTag: Row {
        property string platformId: ""
        spacing: 6
        Badge { anchors.verticalCenter: parent.verticalCenter; platformId: parent.platformId; size: 20 }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: Fmt.name(parent.platformId)
            color: page.textColor
            font.pixelSize: 13
        }
    }

    component Thumb: Rectangle {
        id: th
        property string source: ""
        property int sourceHeight: 120
        radius: 4
        color: page.dark ? "#0c0c0e" : "#e4e4ea"
        border.color: page.line
        clip: true
        Image {
            id: thumbImage
            anchors.fill: parent
            anchors.margins: 1
            source: th.source
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
            cache: true
            sourceSize.height: th.sourceHeight
        }
        Label {
            anchors.centerIn: parent
            visible: th.source === "" || thumbImage.status === Image.Error || thumbImage.status === Image.Null
            text: "▶"
            color: page.dim
            font.pixelSize: Math.max(12, Math.min(22, th.width / 3))
        }
    }

    component Chip: Button {
        id: chip
        property string platformId: ""
        checkable: true
        autoExclusive: false
        flat: true
        leftPadding: 12
        rightPadding: 14
        topPadding: 0
        bottomPadding: 0
        topInset: 0
        bottomInset: 0
        implicitHeight: 34
        font.pixelSize: 13
        font.bold: checked
        contentItem: Row {
            spacing: 6
            Badge { visible: chip.platformId !== ""; anchors.verticalCenter: parent.verticalCenter; platformId: chip.platformId; size: 18 }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: chip.text
                color: page.textColor
                font: chip.font
            }
        }
        background: Rectangle {
            implicitHeight: 34
            radius: 17
            color: chip.checked ? page.accentSoft : chip.hovered ? page.hoverColor : "transparent"
            border.width: chip.checked ? 2 : 1
            border.color: chip.checked ? page.accentLine : page.line
        }
    }

    // A titled box for a chart.
    component Section: Rectangle {
        id: sec
        property string title
        property string subtitle
        property Item action: null
        default property alias content: secBody.data
        radius: 10
        color: page.cardColor
        border.color: page.line
        implicitHeight: secColumn.implicitHeight + 32
        ColumnLayout {
            id: secColumn
            anchors.fill: parent
            anchors.margins: 16
            spacing: 12
            RowLayout {
                Layout.fillWidth: true
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    Label { text: sec.title; font.bold: true; font.pixelSize: 16; color: page.textColor }
                    Label { text: sec.subtitle; visible: text !== ""; color: page.dim; font.pixelSize: 12; Layout.fillWidth: true; elide: Text.ElideRight }
                }
                Item {
                    id: actionSlot
                    Layout.preferredWidth: sec.action ? sec.action.implicitWidth : 0
                    Layout.preferredHeight: sec.action ? sec.action.implicitHeight : 0
                    data: sec.action ? [sec.action] : []
                }
            }
            ColumnLayout {
                id: secBody
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 0
            }
        }
    }
}
