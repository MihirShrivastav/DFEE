import QtQuick
import QtQuick.Controls
import DFEE

// Modal sheet: popover surface, radius 10, dimmed backdrop, 13/600 title. Esc and a
// click outside close it; closing hands focus back to the window.
Popup {
    id: sheet
    property string title: ""
    default property alias content: body.data
    modal: true
    dim: true
    focus: true
    anchors.centerIn: Overlay.overlay
    width: 420
    padding: 20
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    onClosed: Qt.callLater(function() {
        const w = sheet.parent ? sheet.parent.Window.window : null;
        if (w && w.returnFocus) w.returnFocus();
    })
    Overlay.modal: Rectangle { color: "#80000000" }
    background: Rectangle {
        color: Theme.popover
        radius: Theme.radiusPopover
        border.width: 1
        border.color: Theme.hairline
    }
    contentItem: Column {
        spacing: 16
        Text {
            width: parent.width
            text: sheet.title
            color: Theme.text
            font.pixelSize: Theme.fontTitle
            font.weight: Font.DemiBold
        }
        Column {
            id: body
            width: parent.width
            spacing: 14
        }
    }
}
