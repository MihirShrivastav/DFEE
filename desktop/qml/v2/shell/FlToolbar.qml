import QtQuick
import QtQuick.Controls
import DFEE

// Unified toolbar under the (DWM-colored) native title bar: menus + sidebar toggle,
// file title over a quiet camera line, compare control, zoom, Crop/Export/help.
Rectangle {
    id: bar
    property int compareMode: 0
    property real zoom: 1.0
    property bool cropActive: false
    signal compareChosen(int mode)
    signal cropRequested()
    signal helpRequested()
    signal sidebarToggled()
    signal fitRequested()
    signal actualSizeRequested()
    signal openRequested()
    signal addFolderRequested()
    signal cycleStockRequested(int dir)
    signal exportRequested()
    signal resetAllRequested()
    height: Theme.toolbarHeight
    color: Theme.toolbar

    function fileName(path) { return path ? path.split("/").pop() : ""; }
    function cameraLine(info) {
        if (!info) return "";
        const parts = [];
        if (info.camera) parts.push(info.camera);
        if (info.lens) parts.push(info.lens);
        if (info.aperture) parts.push(info.aperture);
        if (info.shutter) parts.push(info.shutter + " s");
        if (info.iso) parts.push("ISO " + info.iso);
        return parts.join(" · ");
    }

    // Left zone: menus (width of the sidebar)
    Row {
        id: left
        anchors.left: parent.left
        anchors.leftMargin: 10
        anchors.verticalCenter: parent.verticalCenter
        width: Theme.sidebarWidth - 10
        spacing: 2
        FlIconButton {
            visible: !engine.lightroomRoundTrip
            iconName: "sidebar-simple"
            tip: "Show or hide sidebar"
            onClicked: bar.sidebarToggled()
        }
        FlIconButton {
            id: historyButton
            objectName: "historyButton"
            visible: engine.lightroomRoundTrip
            iconName: "clock-counter-clockwise"
            tip: "History"
            active: historyPopover.visible
            onClicked: historyPopover.open()
            Popup {
                id: historyPopover
                objectName: "historyPopover"
                y: historyButton.height + 8
                width: 260
                height: Math.min(popoverList.contentHeight + 16, 360)
                padding: 8
                focus: true
                closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
                onClosed: Qt.callLater(function() {
                    const w = bar.Window.window;
                    if (w && w.returnFocus) w.returnFocus();
                })
                background: Rectangle {
                    color: Theme.popover
                    radius: Theme.radiusPopover
                    border.width: 1
                    border.color: Theme.hairline
                }
                contentItem: FlHistoryList { id: popoverList; objectName: "historyPopoverList" }
            }
        }
        Repeater {
            model: [
                { title: "File", menu: fileMenu, name: "menuFile" },
                { title: "Edit", menu: editMenu, name: "menuEdit" },
                { title: "Photo", menu: photoMenu, name: "menuPhoto" },
                { title: "View", menu: viewMenu, name: "menuView" },
                { title: "Help", menu: helpMenu, name: "menuHelp" }
            ]
            delegate: FlButton {
                id: menuButton
                objectName: modelData.name
                kind: "text"
                text: modelData.title
                onClicked: modelData.menu.popup(menuButton, 0, menuButton.height + 4)
            }
        }
    }

    // Title block
    Column {
        anchors.left: parent.left
        anchors.leftMargin: Theme.sidebarWidth + 16
        anchors.right: compare.left
        anchors.rightMargin: 16
        anchors.verticalCenter: parent.verticalCenter
        spacing: 1
        Text {
            objectName: "toolbarTitle"
            width: parent.width
            text: bar.fileName(engine.currentFile)
            color: Theme.text
            font.pixelSize: Theme.fontTitle
            font.weight: Font.DemiBold
            elide: Text.ElideMiddle
        }
        Text {
            objectName: "toolbarCamera"
            width: parent.width
            text: bar.cameraLine(engine.imageInfo)
            color: Theme.textCaption
            font.pixelSize: Theme.fontCaption
            font.features: { "tnum": 1 }
            elide: Text.ElideRight
            visible: text.length > 0
        }
    }

    FlSegmented {
        id: compare
        objectName: "compareSegmented"
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.verticalCenter: parent.verticalCenter
        visible: engine.hasImage && engine.hasBefore
        model: ["Edited", "Split", "Side by side"]
        currentIndex: bar.compareMode
        onActivated: (i) => bar.compareChosen(i)
    }

    Row {
        anchors.right: parent.right
        anchors.rightMargin: 12
        anchors.verticalCenter: parent.verticalCenter
        spacing: 6
        FlButton {
            id: zoomButton
            kind: "text"
            text: bar.zoom > 1.001 ? Math.round(bar.zoom * 100) + "%" : "Fit"
            enabled: engine.hasImage
            onClicked: zoomMenu.popup(zoomButton, 0, zoomButton.height + 4)
        }
        FlIconButton { objectName: "cropButton"; iconName: "crop"; tip: "Crop (C)"; active: bar.cropActive; enabled: engine.hasImage && !engine.lightroomRoundTrip; onClicked: bar.cropRequested() }
        FlButton {
            objectName: "exportButton"
            kind: "accent"
            text: engine.lightroomRoundTrip ? "Save & Return" : "Export…"
            enabled: engine.hasImage && !engine.exporting
            onClicked: bar.exportRequested()
        }
        FlIconButton { iconName: "question"; tip: "Keyboard shortcuts (?)"; onClicked: bar.helpRequested() }
    }

    FlHairline { anchors.bottom: parent.bottom }

    // `keys` is display text (FlMenu shows it); the live bindings are MainV2's
    // Shortcut items (Task 5), so no key is bound twice.
    FlMenu {
        id: fileMenu
        objectName: "fileMenu"
        Action { text: "Open…"; property string keys: "Ctrl+O"; enabled: !engine.lightroomRoundTrip; onTriggered: bar.openRequested() }
        Action { text: "Add Folder…"; enabled: !engine.lightroomRoundTrip; onTriggered: bar.addFolderRequested() }
        MenuSeparator {}
        Action { text: engine.lightroomRoundTrip ? "Save & Return" : "Export"; property string keys: "Ctrl+S"; enabled: engine.hasImage && !engine.exporting; onTriggered: bar.exportRequested() }
    }
    FlMenu {
        id: editMenu
        Action { text: "Undo"; property string keys: "Ctrl+Z"; enabled: engine.canUndo; onTriggered: engine.undo() }
        Action { text: "Redo"; property string keys: "Ctrl+Y"; enabled: engine.canRedo; onTriggered: engine.redo() }
        MenuSeparator {}
        Action { text: "Reset All Edits…"; property string keys: "Ctrl+Shift+R"; enabled: engine.hasImage; onTriggered: bar.resetAllRequested() }
    }
    FlMenu {
        id: photoMenu
        Action { text: "Previous Film"; property string keys: "["; enabled: engine.hasImage; onTriggered: bar.cycleStockRequested(-1) }
        Action { text: "Next Film"; property string keys: "]"; enabled: engine.hasImage; onTriggered: bar.cycleStockRequested(1) }
        MenuSeparator {}
        Action { text: "Crop"; property string keys: "C"; enabled: engine.hasImage && !engine.lightroomRoundTrip; onTriggered: bar.cropRequested() }
    }
    FlMenu {
        id: viewMenu
        Action { text: "Edited"; enabled: engine.hasImage; onTriggered: bar.compareChosen(0) }
        Action { text: "Split"; enabled: engine.hasImage && engine.hasBefore; onTriggered: bar.compareChosen(1) }
        Action { text: "Side by Side"; enabled: engine.hasImage && engine.hasBefore; onTriggered: bar.compareChosen(2) }
        MenuSeparator {}
        Action { objectName: "zoomFitAction"; text: "Fit"; property string keys: "Ctrl+0"; enabled: engine.hasImage; onTriggered: bar.fitRequested() }
        Action { objectName: "zoomActualAction"; text: "Zoom to 200%"; property string keys: "Ctrl+1"; enabled: engine.hasImage; onTriggered: bar.actualSizeRequested() }
        MenuSeparator {}
        Action { text: "Show or Hide Sidebar"; enabled: !engine.lightroomRoundTrip; onTriggered: bar.sidebarToggled() }
    }
    FlMenu {
        id: helpMenu
        Action { text: "Keyboard Shortcuts"; property string keys: "?"; onTriggered: bar.helpRequested() }
    }
    FlMenu {
        id: zoomMenu
        Action { text: "Fit"; property string keys: "Ctrl+0"; onTriggered: bar.fitRequested() }
        Action { text: "Zoom to 200%"; property string keys: "Ctrl+1"; onTriggered: bar.actualSizeRequested() }
    }
}
