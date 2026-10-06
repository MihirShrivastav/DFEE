import QtQuick
import DFEE

// Segmented control: inset track, active segment raised. Only for switching views
// of the same thing; one per region. Controlled: clicking only emits activated(index).
Rectangle {
    id: seg
    property var model: []
    property int currentIndex: 0
    signal activated(int index)
    implicitWidth: row.implicitWidth + 4
    implicitHeight: Theme.segmentHeight + 4
    radius: Theme.radiusTrack
    color: Theme.inset
    border.width: 1
    border.color: Theme.hairline
    Row {
        id: row
        anchors.centerIn: parent
        spacing: 2
        Repeater {
            model: seg.model
            delegate: Rectangle {
                id: s
                readonly property bool on: index === seg.currentIndex
                width: label.implicitWidth + 24
                height: Theme.segmentHeight
                radius: Theme.radiusSegment
                color: on ? Theme.selected : (hover.hovered ? Theme.rowHover : "transparent")
                Text {
                    id: label
                    anchors.centerIn: parent
                    text: modelData
                    font.pixelSize: Theme.fontLabel
                    font.weight: Font.Medium
                    color: s.on ? Theme.text : Theme.textCaption
                }
                HoverHandler { id: hover }
                MouseArea {
                    anchors.fill: parent
                    onClicked: {
                        // A slider keeps the arrows only until something else is clicked.
                        const w = seg.Window.window;
                        if (w && w.activeFocusItem && w.activeFocusItem.keepsArrowKeys === true && w.returnFocus) w.returnFocus();
                        seg.activated(index)   // owner sets currentIndex; keeps its binding
                    }
                }
            }
        }
    }
}
