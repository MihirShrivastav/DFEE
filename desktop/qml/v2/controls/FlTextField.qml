import QtQuick
import QtQuick.Controls
import DFEE

// Text field per DESIGN.md: inset well, 26 tall, radius 6, accent focus ring. While
// it has focus MainV2 turns bare-key shortcuts off (flTextEntry). Esc hands focus
// back to the window unless a sheet owns Esc (escReturnsFocus: false).
TextField {
    id: f
    readonly property bool flTextEntry: true
    property string iconName: ""
    property bool escReturnsFocus: true
    implicitHeight: Theme.controlHeight
    leftPadding: iconName.length > 0 ? 26 : 8
    rightPadding: 8
    color: Theme.text
    placeholderTextColor: Theme.textTertiary
    selectionColor: Theme.accent
    selectedTextColor: Theme.textOnAccent
    font.pixelSize: Theme.fontLabel
    verticalAlignment: TextInput.AlignVCenter
    selectByMouse: true
    background: Rectangle {
        radius: Theme.radiusControl
        color: Theme.inset
        border.width: 1
        border.color: f.activeFocus ? "#730a84ff" : Theme.hairline
    }
    FlIcon {
        visible: f.iconName.length > 0
        name: f.iconName
        size: 13
        color: Theme.textTertiary
        x: 8
        anchors.verticalCenter: parent.verticalCenter
    }
    Keys.onEscapePressed: (e) => {
        if (!f.escReturnsFocus) { e.accepted = false; return; }
        const w = f.Window.window;
        if (w && w.returnFocus) w.returnFocus();
        e.accepted = true;
    }
}
