import QtQuick
import QtQuick.Controls
import DFEE

// Picker popover: rows of {id, name, detail, group, art}. A row whose group differs
// from the row above gets a group caption. Row objectName "<rowPrefix><id>". Picking
// emits picked(id) and closes; closing hands focus back to the window.
Popup {
    id: pop
    property var rows: []
    property string currentId: ""
    property string rowPrefix: "row_"
    signal picked(string id)
    width: 268
    height: Math.min(list.contentHeight + 12, 420)
    padding: 6
    margins: 8                     // stay inside the window
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    function indexOf(id) {
        for (let i = 0; i < rows.length; ++i) if (rows[i].id === id) return i;
        return -1;
    }
    onOpened: list.positionViewAtIndex(Math.max(0, indexOf(currentId)), ListView.Contain)
    onClosed: Qt.callLater(function() {
        const w = pop.parent ? pop.parent.Window.window : null;
        if (w && w.returnFocus) w.returnFocus();
    })
    background: Rectangle {
        color: Theme.popover
        radius: Theme.radiusPopover
        border.width: 1
        border.color: Theme.hairline
    }
    contentItem: ListView {
        id: list
        clip: true
        model: pop.rows
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
        delegate: Column {
            id: rowItem
            width: list.width
            readonly property bool hasArt: (modelData.art || "").length > 0
            readonly property bool firstOfGroup: index === 0 || pop.rows[index - 1].group !== modelData.group
            readonly property bool current: modelData.id === pop.currentId
            Text {
                visible: rowItem.firstOfGroup && (modelData.group || "").length > 0
                height: visible ? 26 : 0
                leftPadding: 8
                bottomPadding: 4
                verticalAlignment: Text.AlignBottom
                text: modelData.group || ""
                color: Theme.textCaption
                font.pixelSize: Theme.fontCaption
                font.weight: Font.DemiBold
            }
            Rectangle {
                objectName: pop.rowPrefix + modelData.id
                width: parent.width
                height: rowItem.hasArt ? 40 : 28
                radius: Theme.radiusControl
                color: rowHover.hovered ? Theme.rowSelected : "transparent"
                HoverHandler { id: rowHover }
                MouseArea {
                    anchors.fill: parent
                    onClicked: { pop.picked(modelData.id); pop.close(); }
                }
                Image {
                    id: art
                    visible: rowItem.hasArt
                    x: 8
                    anchors.verticalCenter: parent.verticalCenter
                    width: 28
                    height: 28
                    source: modelData.art || ""
                    sourceSize: Qt.size(56, 56)
                    fillMode: Image.PreserveAspectCrop
                }
                Column {
                    anchors.left: rowItem.hasArt ? art.right : parent.left
                    anchors.leftMargin: rowItem.hasArt ? 10 : 8
                    anchors.right: check.left
                    anchors.rightMargin: 6
                    anchors.verticalCenter: parent.verticalCenter
                    Text {
                        width: parent.width
                        text: modelData.name
                        elide: Text.ElideRight
                        color: rowItem.current ? Theme.text : Theme.textBody
                        font.pixelSize: Theme.fontBody
                        font.weight: rowItem.current ? Font.DemiBold : Font.Normal
                    }
                    Text {
                        visible: (modelData.detail || "").length > 0
                        width: parent.width
                        text: modelData.detail || ""
                        elide: Text.ElideRight
                        color: Theme.textTertiary
                        font.pixelSize: Theme.fontCaption
                    }
                }
                FlIcon {
                    id: check
                    visible: rowItem.current
                    name: "check"
                    size: 14
                    color: Theme.accent
                    anchors.right: parent.right
                    anchors.rightMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                }
            }
        }
    }
}
