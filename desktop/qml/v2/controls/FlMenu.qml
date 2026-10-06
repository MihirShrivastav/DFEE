import QtQuick
import QtQuick.Controls
import DFEE

// Menu styled per DESIGN.md v2; items show their shortcut on the right. Closing
// hands focus back to the window (MainV2.returnFocus).
Menu {
    id: menu
    padding: 6
    onClosed: Qt.callLater(function() {
        const w = menu.parent ? menu.parent.Window.window : null;
        if (w && w.returnFocus) w.returnFocus();
    })
    background: Rectangle {
        implicitWidth: 240
        color: Theme.popover
        radius: Theme.radiusPopover
        border.width: 1
        border.color: Theme.hairline
    }
    delegate: MenuItem {
        id: item
        implicitHeight: 26
        contentItem: Item {
            Text {
                anchors.left: parent.left
                anchors.leftMargin: 4
                anchors.verticalCenter: parent.verticalCenter
                text: item.text
                font.pixelSize: Theme.fontLabel
                color: item.highlighted ? Theme.textOnAccent : (item.enabled ? Theme.text : Theme.textTertiary)
            }
            Text {
                anchors.right: parent.right
                anchors.rightMargin: 4
                anchors.verticalCenter: parent.verticalCenter
                // Display only: menu Actions carry `property string keys`; the live
                // bindings are MainV2's Shortcut items (no duplicate, ambiguous bindings).
                text: item.action && item.action.keys ? item.action.keys : ""
                font.pixelSize: Theme.fontCaption
                color: item.highlighted ? "#ccffffff" : Theme.textTertiary
            }
        }
        background: Rectangle {
            radius: 5
            color: item.highlighted ? Theme.accent : "transparent"
        }
    }
}
