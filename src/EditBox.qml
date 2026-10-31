import QtQuick

// Makes whatever it covers movable and resizable with the mouse: drag the
// inside to move, drag a corner or scroll to resize. Distances are in scene
// pixels from where the gesture started, so the box can move under the
// pointer without throwing the gesture off.
//
// `began` fires once a gesture really changes something (not on a plain
// click), which is where the owner records its undo step.
Item {
    id: box
    property bool selected: false
    property bool handlesInside: false
    readonly property real handleSize: 12

    signal pressed()
    signal began()
    signal dragged(real dx, real dy)
    signal resized(real factor)
    signal wheeled(real steps)

    // A run of wheel steps counts as one edit.
    property real lastWheel: 0

    MouseArea {
        id: body
        anchors.fill: parent
        preventStealing: true
        hoverEnabled: true
        cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
        property point start
        property bool moving: false
        onPressed: function(mouse) {
            start = mapToItem(null, mouse.x, mouse.y)
            moving = false
            box.pressed()
        }
        onPositionChanged: function(mouse) {
            if (!pressed)
                return
            const p = mapToItem(null, mouse.x, mouse.y)
            if (!moving && Math.abs(p.x - start.x) + Math.abs(p.y - start.y) < 3)
                return
            if (!moving) {
                moving = true
                box.began()
            }
            box.dragged(p.x - start.x, p.y - start.y)
        }
        onWheel: function(wheel) {
            const now = Date.now()
            if (now - box.lastWheel > 700) {
                box.pressed()
                box.began()
            }
            box.lastWheel = now
            box.wheeled(wheel.angleDelta.y / 120)
        }
    }

    Rectangle {
        anchors.fill: parent
        visible: box.selected
        color: "transparent"
        border.color: theme.accent
        border.width: 1
    }

    Repeater {
        model: 4
        delegate: Rectangle {
            required property int index
            readonly property bool onRight: index % 2 === 1
            readonly property bool onBottom: index >= 2
            visible: box.selected
            width: box.handleSize
            height: box.handleSize
            x: (onRight ? box.width : 0) - (box.handlesInside ? (onRight ? box.handleSize : 0) : box.handleSize / 2)
            y: (onBottom ? box.height : 0) - (box.handlesInside ? (onBottom ? box.handleSize : 0) : box.handleSize / 2)
            color: "white"
            border.color: theme.accent
            border.width: 2

            MouseArea {
                anchors.fill: parent
                anchors.margins: -4
                preventStealing: true
                cursorShape: parent.onRight === parent.onBottom ? Qt.SizeFDiagCursor : Qt.SizeBDiagCursor
                property point center
                property real startDistance: 1
                property bool moving: false
                onPressed: function(mouse) {
                    center = box.mapToItem(null, box.width / 2, box.height / 2)
                    const p = mapToItem(null, mouse.x, mouse.y)
                    startDistance = Math.max(1, Math.hypot(p.x - center.x, p.y - center.y))
                    moving = false
                    box.pressed()
                }
                onPositionChanged: function(mouse) {
                    if (!pressed)
                        return
                    const p = mapToItem(null, mouse.x, mouse.y)
                    const factor = Math.hypot(p.x - center.x, p.y - center.y) / startDistance
                    if (!moving && Math.abs(factor - 1) < 0.01)
                        return
                    if (!moving) {
                        moving = true
                        box.began()
                    }
                    box.resized(factor)
                }
            }
        }
    }
}
