import QtQuick
import QtQuick.Controls
import DFEE

// Looks: "Save current look" first, then each saved look (its film's box art, name,
// group). A click applies the look; the "…" menu renames or deletes it.
ListView {
    id: strip
    property string query: ""
    signal saveRequested()
    signal renameRequested(string id, string name, string group)
    orientation: ListView.Horizontal
    spacing: 14
    leftMargin: 16
    rightMargin: 16
    // Horizontal views ignore topMargin, so tiles start `tileTop` down to leave room for the selection ring.
    readonly property int tileTop: 8
    clip: true
    boundsBehavior: Flickable.StopAtBounds
    ScrollBar.horizontal: FlScrollBar {}
    model: {
        const q = query.trim().toLowerCase();
        const rows = [{ kind: "save" }];
        const p = engine.presets;
        for (let i = 0; i < p.length; ++i)
            if (q.length === 0 || p[i].name.toLowerCase().indexOf(q) >= 0 || (p[i].group || "").toLowerCase().indexOf(q) >= 0)
                rows.push(p[i]);
        return rows;
    }
    delegate: Item {
        id: tile
        readonly property bool isSave: modelData.kind === "save"
        objectName: isSave ? "lookSave" : "lookTile_" + modelData.id
        width: 124
        height: strip.tileTop + 118
        Rectangle {
            id: art
            y: strip.tileTop
            width: 124
            height: 82
            radius: Theme.radiusTile
            color: tile.isSave ? "transparent" : Theme.inset
            border.width: 1
            border.color: tile.isSave ? "#2effffff" : (tileHover.hovered ? "#40ffffff" : "transparent")
            clip: true
            opacity: tile.isSave && !engine.hasImage ? 0.4 : 1.0
            Image {
                anchors.fill: parent
                visible: !tile.isSave && (modelData.stock || "none") !== "none"
                source: visible ? "qrc:/boxart/" + modelData.stock + ".svg" : ""
                sourceSize: Qt.size(248, 164)
                fillMode: Image.PreserveAspectCrop
                asynchronous: true
            }
            Column {
                anchors.centerIn: parent
                spacing: 6
                visible: tile.isSave || (modelData.stock || "none") === "none"
                FlIcon {
                    anchors.horizontalCenter: parent.horizontalCenter
                    name: tile.isSave ? "plus" : "image-square"
                    size: 16
                    color: Theme.textSecondary
                }
                Text {
                    visible: tile.isSave
                    text: "Save current look"
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontLabel
                }
            }
        }
        Column {
            visible: !tile.isSave
            anchors.top: art.bottom
            anchors.topMargin: 7
            width: parent.width
            spacing: 1
            Text {
                width: parent.width
                text: modelData.name || ""
                elide: Text.ElideRight
                color: Theme.text
                font.pixelSize: Theme.fontLabel
                font.weight: Font.Medium
            }
            Text {
                width: parent.width
                text: modelData.group || ""
                elide: Text.ElideRight
                color: Theme.textTertiary
                font.pixelSize: Theme.fontCaption
            }
        }
        HoverHandler { id: tileHover }
        MouseArea {
            anchors.fill: art
            enabled: !tile.isSave || engine.hasImage
            onClicked: tile.isSave ? strip.saveRequested() : engine.applyPreset(modelData.id)
        }
        FlIconButton {
            id: moreButton
            objectName: tile.isSave ? "" : "lookMore_" + modelData.id
            visible: !tile.isSave
            opacity: tileHover.hovered || menu.visible ? 1.0 : 0.0
            x: art.width - width - 4
            y: art.y + 4
            width: 24
            height: 22
            iconName: "dots-three"
            tip: "More"
            onClicked: menu.popup(moreButton, 0, moreButton.height + 4)
        }
        FlMenu {
            id: menu
            Action {
                objectName: "lookRename"
                text: "Rename…"
                onTriggered: strip.renameRequested(modelData.id, modelData.name, modelData.group || "")
            }
            Action {
                objectName: "lookDelete"
                text: "Delete"
                onTriggered: engine.deletePreset(modelData.id)
            }
        }
    }
}
