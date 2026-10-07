import QtQuick
import QtQuick.Controls
import DFEE

// Toolbar toggle for a panel (VS Code style): a small window glyph whose panel
// side is filled while the panel is open, outlined while it is closed. Controlled:
// a click only emits clicked(); the owner flips `checked`.
Button {
    id: b
    property string iconName: ""          // outlined glyph, e.g. "sidebar-simple"
    property string checkedIconName: ""   // filled glyph, e.g. "sidebar-simple-fill"
    property bool flipGlyph: false        // the right panel is the left glyph mirrored (Control.mirrored is taken)
    property string tip: ""
    // `checked` is Button's own (bound by the owner; not checkable, so a click never flips it).
    checkable: false
    implicitWidth: 28
    implicitHeight: Theme.controlHeight
    focusPolicy: Qt.NoFocus
    Accessible.name: tip
    contentItem: Item {
        FlIcon {
            anchors.centerIn: parent
            name: b.checked ? b.checkedIconName : b.iconName
            size: 16
            mirror: b.flipGlyph
            color: b.hovered ? Theme.text : (b.checked ? Theme.textSecondary : Theme.textTertiary)
        }
    }
    background: Rectangle {
        radius: Theme.radiusControl
        color: b.hovered ? Theme.rowSelected : "transparent"
    }
    FlTip { visible: b.hovered && b.tip.length > 0; text: b.tip }
}
