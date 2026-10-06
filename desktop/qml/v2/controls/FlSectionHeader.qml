import QtQuick
import DFEE

// 40px section header: title 13/600, edited dot, one-word summary when collapsed,
// Reset on hover when edited, chevron (down = open, right = closed). Controlled.
Item {
    id: h
    property string title: ""
    property bool open: true
    property bool edited: false
    property string summary: ""
    property bool showHairline: true
    signal toggled()
    signal resetRequested()
    width: parent ? parent.width : 300
    height: Theme.sectionRow
    HoverHandler { id: hover }
    // Controlled: the owner flips `open` in response to toggled().
    MouseArea {
        anchors.fill: parent
        onClicked: {
            // A slider keeps the arrows only until something else is clicked.
            const w = h.Window.window;
            if (w && w.activeFocusItem && w.activeFocusItem.keepsArrowKeys === true && w.returnFocus) w.returnFocus();
            h.toggled();
        }
    }
    Row {
        anchors.left: parent.left
        anchors.leftMargin: 16
        anchors.verticalCenter: parent.verticalCenter
        spacing: 8
        Text { text: h.title; color: Theme.text; font.pixelSize: Theme.fontTitle; font.weight: Font.DemiBold }
        Rectangle { visible: h.edited; width: 5; height: 5; radius: 3; color: Theme.accent; anchors.verticalCenter: parent.verticalCenter }
        Text {
            visible: !h.open && h.summary.length > 0
            text: h.summary
            color: Theme.textTertiary
            font.pixelSize: Theme.fontCaption
            anchors.verticalCenter: parent.verticalCenter
        }
    }
    Row {
        anchors.right: parent.right
        anchors.rightMargin: 16
        anchors.verticalCenter: parent.verticalCenter
        spacing: 12
        Text {
            visible: h.edited && hover.hovered
            text: "Reset"
            color: resetHover.hovered ? Theme.text : Theme.textSecondary
            font.pixelSize: Theme.fontCaption
            anchors.verticalCenter: parent.verticalCenter
            HoverHandler { id: resetHover }
            MouseArea { anchors.fill: parent; onClicked: h.resetRequested() }
        }
        FlIcon {
            name: "caret-down"
            size: 12
            color: Theme.textCaption
            rotation: h.open ? 0 : -90
            anchors.verticalCenter: parent.verticalCenter
            Behavior on rotation { NumberAnimation { duration: Theme.motionFast; easing.type: Easing.OutCubic } }
        }
    }
    FlHairline { anchors.bottom: parent.bottom; visible: h.showHairline }
}
