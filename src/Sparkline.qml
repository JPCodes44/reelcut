pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import "StatsFormat.js" as Fmt

// A small trend line for a summary card: points [{t, v}], oldest first.
// Hovering shows the nearest day's exact value.
Item {
    id: spark
    property var points: []
    property color color: "#2f7fe0"
    property color backgroundColor: "#1c1c21"
    property string unit: "count"
    implicitWidth: 140
    implicitHeight: 32

    readonly property var clean: Fmt.cleanPoints(points)
    readonly property real tMin: clean.length > 0 ? clean[0].t : 0
    readonly property real tMax: clean.length > 0 ? clean[clean.length - 1].t : 1
    readonly property real vMin: clean.reduce(function(m, p) { return Math.min(m, p.v) }, Infinity)
    readonly property real vMax: clean.reduce(function(m, p) { return Math.max(m, p.v) }, -Infinity)

    function xOf(t) { return 3 + (tMax > tMin ? (t - tMin) / (tMax - tMin) : 0.5) * (width - 6) }
    function yOf(v) {
        // A flat series sits in the middle rather than on an edge.
        if (!(vMax > vMin)) return height / 2
        return 3 + (1 - (v - vMin) / (vMax - vMin)) * (height - 6)
    }

    onCleanChanged: canvas.requestPaint()
    onColorChanged: canvas.requestPaint()

    Canvas {
        id: canvas
        anchors.fill: parent
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
        onPaint: {
            var ctx = getContext("2d")
            ctx.reset()
            ctx.clearRect(0, 0, width, height)
            var pts = spark.clean
            if (pts.length === 0)
                return
            var step = Math.max(1, Math.floor(pts.length / Math.max(1, width)))
            ctx.beginPath()
            ctx.moveTo(spark.xOf(pts[0].t), spark.yOf(pts[0].v))
            for (var i = step; i < pts.length; i += step)
                ctx.lineTo(spark.xOf(pts[i].t), spark.yOf(pts[i].v))
            ctx.lineTo(spark.xOf(pts[pts.length - 1].t), spark.yOf(pts[pts.length - 1].v))
            if (pts.length === 1) {
                ctx.moveTo(3, height / 2)
                ctx.lineTo(width - 3, height / 2)
            }
            ctx.strokeStyle = spark.color
            ctx.lineWidth = 2
            ctx.lineJoin = "round"
            ctx.stroke()
            // Under the line, faintly.
            if (pts.length > 1) {
                ctx.lineTo(spark.xOf(pts[pts.length - 1].t), height)
                ctx.lineTo(spark.xOf(pts[0].t), height)
                ctx.closePath()
                ctx.globalAlpha = 0.15
                ctx.fillStyle = spark.color
                ctx.fill()
                ctx.globalAlpha = 1
            }
            var last = pts[pts.length - 1]
            ctx.beginPath()
            ctx.arc(spark.xOf(last.t), spark.yOf(last.v), 3, 0, 2 * Math.PI)
            ctx.fillStyle = spark.color
            ctx.fill()
        }
    }

    property int hoverIndex: -1
    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.NoButton
        onPositionChanged: function(mouse) {
            var pts = spark.clean
            if (pts.length === 0) { spark.hoverIndex = -1; return }
            var t = spark.tMin + (mouse.x - 3) / Math.max(1, spark.width - 6) * (spark.tMax - spark.tMin)
            spark.hoverIndex = Fmt.nearest(pts, t)
        }
        onExited: spark.hoverIndex = -1
    }
    Rectangle {
        visible: spark.hoverIndex >= 0 && spark.hoverIndex < spark.clean.length
        readonly property var p: visible ? spark.clean[spark.hoverIndex] : null
        x: p ? spark.xOf(p.t) - 4 : 0
        y: p ? spark.yOf(p.v) - 4 : 0
        width: 8; height: 8; radius: 4
        color: spark.color
        border.color: spark.backgroundColor
        border.width: 1.5
    }
    ToolTip.visible: hoverIndex >= 0 && hoverIndex < clean.length
    ToolTip.text: ToolTip.visible ? Fmt.longDate(clean[hoverIndex].t) + ": " + Fmt.formatExact(clean[hoverIndex].v, unit) : ""
}
