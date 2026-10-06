import QtQuick
import QtQuick.Controls
import DFEE

// Icon-only toolbar button with a tooltip that names its shortcut.
Button {
    id: b
    property string iconName: ""   // Button.icon is FINAL
    property string tip: ""
    property bool active: false
    implicitWidth: 30
    implicitHeight: Theme.controlHeight
    focusPolicy: Qt.NoFocus
    Accessible.name: tip
    contentItem: Item {
        FlIcon {
            anchors.centerIn: parent
            name: b.iconName
            size: 16
            color: b.active ? Theme.accent : (b.hovered ? Theme.text : Theme.textSecondary)
        }
    }
    background: Rectangle {
        radius: Theme.radiusControl
        color: b.active ? "#2e0a84ff" : (b.hovered ? Theme.rowSelected : "transparent")
    }
    FlTip { visible: b.hovered && b.tip.length > 0; text: b.tip }
}
