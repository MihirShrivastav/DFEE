import QtQuick
import QtQuick.Controls
import DFEE

// A grading zone's tint as a swatch; a click opens that zone's wheel in a popover.
// A second view of the Fine-tune color grading wheels (same controls).
Item {
    id: sw
    property string zone: ""
    property string label: ""
    readonly property real hueVal: Number(engine.filmControls["cg_" + zone + "_hue"])
    readonly property real satVal: Number(engine.filmControls["cg_" + zone + "_sat"])
    width: 120
    height: 28
    Rectangle {
        id: dot
        anchors.verticalCenter: parent.verticalCenter
        width: 20
        height: 20
        radius: 10
        color: sw.satVal > 0
            ? Qt.hsla((((sw.hueVal % 360) + 360) % 360) / 360, Math.min(sw.satVal / 100, 1.0), 0.55, 1.0)
            : Theme.control
        border.width: 1
        border.color: Theme.hairline
    }
    Text {
        anchors.left: dot.right
        anchors.leftMargin: 8
        anchors.verticalCenter: parent.verticalCenter
        text: sw.label
        color: Theme.textSecondary
        font.pixelSize: Theme.fontLabel
    }
    MouseArea { anchors.fill: parent; onClicked: pop.open() }
    Popup {
        id: pop
        objectName: "swatchPopup_" + sw.zone
        y: sw.height + 6
        padding: 14
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        onClosed: Qt.callLater(function() {
            const w = sw.Window.window;
            if (w && w.returnFocus) w.returnFocus();
        })
        background: Rectangle {
            color: Theme.popover
            radius: Theme.radiusPopover
            border.width: 1
            border.color: Theme.hairline
        }
        contentItem: FlColorWheel {
            objectName: "swatchWheel_" + sw.zone
            zone: sw.zone
            label: sw.label
            diameter: 120
        }
    }
}
