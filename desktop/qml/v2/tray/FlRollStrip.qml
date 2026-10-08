import QtQuick
import QtQuick.Controls
import DFEE

// Roll: the current library folder as thumbnails; the open photo is ringed and
// edited photos carry a badge. A click asks the window to open the photo.
ListView {
    id: strip
    property string query: ""
    signal openRequested(url fileUrl)
    readonly property string summary: {
        const folder = library.currentFolder;
        if (!folder) return "";
        const name = folder.split("/").pop();
        const n = library.files.length;
        return name + " · " + n + (n === 1 ? " photo" : " photos");
    }
    orientation: ListView.Horizontal
    spacing: 8
    leftMargin: 16
    rightMargin: 16
    // Horizontal views ignore topMargin, so tiles start `tileTop` down to leave room for the selection ring.
    readonly property int tileTop: 8
    clip: true
    boundsBehavior: Flickable.StopAtBounds
    ScrollBar.horizontal: FlScrollBar {}
    model: {
        const q = query.trim().toLowerCase();
        return library.files.filter(f => q.length === 0 || f.name.toLowerCase().indexOf(q) >= 0);
    }
    delegate: Item {
        id: tile
        objectName: "rollTile_" + modelData.name.replace(/\./g, "_")
        readonly property bool current: modelData.path.toLowerCase() === engine.currentFile.toLowerCase()
        // recordCount's NOTIFY (EditStore::changed) fires on every save, so this re-reads.
        readonly property bool edited: { editStore.recordCount; return editStore.isEdited(modelData.path); }
        width: 128
        height: strip.tileTop + 96 + 22
        Rectangle {
            x: -4; y: strip.tileTop - 4
            width: 136; height: 104
            radius: 9
            color: "transparent"
            border.width: 2
            border.color: Theme.accent
            visible: tile.current
        }
        Rectangle {
            id: thumb
            y: strip.tileTop
            width: 128
            height: 96
            radius: 5
            color: Theme.inset
            clip: true
            Image {
                anchors.fill: parent
                asynchronous: true
                cache: true
                fillMode: Image.PreserveAspectCrop
                sourceSize: Qt.size(256, 192)
                source: "image://thumb/" + encodeURIComponent(modelData.path)
            }
        }
        Rectangle {
            visible: tile.edited
            anchors.right: thumb.right
            anchors.bottom: thumb.bottom
            anchors.margins: 5
            width: 16; height: 16; radius: 8
            color: "#99000000"
            FlIcon { anchors.centerIn: parent; name: "pencil-simple"; size: 10; color: Theme.text }
        }
        Text {
            objectName: "rollName_" + modelData.name.replace(/\./g, "_")
            anchors.top: thumb.bottom
            anchors.topMargin: 5
            width: parent.width
            text: modelData.name
            elide: Text.ElideMiddle                 // keep the number and extension
            color: tile.current ? Theme.text : Theme.textTertiary
            font.pixelSize: Theme.fontCaption
            font.features: { "tnum": 1 }
        }
        HoverHandler { id: tileHover }
        FlTip { visible: tileHover.hovered; text: modelData.name }
        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            onClicked: (mouse) => {
                if (mouse.button === Qt.RightButton) {
                    rollMenu.photoPath = modelData.path;
                    rollMenu.photoEdited = tile.edited;
                    rollMenu.popup(tile, mouse.x, mouse.y);
                } else {
                    strip.openRequested(Qt.resolvedUrl("file:///" + modelData.path.replace(/\\/g, "/")));
                }
            }
        }
    }
    // Right-click on a thumbnail: copy its look, paste the copied look onto it (without
    // opening it), reset its edits, or show it in Explorer.
    FlMenu {
        id: rollMenu
        objectName: "rollMenu"
        property string photoPath: ""
        property bool photoEdited: false
        Action {
            objectName: "rollCopyLook"
            text: "Copy Look"
            enabled: rollMenu.photoEdited
            onTriggered: engine.copyLookFrom(rollMenu.photoPath)
        }
        Action {
            objectName: "rollPasteLook"
            text: engine.hasCopiedLook ? "Paste Look (" + engine.copiedLookLabel + ")" : "Paste Look"
            enabled: engine.hasCopiedLook
            onTriggered: engine.pasteLookTo(rollMenu.photoPath)
        }
        FlMenuSeparator {}
        Action {
            objectName: "rollResetEdits"
            text: "Reset Edits"
            enabled: rollMenu.photoEdited
            onTriggered: engine.resetEditsOf(rollMenu.photoPath)
        }
        Action {
            objectName: "rollShowInExplorer"
            text: "Show in Explorer"
            onTriggered: engine.showInExplorer(rollMenu.photoPath)
        }
    }
    Text {
        anchors.centerIn: parent
        visible: library.files.length === 0
        text: "Add a folder to the library to see its photos here."
        color: Theme.textTertiary
        font.pixelSize: Theme.fontLabel
    }
}
