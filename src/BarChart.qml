pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import "StatsFormat.js" as Fmt

// Labelled horizontal bars, largest first as given, each with its value at
// the end, on rounded gridlines. Clicking a bar emits activated(index).
//
// items: [{ label, value, color, icon (SocialIcon name, optional),
//           iconLabel (says what the icon means, for the tooltip) }]
Item {
    id: chart
    property var items: []
    property string unit: "count"
    property string emptyText: qsTr("No data yet")
    property string valueTitle: ""
    property int barHeight: 30
    property real rowSpacing: 6

    property color textColor: "#ececf0"
    property color dimColor: "#9a9aa2"
    property color gridColor: "#2a2a30"
    property color axisColor: "#45454d"
    property color trackColor: "#202026"
    property color hoverColor: "#26262c"
    property int fontSize: 12

    signal activated(int index)

    readonly property var rows: {
        var out = []
        var list = items && typeof items.length === "number" ? items : []
        for (var i = 0; i < list.length; ++i) {
            var it = list[i] || {}
            var v = Fmt.toNum(it.value)
            out.push({ label: String(it.label || ""), value: isNaN(v) ? 0 : Math.max(0, v), missing: isNaN(v),
                       color: it.color || chart.textColor, icon: it.icon || "", iconLabel: it.iconLabel || "" })
        }
        return out
    }
    readonly property real maxValue: rows.reduce(function(m, r) { return Math.max(m, r.value) }, 0)

    implicitWidth: 480
    implicitHeight: titleText.height + 8 + Math.max(1, rows.length) * (barHeight + rowSpacing) + 24

    // Room for the longest value at the end of the longest bar.
    FontMetrics { id: valueMetrics; font.pixelSize: chart.fontSize; font.bold: true }
    readonly property real valueWidth: {
        var w = 0
        for (var i = 0; i < rows.length; ++i)
            w = Math.max(w, valueMetrics.advanceWidth(Fmt.format(rows[i].value, unit)))
        return Math.ceil(w) + 10
    }
    readonly property real labelWidth: Math.min(Math.max(140, width * 0.36), 300)
    readonly property real barX: labelWidth + 10
    readonly property real barWidth: Math.max(20, width - barX - valueWidth)
    readonly property var scale: Fmt.niceScale(0, maxValue, Math.max(2, Math.floor(barWidth / 90)), unit)
    function xOf(v) { return barX + (scale.hi > 0 ? v / scale.hi : 0) * barWidth }

    Text {
        id: titleText
        text: chart.valueTitle
        visible: chart.rows.length > 0
        height: text === "" || !visible ? 0 : Math.ceil(valueMetrics.height)
        color: chart.dimColor
        font.pixelSize: chart.fontSize
        font.bold: true
        x: chart.barX
    }

    Item {
        id: area
        y: titleText.height + (titleText.height > 0 ? 8 : 0)
        width: chart.width
        height: Math.max(1, chart.rows.length) * (chart.barHeight + chart.rowSpacing)

        // Gridlines and the zero axis.
        Repeater {
            model: chart.rows.length > 0 ? chart.scale.ticks : []
            delegate: Rectangle {
                required property var modelData
                x: Math.round(chart.xOf(modelData))
                y: 0
                width: 1
                height: area.height
                color: modelData === 0 ? chart.axisColor : chart.gridColor
            }
        }
        Repeater {
            model: chart.rows.length > 0 ? chart.scale.ticks : []
            delegate: Text {
                required property var modelData
                x: Math.min(chart.width - width, Math.round(chart.xOf(modelData) - width / 2))
                y: area.height + 4
                text: Fmt.format(modelData, chart.unit)
                color: chart.dimColor
                font.pixelSize: chart.fontSize
            }
        }

        Repeater {
            model: chart.rows
            delegate: Item {
                id: row
                required property var modelData
                required property int index
                x: 0
                y: index * (chart.barHeight + chart.rowSpacing)
                width: chart.width
                height: chart.barHeight

                Rectangle {
                    anchors.fill: parent
                    anchors.leftMargin: -4
                    radius: 4
                    color: hover.hovered ? chart.hoverColor : "transparent"
                }
                Row {
                    x: 0
                    width: chart.labelWidth
                    height: parent.height
                    spacing: 6
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        width: 22
                        horizontalAlignment: Text.AlignRight
                        text: (row.index + 1) + "."
                        color: chart.dimColor
                        font.pixelSize: chart.fontSize
                    }
                    Rectangle {
                        visible: row.modelData.icon !== ""
                        anchors.verticalCenter: parent.verticalCenter
                        width: 20; height: 20; radius: 10
                        color: row.modelData.color
                        // Drawn into a texture: the icon's curve renderer ignores clipping.
                        layer.enabled: true
                        layer.smooth: true
                        layer.textureSize: Qt.size(40, 40)
                        SocialIcon { anchors.centerIn: parent; width: 13; height: 13; name: row.modelData.icon }
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        width: chart.labelWidth - 28 - (row.modelData.icon !== "" ? 26 : 0) - 6
                        text: row.modelData.label
                        elide: Text.ElideRight
                        maximumLineCount: 1
                        color: chart.textColor
                        font.pixelSize: chart.fontSize + 1
                    }
                }
                Rectangle {
                    x: chart.barX
                    anchors.verticalCenter: parent.verticalCenter
                    width: Math.max(row.modelData.value > 0 ? 2 : 0, chart.xOf(row.modelData.value) - chart.barX)
                    height: chart.barHeight - 8
                    radius: 3
                    color: row.modelData.color
                    opacity: hover.hovered ? 1 : 0.9
                }
                Text {
                    x: chart.xOf(row.modelData.value) + 6
                    anchors.verticalCenter: parent.verticalCenter
                    text: Fmt.format(row.modelData.missing ? NaN : row.modelData.value, chart.unit)
                    color: chart.textColor
                    font.pixelSize: chart.fontSize
                    font.bold: true
                }
                HoverHandler { id: hover; cursorShape: Qt.PointingHandCursor }
                TapHandler { onTapped: chart.activated(row.index) }
                ToolTip.visible: hover.hovered
                ToolTip.delay: 300
                ToolTip.text: (row.modelData.iconLabel !== "" ? row.modelData.iconLabel + " · " : "")
                              + row.modelData.label + "\n"
                              + Fmt.formatExact(row.modelData.missing ? NaN : row.modelData.value, chart.unit)
            }
        }
    }

    Text {
        anchors.centerIn: parent
        visible: chart.rows.length === 0
        width: parent.width - 24
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.Wrap
        text: chart.emptyText
        color: chart.dimColor
        font.pixelSize: chart.fontSize + 1
    }
}
