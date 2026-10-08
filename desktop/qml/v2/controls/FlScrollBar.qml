import QtQuick
import QtQuick.Controls
import DFEE

// Quiet scroll bar: a thin thumb a shade lighter than the panel, no track. It widens
// a little and brightens under the pointer or while dragged so it stays easy to grab.
ScrollBar {
    id: bar
    policy: ScrollBar.AsNeeded
    padding: 2
    readonly property bool engaged: bar.hovered || bar.pressed
    contentItem: Rectangle {
        implicitWidth: bar.engaged ? 6 : 4
        implicitHeight: bar.engaged ? 6 : 4
        radius: 3
        color: bar.engaged ? Theme.scrollThumbHover : Theme.scrollThumb
        visible: bar.size < 1.0
        Behavior on implicitWidth { NumberAnimation { duration: Theme.motionFast } }
        Behavior on implicitHeight { NumberAnimation { duration: Theme.motionFast } }
    }
    background: Item {}
}
