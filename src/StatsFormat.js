.pragma library

// Numbers, dates, scales and platform styling shared by the Stats page and
// its charts, so every chart reads the same way.

var platforms = ["instagram", "facebook", "youtube"]
var names = { instagram: "Instagram", facebook: "Facebook", youtube: "YouTube" }
// SocialIcon glyphs, drawn white on the platform color.
var icons = { instagram: "camera", facebook: "thumbUp", youtube: "shorts" }
// Each platform also has its own point shape, so lines never differ by color alone.
var markers = { instagram: "circle", facebook: "square", youtube: "diamond" }
// Picked to stay apart for color-blind eyes too (blue, vermilion, reddish
// purple) and to read on both dark and light backgrounds.
var colors = { instagram: "#c95aa6", facebook: "#2f7fe0", youtube: "#e2482c" }

function name(p) { return names[p] || String(p || "") }
function icon(p) { return icons[p] || "video" }
function marker(p) { return markers[p] || "circle" }
function color(p) { return colors[p] || "#8a8a94" }

// What the page falls back to when the backend doesn't list its metrics.
var defaultMetricInfo = [
    { key: "views", label: "Views", unit: "count" },
    { key: "reach", label: "Accounts reached", unit: "count" },
    { key: "likes", label: "Likes", unit: "count" },
    { key: "comments", label: "Comments", unit: "count" },
    { key: "shares", label: "Shares", unit: "count" },
    { key: "saves", label: "Saves", unit: "count" },
    { key: "interactions", label: "Total interactions", unit: "count" },
    { key: "replays", label: "Replays", unit: "count" },
    { key: "initialPlays", label: "Initial plays", unit: "count" },
    { key: "avgWatch", label: "Avg watch time", unit: "seconds" },
    { key: "totalWatch", label: "Total watch time", unit: "seconds" },
    { key: "avgViewPct", label: "Avg % viewed", unit: "percent" },
    { key: "followsGained", label: "Followers gained", unit: "count" },
    { key: "subsLost", label: "Subscribers lost", unit: "count" }
]

// A number, or NaN for anything missing or broken.
function toNum(v) {
    if (v === null || v === undefined || v === "" || typeof v === "boolean")
        return NaN
    var n = Number(v)
    return isFinite(n) ? n : NaN
}
function has(v) { return !isNaN(toNum(v)) }

function trimZero(s) { return s.replace(/\.0+$/, "") }

// 950, 1.2K, 12.3K, 123K, 4.5M, 1.2B.
function abbrev(v) {
    var n = toNum(v)
    if (isNaN(n))
        return "—"
    var a = Math.abs(n), sign = n < 0 ? "−" : ""
    var units = [[1e12, "T"], [1e9, "B"], [1e6, "M"], [1e3, "K"]]
    for (var i = 0; i < units.length; ++i) {
        if (a >= units[i][0] * 0.9995) {
            var x = a / units[i][0]
            // 999.96K would round to "1000K": move up a unit instead.
            if (x >= 999.5 && i > 0)
                return sign + trimZero((a / units[i - 1][0]).toFixed(1)) + units[i - 1][1]
            return sign + (x >= 100 ? Math.round(x).toString() : trimZero(x.toFixed(1))) + units[i][1]
        }
    }
    if (a >= 100 || a === Math.round(a))
        return sign + Math.round(a).toString()
    return sign + trimZero(a.toFixed(a < 10 ? 1 : 0))
}

// 1,234,567 (locale separators), with up to two decimals when it has some.
function exact(v) {
    var n = toNum(v)
    if (isNaN(n))
        return "—"
    var decimals = n === Math.round(n) ? 0 : Math.abs(n) < 10 ? 2 : 1
    return n.toLocaleString(Qt.locale(), 'f', decimals)
}

function pad2(n) { return (n < 10 ? "0" : "") + n }

