import QtQuick
import QtQuick.Effects
import DFEE

// Phosphor SVG from qrc:/icons/<name>.svg, recolored to `color`.
Item {
    id: icon
    property string name: ""
    property color color: Theme.textSecondary
    property int size: 16
    property bool mirror: false          // horizontal mirror (a right-hand panel glyph)
    implicitWidth: size
    implicitHeight: size
    Image {
        id: src
        anchors.fill: parent
        source: icon.name.length ? ("qrc:/icons/" + icon.name + ".svg") : ""
        sourceSize.width: icon.size * 2
        sourceSize.height: icon.size * 2
        fillMode: Image.PreserveAspectFit
        mirror: icon.mirror
        smooth: true
        visible: false
    }
    MultiEffect {
        anchors.fill: src
        source: src
        colorization: 1.0
        colorizationColor: icon.color
    }
}
