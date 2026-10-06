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
    topMargin: 8
    clip: true
    boundsBehavior: Flickable.StopAtBounds
    ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded }
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
        height: 96
        Rectangle {
            x: -4; y: -4
            width: 136; height: 104
            radius: 9
            color: "transparent"
            border.width: 2
            border.color: Theme.accent
            visible: tile.current
        }
        Rectangle {
            anchors.fill: parent
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
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: 5
            width: 16; height: 16; radius: 8
            color: "#99000000"
            FlIcon { anchors.centerIn: parent; name: "pencil-simple"; size: 10; color: Theme.text }
        }
        HoverHandler { id: tileHover }
        FlTip { visible: tileHover.hovered; text: modelData.name }
        MouseArea {
            anchors.fill: parent
            onClicked: strip.openRequested(Qt.resolvedUrl("file:///" + modelData.path.replace(/\\/g, "/")))
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
