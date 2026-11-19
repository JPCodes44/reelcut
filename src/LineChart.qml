pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import "StatsFormat.js" as Fmt

// Lines over time: one per series, on rounded value gridlines and date ticks,
// with a legend and a crosshair that shows each line's exact value.
//
// series: [{ name, color, marker ("circle", "square", "diamond"), icon (a
// SocialIcon name, optional), points: [{t: ms, v: number}] }]
Item {
    id: chart
    property var series: []
    // "count", "seconds", "percent" or "ratio": how values and ticks read.
    property string unit: "count"
    // Says what the values are, above the value axis.
    property string valueTitle: ""
    // Show the time of day in the tooltip, for snapshots taken at any hour.
    property bool showTime: false
    property bool legendVisible: true
    property string emptyText: qsTr("No data yet")

    property color textColor: "#ececf0"
    property color dimColor: "#9a9aa2"
    property color gridColor: "#2a2a30"
    property color axisColor: "#45454d"
    property color tooltipColor: "#232328"
    property color tooltipBorderColor: "#3a3a40"
    property color backgroundColor: "#17171a"
    property int fontSize: 12

    implicitWidth: 480
    implicitHeight: 280

    // The series with clean points, and the scales; rebuilt when data changes.
    property var lines: []
    property real tMin: 0
    property real tMax: 1
    property var yScale: Fmt.niceScale(0, 1, 4, unit)
    readonly property bool empty: lines.every(function(l) { return l.points.length === 0 })

    function rebuild() {
        var out = []
        var tLo = Infinity, tHi = -Infinity, vLo = Infinity, vHi = -Infinity
        var list = series && typeof series.length === "number" ? series : []
        for (var i = 0; i < list.length; ++i) {
            var s = list[i] || {}
            var pts = Fmt.cleanPoints(s.points)
            if (pts.length > 0) {
                tLo = Math.min(tLo, pts[0].t)
                tHi = Math.max(tHi, pts[pts.length - 1].t)
                for (var j = 0; j < pts.length; ++j) {
                    if (pts[j].v < vLo) vLo = pts[j].v
                    if (pts[j].v > vHi) vHi = pts[j].v
                }
            }
            out.push({ name: s.name || "", color: s.color || textColor, marker: s.marker || "circle",
                       icon: s.icon || "", points: pts })
        }
        if (!isFinite(tLo)) {
            tLo = Date.now() - 7 * Fmt.DAY
            tHi = Date.now()
        } else if (tHi - tLo < 1) {
            // One moment only: a few days either side, so the point sits in the middle.
            tLo -= 3 * Fmt.DAY
            tHi += 3 * Fmt.DAY
        }
        tMin = tLo
        tMax = tHi
        lines = out
        updateScale(vLo, vHi)
        canvas.requestPaint()
    }
    property real dataMin: 0
    property real dataMax: 0
    function updateScale(lo, hi) {
        dataMin = isFinite(lo) ? lo : 0
        dataMax = isFinite(hi) ? hi : 0
        yScale = Fmt.niceScale(dataMin, dataMax, Math.max(2, Math.floor(plot.height / 46)), unit)
    }
    onSeriesChanged: rebuild()
    onUnitChanged: rebuild()
    Component.onCompleted: rebuild()

    function xOf(t) { return plot.x + (tMax > tMin ? (t - tMin) / (tMax - tMin) : 0.5) * plot.width }
    function yOf(v) {
        var span = yScale.hi - yScale.lo
        return plot.y + plot.height - (span > 0 ? (v - yScale.lo) / span : 0) * plot.height
    }

    readonly property var xTicks: empty ? [] : Fmt.timeTicks(tMin, tMax, Math.max(2, Math.floor(plot.width / 96)), showTime)

    // ---- Legend ----
    Flow {
        id: legend
        x: plot.x
        width: chart.width - plot.x
        y: 0
        spacing: 16
        visible: chart.legendVisible && chart.lines.length > 0
        layoutDirection: Qt.LeftToRight
        Repeater {
            model: chart.legendVisible ? chart.lines : []
            delegate: Row {
                required property var modelData
                spacing: 6
                height: 22
                Item {
                    width: 26; height: 22
                    Rectangle { anchors.verticalCenter: parent.verticalCenter; width: 26; height: 3; radius: 1.5; color: modelData.color }
                    Marker { anchors.centerIn: parent; shape: modelData.marker; fill: modelData.color; outline: chart.backgroundColor; size: 9 }
                }
                Rectangle {
                    visible: modelData.icon !== ""
                    anchors.verticalCenter: parent.verticalCenter
                    width: 20; height: 20; radius: 10
                    color: modelData.color
                    // Drawn into a texture: the icon's curve renderer ignores clipping.
                    layer.enabled: true
                    layer.smooth: true
                    layer.textureSize: Qt.size(40, 40)
                    SocialIcon { anchors.centerIn: parent; width: 13; height: 13; name: modelData.icon }
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: modelData.name + (modelData.points.length === 0 ? qsTr(" (no data)") : "")
                    color: modelData.points.length === 0 ? chart.dimColor : chart.textColor
                    font.pixelSize: chart.fontSize + 1
                }
            }
        }
    }

    Text {
        id: valueTitleText
        x: 0
        y: legend.visible ? legend.height + 6 : 0
        text: chart.valueTitle
        visible: text !== "" && !chart.empty
        color: chart.dimColor
        font.pixelSize: chart.fontSize
        font.bold: true
    }

    FontMetrics { id: tickMetrics; font.pixelSize: chart.fontSize }
    readonly property real tickWidth: {
        var w = 0
        for (var i = 0; i < yScale.ticks.length; ++i)
            w = Math.max(w, tickMetrics.advanceWidth(Fmt.format(yScale.ticks[i], unit)))
        return Math.ceil(w)
    }

    // The plotting area, inside the axes.
    Item {
        id: plot
        x: chart.tickWidth + 10
        y: (valueTitleText.visible ? valueTitleText.y + valueTitleText.height : (legend.visible ? legend.height : 0)) + 10
        width: Math.max(10, chart.width - x - 12)
        height: Math.max(10, chart.height - y - 26)
        onHeightChanged: chart.updateScale(chart.dataMin, chart.dataMax)
    }

    Repeater {
        model: chart.empty ? [] : chart.yScale.ticks
        delegate: Text {
            required property var modelData
            x: plot.x - 8 - width
            y: chart.yOf(modelData) - height / 2
            text: Fmt.format(modelData, chart.unit)
            color: chart.dimColor
            font.pixelSize: chart.fontSize
            horizontalAlignment: Text.AlignRight
        }
    }
    Repeater {
        model: chart.xTicks
        delegate: Text {
            required property var modelData
            readonly property real cx: chart.xOf(modelData.t)
            // Kept inside the chart at both ends.
            x: Math.max(plot.x - 4, Math.min(chart.width - width, cx - width / 2))
            y: plot.y + plot.height + 6
            text: modelData.label
            color: chart.dimColor
            font.pixelSize: chart.fontSize
        }
    }

    Canvas {
        id: canvas
        anchors.fill: parent
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
        onPaint: {
            var ctx = getContext("2d")
            ctx.reset()
            ctx.clearRect(0, 0, width, height)
            var px = plot.x, py = plot.y, pw = plot.width, ph = plot.height

            // Gridlines, with a stronger one at zero.
            ctx.lineWidth = 1
            var ticks = chart.empty ? [] : chart.yScale.ticks
            for (var i = 0; i < ticks.length; ++i) {
                var gy = Math.round(chart.yOf(ticks[i])) + 0.5
                ctx.strokeStyle = ticks[i] === 0 ? chart.axisColor : chart.gridColor
                ctx.beginPath()
                ctx.moveTo(px, gy)
                ctx.lineTo(px + pw, gy)
                ctx.stroke()
            }
            ctx.strokeStyle = chart.gridColor
            var xt = chart.xTicks
            for (var k = 0; k < xt.length; ++k) {
                var gx = Math.round(chart.xOf(xt[k].t)) + 0.5
                ctx.beginPath()
                ctx.moveTo(gx, py)
                ctx.lineTo(gx, py + ph + 4)
                ctx.stroke()
            }
            if (chart.empty)
                return
            ctx.strokeStyle = chart.axisColor
            ctx.beginPath()
            ctx.moveTo(px + 0.5, py)
            ctx.lineTo(px + 0.5, py + ph)
            ctx.stroke()

            var lines = chart.lines
            for (var s = 0; s < lines.length; ++s) {
                var pts = lines[s].points
                if (pts.length === 0)
                    continue
                ctx.strokeStyle = lines[s].color
                ctx.fillStyle = lines[s].color
                ctx.lineWidth = 2.25
                ctx.lineJoin = "round"
                ctx.lineCap = "round"
                ctx.beginPath()
                if (pts.length > pw * 2) {
                    // Thousands of points: the lowest and highest of each pixel column.
                    var col = NaN, lo = 0, hi = 0, started = false
                    var flush = function() {
                        if (isNaN(col)) return
                        if (!started) { ctx.moveTo(col, chart.yOf(lo)); started = true }
                        else ctx.lineTo(col, chart.yOf(lo))
                        if (hi !== lo) ctx.lineTo(col, chart.yOf(hi))
                    }
                    for (var j = 0; j < pts.length; ++j) {
                        var c = Math.floor(chart.xOf(pts[j].t))
                        if (c !== col) {
                            flush()
                            col = c
                            lo = hi = pts[j].v
                        } else {
                            if (pts[j].v < lo) lo = pts[j].v
                            if (pts[j].v > hi) hi = pts[j].v
                        }
                    }
                    flush()
                } else {
                    for (var p = 0; p < pts.length; ++p) {
                        var x = chart.xOf(pts[p].t), y = chart.yOf(pts[p].v)
                        if (p === 0) ctx.moveTo(x, y)
                        else ctx.lineTo(x, y)
                    }
                }
                ctx.stroke()

                // Points, in each line's own shape, while there's room to see them.
                if (pts.length <= pw / 10) {
                    for (var q = 0; q < pts.length; ++q)
                        drawMarker(ctx, lines[s].marker, chart.xOf(pts[q].t), chart.yOf(pts[q].v), pts.length === 1 ? 5 : 3.5)
                }
            }
        }
        function drawMarker(ctx, shape, x, y, r) {
            ctx.beginPath()
            if (shape === "square") {
                ctx.rect(x - r, y - r, 2 * r, 2 * r)
            } else if (shape === "diamond") {
                ctx.moveTo(x, y - r * 1.3); ctx.lineTo(x + r * 1.3, y); ctx.lineTo(x, y + r * 1.3); ctx.lineTo(x - r * 1.3, y)
                ctx.closePath()
            } else {
                ctx.arc(x, y, r, 0, 2 * Math.PI)
            }
            ctx.fill()
        }
    }
    Connections {
        target: chart
        function onYScaleChanged() { canvas.requestPaint() }
        function onTextColorChanged() { canvas.requestPaint() }
        function onGridColorChanged() { canvas.requestPaint() }
        function onAxisColorChanged() { canvas.requestPaint() }
    }

    Text {
        anchors.centerIn: plot
        visible: chart.empty
        width: plot.width - 24
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.Wrap
        text: chart.emptyText
        color: chart.dimColor
        font.pixelSize: chart.fontSize + 1
    }

    // ---- Crosshair ----
    property real hoverX: -1
    // The hovered moment and each line's value there.
    readonly property var hover: {
        if (hoverX < 0 || empty)
            return null
        var t = tMin + (hoverX - plot.x) / plot.width * (tMax - tMin)
        // Snap to the nearest point of any line.
        var best = NaN
        for (var i = 0; i < lines.length; ++i) {
            var n = Fmt.nearest(lines[i].points, t)
            if (n >= 0 && (isNaN(best) || Math.abs(lines[i].points[n].t - t) < Math.abs(best - t)))
                best = lines[i].points[n].t
        }
        if (isNaN(best))
            return null
        var rows = []
        for (var k = 0; k < lines.length; ++k) {
            var pts = lines[k].points
            var m = Fmt.nearest(pts, best)
            // Other lines count only with a point on the same day.
            var ok = m >= 0 && Math.abs(pts[m].t - best) < (showTime ? 1 : Fmt.DAY / 2)
            rows.push({ name: lines[k].name, color: lines[k].color, marker: lines[k].marker,
                        v: ok ? pts[m].v : NaN, has: ok })
        }
        return { t: best, rows: rows }
    }

    MouseArea {
        x: plot.x
        y: plot.y
        width: plot.width
        height: plot.height
        hoverEnabled: true
        acceptedButtons: Qt.NoButton
        onPositionChanged: function(mouse) { chart.hoverX = plot.x + mouse.x }
        onExited: chart.hoverX = -1
    }

    Rectangle {
        visible: chart.hover !== null
        x: chart.hover ? Math.round(chart.xOf(chart.hover.t)) : 0
        y: plot.y
        width: 1
        height: plot.height
        color: chart.dimColor
    }
    Repeater {
        model: chart.hover ? chart.hover.rows : []
        delegate: Marker {
            required property var modelData
            visible: modelData.has
            x: chart.xOf(chart.hover ? chart.hover.t : 0) - width / 2
            y: (modelData.has ? chart.yOf(modelData.v) : 0) - height / 2
            shape: modelData.marker
            fill: modelData.color
            outline: chart.backgroundColor
            size: 11
        }
    }

    Rectangle {
        id: tip
        visible: chart.hover !== null
        readonly property real cx: chart.hover ? chart.xOf(chart.hover.t) : 0
        width: tipColumn.implicitWidth + 20
        height: tipColumn.implicitHeight + 14
        x: cx + 14 + width > chart.width ? Math.max(0, cx - 14 - width) : cx + 14
        y: plot.y + 4
        radius: 6
        color: chart.tooltipColor
        border.color: chart.tooltipBorderColor
        Column {
            id: tipColumn
            x: 10
            y: 7
            spacing: 4
            Text {
                text: chart.hover ? (chart.showTime ? Fmt.dateTime(chart.hover.t) : Fmt.longDate(chart.hover.t)) : ""
                color: chart.textColor
                font.pixelSize: chart.fontSize
                font.bold: true
            }
            Repeater {
                model: chart.hover ? chart.hover.rows : []
                delegate: Row {
                    id: tipRow
                    required property var modelData
                    spacing: 6
                    Marker { anchors.verticalCenter: parent.verticalCenter; shape: tipRow.modelData.marker; fill: tipRow.modelData.color; outline: chart.tooltipColor; size: 9 }
                    Text {
                        text: (tipRow.modelData.name !== "" ? tipRow.modelData.name + ": " : "")
                              + (tipRow.modelData.has ? Fmt.formatExact(tipRow.modelData.v, chart.unit) : qsTr("no data"))
                        color: tipRow.modelData.has ? chart.textColor : chart.dimColor
                        font.pixelSize: chart.fontSize
                    }
                }
            }
        }
    }

    // A point drawn as an item: legend, crosshair and tooltip.
    component Marker: Item {
        id: marker
        property string shape: "circle"
        property color fill: "white"
        property color outline: "black"
        property real size: 9
        width: size
        height: size
        Rectangle {
            anchors.centerIn: parent
            width: marker.shape === "diamond" ? marker.size * 0.78 : marker.size
            height: width
            radius: marker.shape === "circle" ? width / 2 : 1
            rotation: marker.shape === "diamond" ? 45 : 0
            color: marker.fill
            border.color: marker.outline
            border.width: 1.5
        }
    }
}
