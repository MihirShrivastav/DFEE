import QtQuick
import QtQuick.Controls
import DFEE

// Films: one tile per stock (box art, name, ISO) in the chosen group, or every
// stock matching the search. A click applies the stock. Box-art tiles until Phase 2
// renders the photo through each stock.
ListView {
    id: strip
    property string query: ""
    property string group: ""
    readonly property var groups: {
        const g = [];
        const m = engine.stockModel;
        for (let i = 0; i < m.length; ++i)
            if (m[i].id !== "none" && g.indexOf(m[i].groupLabel) < 0) g.push(m[i].groupLabel);
        return g;
    }
    readonly property string activeGroup: group.length > 0 ? group : (groups.length > 0 ? groups[0] : "")
    // Ask the engine for exactly the tiles on show; none while the strip is hidden.
    readonly property var wantedIds: (model || []).map(r => r.id)
    function requestTiles() { engine.requestLookTiles(visible ? wantedIds : []); }
    onWantedIdsChanged: requestTiles()
    onVisibleChanged: { requestTiles(); if (!visible) { hoverId = ""; engine.endPeek(); } }
    Component.onCompleted: requestTiles()
    // Hover ~120 ms to preview a film on the canvas; leaving the tiles restores.
    property string hoverId: ""
    Timer {
        id: peekTimer
        interval: 120
        onTriggered: strip.hoverId.length > 0 ? engine.beginPeek(strip.hoverId) : engine.endPeek()
    }
    onHoverIdChanged: peekTimer.restart()
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
        const rows = [{ id: "none", name: "No film", iso: 0 }];
        const m = engine.stockModel;
        for (let i = 0; i < m.length; ++i) {
            const s = m[i];
            if (s.id === "none") continue;
            if (q.length > 0 ? s.name.toLowerCase().indexOf(q) < 0 : s.groupLabel !== strip.activeGroup) continue;
            rows.push(s);
        }
        return rows;
    }
    delegate: Item {
        id: tile
        objectName: "filmTile_" + modelData.id
        readonly property bool current: engine.stock === modelData.id
        readonly property int tileEpoch: engine.lookTiles[modelData.id] || 0
        readonly property bool live: tileEpoch > 0
        readonly property bool fresh: live && tileEpoch === engine.lookEpoch
        width: 124
        height: strip.tileTop + 118
        Rectangle {                                   // selection ring: 2px gap + 2px accent
            x: -4; y: strip.tileTop - 4
            width: 132; height: 90
            radius: 9
            color: "transparent"
            border.width: 2
            border.color: Theme.accent
            visible: tile.current
        }
        Rectangle {
            id: art
            y: strip.tileTop
            width: 124
            height: 82
            radius: Theme.radiusTile
            color: Theme.inset
            clip: true
            border.width: tileHover.hovered && !tile.current ? 1 : 0
            border.color: "#40ffffff"
            Image {
                objectName: "filmTileImage_" + modelData.id
                anchors.fill: parent
                visible: tile.live
                source: tile.live ? "image://look/" + modelData.id + "?e=" + tile.tileEpoch : ""
                fillMode: Image.PreserveAspectCrop
                cache: false
                asynchronous: true
                retainWhileLoading: true            // a refreshing tile never blinks blank
            }
            Image {
                anchors.fill: parent
                visible: modelData.id !== "none" && !tile.live
                source: modelData.id !== "none" ? "qrc:/boxart/" + modelData.id + ".svg" : ""
                sourceSize: Qt.size(248, 164)
                fillMode: Image.PreserveAspectCrop
                asynchronous: true
            }
            Text {
                anchors.centerIn: parent
                visible: modelData.id === "none" && !tile.live
                text: "No film"
                color: Theme.textCaption
                font.pixelSize: Theme.fontLabel
            }
        }
        Image {                                   // box-art badge on a live tile
            visible: tile.live && modelData.id !== "none"
            x: 6
            y: art.y + art.height - height - 6
            width: 18
            height: 18
            source: visible ? "qrc:/boxart/" + modelData.id + ".svg" : ""
            sourceSize: Qt.size(36, 36)
            fillMode: Image.PreserveAspectCrop
        }
        Column {
            anchors.top: art.bottom
            anchors.topMargin: 7
            width: parent.width
            spacing: 1
            Text {
                width: parent.width
                text: modelData.name
                elide: Text.ElideRight
                color: tile.current ? Theme.text : Theme.textBody
                font.pixelSize: Theme.fontLabel
                font.weight: Font.Medium
            }
            Text {
                visible: modelData.iso > 0
                text: "ISO " + modelData.iso
                color: Theme.textTertiary
                font.pixelSize: Theme.fontCaption
                font.features: { "tnum": 1 }
            }
        }
        HoverHandler {
            id: tileHover
            onHoveredChanged: {
                if (hovered) strip.hoverId = modelData.id;
                else if (strip.hoverId === modelData.id) strip.hoverId = "";
            }
        }
        MouseArea { anchors.fill: parent; onClicked: engine.stock = modelData.id }
    }
}
