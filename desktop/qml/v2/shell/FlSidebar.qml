import QtQuick
import QtQuick.Controls
import DFEE

// Sidebar: Library (pinned folders) and History. Group headers 11/600 caption.
Rectangle {
    id: side
    color: Theme.panel
    signal addFolderRequested()
    readonly property bool showLibrary: !engine.lightroomRoundTrip

    Column {
        anchors.fill: parent
        anchors.topMargin: 14
        anchors.leftMargin: 10
        anchors.rightMargin: 10
        spacing: 22

        Column {
            width: parent.width
            spacing: 2
            visible: side.showLibrary
            Item {
                width: parent.width
                height: 24
                Text { anchors.left: parent.left; anchors.leftMargin: 8; anchors.verticalCenter: parent.verticalCenter; text: "Library"; color: Theme.textCaption; font.pixelSize: Theme.fontCaption; font.weight: Font.DemiBold }
                FlIconButton { anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; width: 22; height: 22; iconName: "plus"; tip: "Add folder"; onClicked: side.addFolderRequested() }
            }
            ListView {
                id: folderList
                objectName: "folderList"
                width: parent.width
                height: Math.min(contentHeight, side.height * 0.45)
                clip: true
                model: library.folders
                spacing: 2
                delegate: Rectangle {
                    readonly property bool selected: library.currentFolder === modelData.path
                    width: folderList.width
                    height: 28
                    radius: Theme.radiusControl
                    color: selected ? Theme.rowSelected : (hov.hovered ? Theme.rowHover : "transparent")
                    HoverHandler { id: hov }
                    MouseArea { anchors.fill: parent; onClicked: library.selectFolder(modelData.path) }
                    Row {
                        anchors.left: parent.left
                        anchors.leftMargin: 8
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 8
                        FlIcon { name: "folder-simple"; size: 15; color: parent.parent.selected ? Theme.accent : Theme.textCaption; anchors.verticalCenter: parent.verticalCenter }
                        Text { text: modelData.name; color: parent.parent.selected ? Theme.text : Theme.textBody; font.pixelSize: Theme.fontBody; elide: Text.ElideMiddle; width: folderList.width - 64; anchors.verticalCenter: parent.verticalCenter }
                    }
                    FlIconButton {
                        visible: hov.hovered
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        width: 22; height: 22
                        iconName: "x"
                        tip: "Remove from library"
                        onClicked: library.removeFolder(modelData.path)
                    }
                }
            }
            Text {
                visible: library.folders.length === 0
                leftPadding: 8
                width: parent.width
                text: "Add a folder of photos to browse it."
                color: Theme.textTertiary
                font.pixelSize: Theme.fontCaption
                wrapMode: Text.WordWrap
            }
        }

        Column {
            width: parent.width
            spacing: 2
            Item {
                width: parent.width
                height: 24
                Text { anchors.left: parent.left; anchors.leftMargin: 8; anchors.verticalCenter: parent.verticalCenter; text: "History"; color: Theme.textCaption; font.pixelSize: Theme.fontCaption; font.weight: Font.DemiBold }
            }
            ListView {
                id: historyList
                objectName: "historyList"
                width: parent.width
                height: side.height - y - 40
                clip: true
                model: engine.history
                spacing: 2
                delegate: Rectangle {
                    readonly property bool current: index === engine.historyIndex
                    readonly property bool future: index < engine.historyIndex
                    width: historyList.width
                    height: 26
                    radius: Theme.radiusControl
                    color: current ? Theme.rowSelected : (hh.hovered ? Theme.rowHover : "transparent")
                    HoverHandler { id: hh }
                    MouseArea { anchors.fill: parent; onClicked: engine.jumpToHistory(index) }
                    Row {
                        anchors.left: parent.left
                        anchors.leftMargin: 8
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 8
                        Rectangle { width: 5; height: 5; radius: 3; anchors.verticalCenter: parent.verticalCenter; color: parent.parent.current ? Theme.accent : "#2effffff" }
                        Text { text: modelData.label; font.pixelSize: Theme.fontLabel; color: parent.parent.current ? Theme.text : (parent.parent.future ? Theme.textTertiary : Theme.textSecondary) }
                    }
                }
            }
        }
    }
    Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: Theme.hairline }
}
