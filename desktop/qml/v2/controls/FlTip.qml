import QtQuick
import QtQuick.Controls
import DFEE

ToolTip {
    id: tip
    delay: 450
    padding: 8
    width: Math.min(implicitWidth, 260)
    y: parent ? parent.height + 6 : 0
    contentItem: Text {
        text: tip.text
        color: Theme.text
        font.pixelSize: Theme.fontCaption
        wrapMode: Text.WordWrap
        width: tip.availableWidth
    }
    background: Rectangle { color: Theme.popover; radius: Theme.radiusCard; border.width: 1; border.color: Theme.hairline }
}
