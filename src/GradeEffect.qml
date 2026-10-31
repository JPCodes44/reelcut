import QtQuick

// The color wheels as a layer effect: src/shaders/grade.frag with a grade's
// params (ColorGrade::Grade::toMap()), the same numbers the export's LUT uses.
ShaderEffect {
    property var grade
    readonly property var p: grade && grade.params ? grade.params : null
    function v3(list, d) { return list ? Qt.vector3d(list[0], list[1], list[2]) : Qt.vector3d(d, d, d) }
    property vector3d wb: v3(p ? p.wb : null, 1)
    property vector3d off: v3(p ? p.off : null, 0)
    property vector3d lift: v3(p ? p.lift : null, 0)
    property vector3d gain: v3(p ? p.gain : null, 1)
    property vector3d gexp: v3(p ? p.gexp : null, 1)
    property real contrast: p ? p.contrast : 1
    property real pivot: p ? p.pivot : 0.435
    property real saturation: p ? p.saturation : 1
    property real hue: p ? p.hue : 0
    fragmentShader: "qrc:/shaders/grade.frag.qsb"
}