// 0:14, 3:05, 1:02:03; beyond 100 hours "1.2K h".
function clock(v) {
    var n = toNum(v)
    if (isNaN(n))
        return "—"
    var sign = n < 0 ? "−" : ""
    var s = Math.round(Math.abs(n))
    if (s >= 360000)
        return sign + abbrev(s / 3600) + " h"
    var h = Math.floor(s / 3600), m = Math.floor((s % 3600) / 60), sec = s % 60
    return sign + (h > 0 ? h + ":" + pad2(m) : m) + ":" + pad2(sec)
}

// Percent of 0..100: 38%, 4.5%.
function percent(v) {
    var n = toNum(v)
    if (isNaN(n))
        return "—"
    var a = Math.abs(n)
    return (a > 0 && a < 10 ? trimZero(n.toFixed(1)) : Math.round(n).toString()) + "%"
}

// Short form for a cell, a bar or a tick.
function format(v, unit) {
    if (unit === "seconds") return clock(v)
    if (unit === "percent") return percent(v)
    if (unit === "ratio") return percent(toNum(v) * 100)
    return abbrev(v)
}

// Full form for a tooltip.
function formatExact(v, unit) {
    var n = toNum(v)
    if (isNaN(n))
        return "—"
    if (unit === "seconds") {
        var c = clock(n)
        return Math.abs(n) >= 60 ? c + " (" + exact(Math.round(n)) + " s)" : c + " (" + exact(n) + " s)"
    }
    if (unit === "percent")
        return n.toLocaleString(Qt.locale(), 'f', 1) + "%"
    if (unit === "ratio")
        return (n * 100).toLocaleString(Qt.locale(), 'f', 2) + "%"
    return exact(n)
}

// ---- Scales ----

// A rounded step near `raw`: 1, 2, 2.5, 5 × 10^n (whole numbers for counts),
// or minutes and hours for seconds.
function niceStep(raw, unit) {
    if (!(raw > 0) || !isFinite(raw))
        return 1
    if (unit === "seconds" && raw >= 1) {
        var steps = [1, 2, 5, 10, 15, 30, 60, 120, 300, 600, 900, 1800, 3600, 7200, 10800, 21600, 43200, 86400]
        for (var i = 0; i < steps.length; ++i)
            if (steps[i] >= raw)
                return steps[i]
        return niceStep(raw / 86400, "count") * 86400
    }
    var exp = Math.floor(Math.log(raw) / Math.LN10)
    var base = Math.pow(10, exp)
    var f = raw / base
    var nice = f <= 1 ? 1 : f <= 2 ? 2 : f <= 2.5 ? 2.5 : f <= 5 ? 5 : 10
    var step = nice * base
    if (unit === "count" || unit === undefined)
        step = Math.max(1, nice === 2.5 && base < 10 ? 2 * base : step)
    return step
}

// Gridlines from zero (or below, for negatives) up past the largest value.
function niceScale(minV, maxV, maxTicks, unit) {
    var lo = Math.min(0, isFinite(minV) ? minV : 0)
    var hi = Math.max(0, isFinite(maxV) ? maxV : 0)
    if (hi === lo)
        hi = lo + (unit === "percent" ? 10 : unit === "ratio" ? 0.1 : unit === "seconds" ? 10 : 1)
    var count = Math.max(2, Math.floor(maxTicks) || 2)
    var step = niceStep((hi - lo) / count, unit)
    var a = Math.floor(lo / step) * step, b = Math.ceil(hi / step) * step
    var ticks = []
    for (var v = a, guard = 0; v <= b + step / 2 && guard < 200; v += step, ++guard)
        ticks.push(Math.abs(v) < step * 1e-9 ? 0 : v)
    return { lo: a, hi: b, step: step, ticks: ticks }
}

var DAY = 86400000

function startOfDay(ms) { var d = new Date(ms); d.setHours(0, 0, 0, 0); return d.getTime() }

