import QtQuick
import DFEE

// One inspector section: 40px header (title, edited dot, Reset on hover, collapse)
// over a padded body, hairline under the whole section. Controlled: `open` comes
// from the owner (persisted settings). `group` names the engine control group that
// drives Reset and the edited dot.
Column {
    id: section
    property string title: ""
    property string group: ""
    property bool open: true
    property string summary: ""
    default property alias content: body.data
    signal toggled()
    readonly property bool edited: group.length > 0 && engine.editedGroups[group] === true
    width: parent ? parent.width : Theme.inspectorWidth
    FlSectionHeader {
        objectName: section.objectName.length > 0 ? section.objectName + "Header" : ""
        width: parent.width
        title: section.title
        open: section.open
        edited: section.edited
        summary: section.summary
        showHairline: false
        onToggled: section.toggled()
        onResetRequested: engine.resetControlGroup(section.group)
    }
    Item {
        width: parent.width
        height: section.open ? body.implicitHeight + 28 : 0
        visible: section.open
        Column {
            id: body
            x: 16
            y: 14
            width: parent.width - 32
            spacing: 14
        }
    }
    FlHairline {}
}
