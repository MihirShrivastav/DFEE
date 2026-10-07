import QtQuick
import QtQuick.Controls
import QtCore
import DFEE

// Bottom tray (184): Roll | Films | Looks with a filter field. Lightroom Edit-In
// shows Films | Looks only. The tab persists (QtCore Settings, category "tray").
Rectangle {
    id: tray
    objectName: "tray"
    color: Theme.window
    implicitHeight: Theme.trayHeight
    signal openRequested(url fileUrl)
    signal saveLookRequested()
    signal renameLookRequested(string id, string name, string group)
    readonly property var tabNames: engine.lightroomRoundTrip ? ["films", "looks"] : ["roll", "films", "looks"]
    readonly property var tabLabels: engine.lightroomRoundTrip ? ["Films", "Looks"] : ["Roll", "Films", "Looks"]
    readonly property string activeTab: tabNames.indexOf(prefs.tab) >= 0 ? prefs.tab : "films"
    function showFilmsSearch() {
        prefs.tab = "films";
        search.forceActiveFocus();
        search.selectAll();
    }

    Settings {
        id: prefs
        category: "tray"
        location: uiSettingsLocation
        property string tab: "films"
    }
    FlHairline { anchors.top: parent.top }

    Item {
        id: header
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 44
        FlSegmented {
            id: tabs
            objectName: "traySegmented"
            x: 16
            anchors.verticalCenter: parent.verticalCenter
            model: tray.tabLabels
            currentIndex: tray.tabNames.indexOf(tray.activeTab)
            onActivated: (i) => prefs.tab = tray.tabNames[i]
        }
        Row {
            anchors.left: tabs.right
            anchors.leftMargin: 18
            anchors.verticalCenter: parent.verticalCenter
            spacing: 16
            visible: tray.activeTab === "films" && search.text.trim().length === 0
            Repeater {
                model: films.groups
                delegate: Text {
                    objectName: "filmGroup_" + index
                    text: modelData
                    color: modelData === films.activeGroup ? Theme.text : Theme.textCaption
                    font.pixelSize: Theme.fontLabel
                    font.weight: modelData === films.activeGroup ? Font.Medium : Font.Normal
                    MouseArea { anchors.fill: parent; anchors.margins: -4; onClicked: films.group = modelData }
                }
            }
        }
        Text {
            anchors.left: tabs.right
            anchors.leftMargin: 18
            anchors.verticalCenter: parent.verticalCenter
            visible: tray.activeTab === "roll"
            text: roll.summary
            color: Theme.textCaption
            font.pixelSize: Theme.fontLabel
            font.features: { "tnum": 1 }
        }
        Text {
            anchors.left: tabs.right
            anchors.leftMargin: 18
            anchors.verticalCenter: parent.verticalCenter
            visible: tray.activeTab === "looks"
            text: "Your saved looks"
            color: Theme.textCaption
            font.pixelSize: Theme.fontLabel
        }
        FlTextField {
            id: search
            objectName: "traySearch"
            anchors.right: parent.right
            anchors.rightMargin: 16
            anchors.verticalCenter: parent.verticalCenter
            width: 190
            placeholderText: "Search"
            iconName: "magnifying-glass"
        }
    }

    FlRollStrip {
        id: roll
        objectName: "rollStrip"
        anchors.top: header.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        visible: tray.activeTab === "roll"
        query: search.text
        onOpenRequested: (u) => tray.openRequested(u)
    }
    FlFilmsStrip {
        id: films
        objectName: "filmsStrip"
        anchors.top: header.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        visible: tray.activeTab === "films"
        query: search.text
    }
    FlLooksStrip {
        id: looks
        objectName: "looksStrip"
        anchors.top: header.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        visible: tray.activeTab === "looks"
        query: search.text
        onSaveRequested: tray.saveLookRequested()
        onRenameRequested: (id, name, group) => tray.renameLookRequested(id, name, group)
    }
}