// Date gridlines: whole days, weeks or months, about `maxTicks` of them.
function timeTicks(tMin, tMax, maxTicks, allowHours) {
    var out = []
    if (!isFinite(tMin) || !isFinite(tMax) || tMax < tMin)
        return out
    var span = Math.max(tMax - tMin, 1)
    var count = Math.max(2, Math.floor(maxTicks) || 2)
    var yearly = new Date(tMin).getFullYear() !== new Date(tMax).getFullYear()
    if (allowHours && span <= 2 * DAY && span / count < DAY) {
        // Snapshots within a day or two: hours.
        var hours = [1, 2, 3, 6, 12]
        var hs = 24
        for (var i = 0; i < hours.length; ++i)
            if (span / (hours[i] * 3600000) <= count) { hs = hours[i]; break }
        var t = Math.ceil(tMin / (hs * 3600000)) * hs * 3600000
        for (var g = 0; t <= tMax && g < 100; t += hs * 3600000, ++g)
            out.push({ t: t, label: Qt.formatDateTime(new Date(t), "MMM d h AP") })
        return out
    }
    var days = [1, 2, 3, 7, 14]
    for (var j = 0; j < days.length; ++j) {
        if (span / (days[j] * DAY) <= count) {
            var d = new Date(startOfDay(tMin))
            if (d.getTime() < tMin) d.setDate(d.getDate() + 1)
            if (days[j] === 7 || days[j] === 14) {
                // Weeks start on Monday.
                while (d.getDay() !== 1) d.setDate(d.getDate() + 1)
            }
            for (var k = 0; d.getTime() <= tMax && k < 100; ++k) {
                out.push({ t: d.getTime(), label: Qt.formatDate(d, yearly ? "MMM d yy" : "MMM d") })
                d.setDate(d.getDate() + days[j])
            }
            return out
        }
    }
    var months = [1, 2, 3, 6, 12, 24, 60]
    var ms = 60
    for (var m = 0; m < months.length; ++m)
        if (span / (months[m] * 30.44 * DAY) <= count) { ms = months[m]; break }
    var dm = new Date(tMin)
    dm = new Date(dm.getFullYear(), dm.getMonth(), 1)
    if (dm.getTime() < tMin) dm.setMonth(dm.getMonth() + 1)
    while (dm.getMonth() % Math.min(ms, 12) !== 0 && ms > 1) dm.setMonth(dm.getMonth() + 1)
    for (var n = 0; dm.getTime() <= tMax && n < 100; ++n) {
        out.push({ t: dm.getTime(), label: Qt.formatDate(dm, ms >= 12 ? "yyyy" : (yearly ? "MMM yyyy" : "MMM")) })
        dm.setMonth(dm.getMonth() + ms)
    }
    return out
}

function date(ms) {
    var n = toNum(ms)
    if (isNaN(n) || n <= 0)
        return "—"
    var d = new Date(n)
    return Qt.formatDate(d, d.getFullYear() === new Date().getFullYear() ? "MMM d" : "MMM d, yyyy")
}
function longDate(ms) {
    var n = toNum(ms)
    return isNaN(n) || n <= 0 ? "—" : Qt.formatDate(new Date(n), "ddd MMM d, yyyy")
}
function dateTime(ms) {
    var n = toNum(ms)
    return isNaN(n) || n <= 0 ? "—" : Qt.formatDateTime(new Date(n), "MMM d, yyyy h:mm AP")
}

// Clean [{t, v}]: numbers only, oldest first.
function cleanPoints(points) {
    var out = []
    if (!points || typeof points.length !== "number")
        return out
    for (var i = 0; i < points.length; ++i) {
        var p = points[i]
        if (!p) continue
        var t = toNum(p.t), v = toNum(p.v)
        if (!isNaN(t) && !isNaN(v))
            out.push({ t: t, v: v })
    }
    var sorted = true
    for (var j = 1; j < out.length; ++j)
        if (out[j].t < out[j - 1].t) { sorted = false; break }
    if (!sorted)
        out.sort(function(a, b) { return a.t - b.t })
    return out
}

// Index of the point nearest to t in points sorted by t, or -1.
function nearest(points, t) {
    if (!points || points.length === 0)
        return -1
    var lo = 0, hi = points.length - 1
    while (hi - lo > 1) {
        var mid = (lo + hi) >> 1
        if (points[mid].t < t) lo = mid
        else hi = mid
    }
    return Math.abs(points[lo].t - t) <= Math.abs(points[hi].t - t) ? lo : hi
}
