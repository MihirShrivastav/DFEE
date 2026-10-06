import QtQuick
import DFEE

// A key in the shortcuts sheet.
Rectangle {
    property string label: ""
    implicitWidth: Math.max(22, keyText.implicitWidth + 12)
    implicitHeight: 22
    radius: 5
    color: Theme.control
    border.width: 1
    border.color: Theme.hairline
    Text {
        id: keyText
        anchors.centerIn: parent
        text: parent.label
        color: Theme.text
        font.pixelSize: Theme.fontCaption
        font.weight: Font.Medium
    }
}
