import QtQuick
import DFEE

// Confirms a full reset (film and every adjustment). Undo brings it back.
FlSheet {
    id: sheet
    objectName: "resetSheet"
    title: "Reset all edits?"
    width: 360
    Text {
        width: parent.width
        text: "Clears the film and every adjustment on this photo. You can undo it."
        wrapMode: Text.WordWrap
        color: Theme.textSecondary
        font.pixelSize: Theme.fontLabel
    }
    Row {
        anchors.right: parent.right
        spacing: 8
        FlButton { kind: "quiet"; text: "Cancel"; onClicked: sheet.close() }
        FlButton {
            objectName: "resetConfirm"
            kind: "accent"
            text: "Reset"
            onClicked: { engine.resetAllEdits(); sheet.close(); }
        }
    }
}
