import QtQuick
import DFEE

// Edit history rows: the current step highlighted, steps after it (redo-able)
// dimmed; a click jumps there. Used by the sidebar and the Lightroom popover.
ListView {
    id: list
    clip: true
    model: engine.history
    spacing: 2
    delegate: Rectangle {
        id: rowItem
        readonly property bool current: index === engine.historyIndex
        readonly property bool future: index < engine.historyIndex
        width: list.width
        height: 26
        radius: Theme.radiusControl
        color: current ? Theme.rowSelected : (hover.hovered ? Theme.rowHover : "transparent")
        HoverHandler { id: hover }
        MouseArea { anchors.fill: parent; onClicked: engine.jumpToHistory(index) }
        Rectangle {
            id: dot
            x: 8
            anchors.verticalCenter: parent.verticalCenter
            width: 5; height: 5; radius: 3
            color: rowItem.current ? Theme.accent : "#2effffff"
        }
        Text {
            anchors.left: dot.right
            anchors.leftMargin: 8
            anchors.right: parent.right
            anchors.rightMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            text: modelData.label
            elide: Text.ElideRight
            font.pixelSize: Theme.fontLabel
            color: rowItem.current ? Theme.text : (rowItem.future ? Theme.textTertiary : Theme.textSecondary)
        }
    }
}
