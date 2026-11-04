import QtQuick

// Makes whatever it covers movable and resizable with the mouse: drag the
// inside to move, drag a corner or scroll to resize. Distances are in scene
// pixels from where the gesture started, so the box can move under the
// pointer without throwing the gesture off.
//
// `began` fires once a gesture really changes something (not on a plain
// click), which is where the owner records its undo step.
//
// Snapping: give `snap` an object {area, x, y, edgeX, edgeY} where area is the
// item the guides live in (the 9:16 frame) and the arrays are guide positions
// in that item's pixels. While moving, the box's center snaps to x/y guides
// and its edges to edgeX/edgeY guides; owners get the snapped dx/dy. Holding
// Ctrl turns snapping off for as long as it is held.
Item {
    id: box
    property bool selected: false
    property bool handlesInside: false
    readonly property real handleSize: 12

    property var snap: null
    // The box draws a little past what it covers (negative anchors.margins);
    // edges snap on the covered content, so this many pixels come off each side.
    property real snapInset: 0
    readonly property real snapDistance: 8
    // False while Ctrl is held during a drag.
    property bool snapping: true
    // The guide being snapped to, in snap.area pixels, or -1. Owners that snap
    // on their own (the picture's pan) can set these to show their guides.
    property real guideX: -1
    property real guideY: -1
    // Where the box sat in snap.area when the gesture started.
    property rect startRect

    // Finds the guide nearest the box (its center against `centers`, its two
    // edges against `edges`) within snapDistance; returns [newCenter, guide],
    // or [center, -1] when nothing is close enough.
    function snapAxis(center, half, centers, edges) {
        let best = null, bestGap = box.snapDistance
        const consider = function(value, guide) {
            const gap = Math.abs(guide - value)
            if (gap < bestGap) { bestGap = gap; best = [center + guide - value, guide] }
        }
        for (const g of centers || []) consider(center, g)
        for (const g of edges || []) { consider(center - half, g); consider(center + half, g) }
        return best || [center, -1]
    }

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
            if (box.snap && box.snap.area) {
                const tl = box.mapToItem(box.snap.area, box.snapInset, box.snapInset)
                box.startRect = Qt.rect(tl.x, tl.y, Math.max(0, box.width - 2 * box.snapInset),
                                        Math.max(0, box.height - 2 * box.snapInset))
            }
            box.pressed()
        }
        onReleased: { box.guideX = -1; box.guideY = -1 }
        onCanceled: { box.guideX = -1; box.guideY = -1 }
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
            let dx = p.x - start.x, dy = p.y - start.y
            box.snapping = !(mouse.modifiers & Qt.ControlModifier)
            if (box.snap && box.snap.area && box.snapping) {
                const r = box.startRect
                const sx = box.snapAxis(r.x + r.width / 2 + dx, r.width / 2, box.snap.x, box.snap.edgeX)
                const sy = box.snapAxis(r.y + r.height / 2 + dy, r.height / 2, box.snap.y, box.snap.edgeY)
                dx = sx[0] - r.x - r.width / 2
                dy = sy[0] - r.y - r.height / 2
                box.guideX = sx[1]
                box.guideY = sy[1]
            } else if (box.snap) {
                box.guideX = -1
                box.guideY = -1
            }
            box.dragged(dx, dy)
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

    // Guide lines while snapped: drawn in the frame, across all of it.
    Rectangle {
        parent: box.snap ? box.snap.area : box
        visible: box.snap !== null && box.guideX >= 0
        x: Math.round(box.guideX)
        y: 0
        z: 1000
        width: 1
        height: parent ? parent.height : 0
        color: "#ff3d9a"
    }
    Rectangle {
        parent: box.snap ? box.snap.area : box
        visible: box.snap !== null && box.guideY >= 0
        x: 0
        y: Math.round(box.guideY)
        z: 1000
        width: parent ? parent.width : 0
        height: 1
        color: "#ff3d9a"
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

            // The grab area reaches past the drawn square so it's a 28px
            // target (12 + 2 * 8) while the square itself stays small.
            MouseArea {
                anchors.fill: parent
                anchors.margins: -8
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
