import QtQuick
import QtQuick.Shapes

// Vector outlines keep both platforms consistent at every preview scale.
Item {
    id: root
    property string name: "heart"
    implicitWidth: 24
    implicitHeight: 24
    readonly property var paths: ({
        heart: "M12 21 C10 19 2 14 2 8 C2 2 9 1 12 6 C15 1 22 2 22 8 C22 14 14 19 12 21 Z",
        comment: "M21 11 C21 17 16 20 11 20 L4 23 L5 18 C-2 12 2 2 11 2 C17 2 21 6 21 11 Z",
        share: "M3 12 L21 3 L15 21 L11 14 L3 12 Z M11 14 L21 3",
        shareArrow: "M14 3 L23 11 L14 19 L14 14 C8 14 4 16 1 21 C1 12 5 7 14 7 Z",
        bookmark: "M5 3 L19 3 L19 22 L12 17 L5 22 Z",
        more: "M4 12 L5 12 M11 12 L12 12 M18 12 L19 12",
        home: "M2 11 L12 2 L22 11 M5 9 L5 22 L10 22 L10 15 L14 15 L14 22 L19 22 L19 9",
        search: "M18 10 A8 8 0 1 1 2 10 A8 8 0 1 1 18 10 M16 16 L23 23",
        plus: "M12 5 L12 19 M5 12 L19 12",
        user: "M17 7 A5 5 0 1 1 7 7 A5 5 0 1 1 17 7 M3 23 C3 12 21 12 21 23",
        friends: "M14 7 A4 4 0 1 1 6 7 A4 4 0 1 1 14 7 M2 22 C2 12 18 12 18 22 M18 3 C24 3 24 11 18 11 M19 14 C23 15 24 18 24 22",
        music: "M9 17 L9 5 L21 2 L21 14 M9 17 A4 3 0 1 1 1 17 A4 3 0 1 1 9 17 M21 14 A4 3 0 1 1 13 14 A4 3 0 1 1 21 14",
        camera: "M2 7 L7 7 L9 3 L15 3 L17 7 L22 7 L22 21 L2 21 Z M17 13 A5 5 0 1 1 7 13 A5 5 0 1 1 17 13",
        reel: "M3 3 L21 3 L21 21 L3 21 Z M3 8 L21 8 M7 3 L11 8 M15 3 L19 8 M10 11 L16 14 L10 18 Z"
    })
    Item {
        width: 24; height: 24
        scale: root.width / 24
        transformOrigin: Item.TopLeft
        Shape {
            anchors.fill: parent
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                strokeColor: "white"
                strokeWidth: 1.8
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                fillColor: "transparent"
                PathSvg { path: root.paths[root.name] || root.paths.more }
            }
        }
    }
}
