import QtQuick
import DFEE

// Switch row: label left, 26×16 switch right (accent when on). Controlled: a click
// only emits toggled(); the owner binds `checked` to its model.
Item {
    id: sw
    property string label: ""
    property bool checked: false
    property string tip: ""
    signal toggled()
    width: parent ? parent.width : 240
    height: 24
    opacity: enabled ? 1.0 : 0.4
    Text {
        anchors.left: parent.left
        anchors.right: track.left
        anchors.rightMargin: 8
        anchors.verticalCenter: parent.verticalCenter
        text: sw.label
        color: Theme.textSecondary
        font.pixelSize: Theme.fontLabel
        elide: Text.ElideRight
    }
    Rectangle {
        id: track
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        width: 26
        height: 16
        radius: 8
        color: sw.checked ? Theme.accent : Theme.control
        Behavior on color { ColorAnimation { duration: Theme.motionFast } }
        Rectangle {
            width: 12
            height: 12
            radius: 6
            y: 2
            x: sw.checked ? parent.width - width - 2 : 2
            color: Theme.knob
            Behavior on x { NumberAnimation { duration: Theme.motionFast; easing.type: Easing.OutCubic } }
        }
    }
    HoverHandler { id: hover }
    MouseArea {
        anchors.fill: parent
        enabled: sw.enabled
        onClicked: {
            // A slider keeps the arrows only until something else is clicked.
            const w = sw.Window.window;
            if (w && w.activeFocusItem && w.activeFocusItem.keepsArrowKeys === true && w.returnFocus) w.returnFocus();
            sw.toggled();
        }
    }
    FlTip { visible: hover.hovered && sw.tip.length > 0; text: sw.tip }
}
