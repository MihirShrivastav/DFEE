import QtQuick
import QtQuick.Controls
import DFEE

// Text button. kind: "accent" (the one commit action per view), "quiet" (control
// fill), "text" (no fill). Height 26, radius 6.
Button {
    id: b
    property string kind: "quiet"
    implicitHeight: Theme.controlHeight
    leftPadding: kind === "text" ? 6 : 12
    rightPadding: leftPadding
    focusPolicy: Qt.NoFocus
    font.pixelSize: Theme.fontLabel
    font.weight: kind === "accent" ? Font.DemiBold : Font.Medium
    opacity: enabled ? 1.0 : 0.4
    contentItem: Text {
        text: b.text
        font: b.font
        color: b.kind === "accent" ? Theme.textOnAccent
             : (b.kind === "text" ? (b.hovered ? Theme.text : Theme.textSecondary) : Theme.text)
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }
    background: Rectangle {
        radius: Theme.radiusControl
        color: b.kind === "accent" ? (b.hovered ? Theme.accentHover : Theme.accent)
             : (b.kind === "quiet" ? (b.hovered ? Theme.controlHover : Theme.control) : "transparent")
    }
}
