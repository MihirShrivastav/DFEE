import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import DFEE

ApplicationWindow {
    id: root
    objectName: "v2Root"
    width: 1280
    height: 800
    minimumWidth: 960
    minimumHeight: 620
    visible: true
    title: "Film Lab"
    color: Theme.window
    font.family: Theme.fontFamily
    font.pixelSize: Theme.fontBody

    // Focus model (see desktop/DESIGN.md): popups hand focus back to a plain Item,
    // never to contentItem (a focus scope that returns it to its last child).
    function returnFocus() { keySink.forceActiveFocus(); }
    Item { id: keySink }
    // True while a text field has focus: bare-key shortcuts stay off.
    property bool textEntry: false

    property bool sidebarOpen: true
    property int compareMode: 0          // 0 Edited, 1 Split, 2 Side by side
    property bool peekBefore: false

    function cycleStock(dir) {
        const m = engine.stockModel;
        if (!m || m.length === 0) return;
        let cur = 0;
        for (let i = 0; i < m.length; ++i) if (m[i].id === engine.stock) { cur = i; break; }
        engine.stock = m[(cur + dir + m.length) % m.length].id;
    }

    FileDialog {
        id: openDialog
        title: "Open image"
        nameFilters: ["Supported images (*.tif *.tiff *.arw *.nef *.cr2 *.cr3 *.raf *.rw2 *.dng *.orf *.pef *.srw *.3fr)",
                      "RAW photos (*.arw *.nef *.cr2 *.cr3 *.raf *.rw2 *.dng *.orf *.pef *.srw *.3fr)",
                      "TIFF images (*.tif *.tiff)"]
        onAccepted: engine.openFile(selectedFile)
        onRejected: root.returnFocus()
    }
    FolderDialog {
        id: folderDialog
        title: "Add folder to library"
        onAccepted: library.addFolder(selectedFolder)
        onRejected: root.returnFocus()
    }

    FlToolbar {
        id: toolbar
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        compareMode: root.compareMode
        onCompareChosen: (m) => { root.peekBefore = false; root.compareMode = m; }
        onSidebarToggled: root.sidebarOpen = !root.sidebarOpen
        onOpenRequested: openDialog.open()
        onAddFolderRequested: folderDialog.open()
        onCycleStockRequested: (dir) => root.cycleStock(dir)
        zoom: canvas.zoom
        onFitRequested: canvas.resetZoom()
        onActualSizeRequested: canvas.setZoom(2.0, canvas.width / 2, canvas.height / 2)
        onCropRequested: canvas.cropMode ? canvas.applyCropMode() : canvas.enterCropMode()
    }

    FlSidebar {
        id: sidebar
        anchors.left: parent.left
        anchors.top: toolbar.bottom
        anchors.bottom: parent.bottom
        onAddFolderRequested: folderDialog.open()
        width: root.sidebarOpen ? Theme.sidebarWidth : 0
        visible: width > 0
        Behavior on width { NumberAnimation { duration: Theme.motionNormal; easing.type: Easing.OutCubic } }
    }

    // Inspector (plan 1B) and tray (plan 1C) join the canvas later.
    FlCanvas {
        id: canvas
        anchors.left: sidebar.right
        anchors.right: inspectorSlot.left
        anchors.top: toolbar.bottom
        anchors.bottom: parent.bottom
        compareMode: root.compareMode
        peekBefore: root.peekBefore
        onCompareModeRequested: (m) => { root.peekBefore = false; root.compareMode = m; }
    }
    Rectangle {
        id: inspectorSlot
        anchors.right: parent.right
        anchors.top: toolbar.bottom
        anchors.bottom: parent.bottom
        width: Theme.inspectorWidth
        color: Theme.panel
        Rectangle { anchors.left: parent.left; width: 1; height: parent.height; color: Theme.hairline }
    }

    // Test hook: `--min-size` starts at the minimum window size (Review Focus 3).
    Component.onCompleted: {
        if (Qt.application.arguments.indexOf("--min-size") >= 0) {
            width = minimumWidth;
            height = minimumHeight;
        }
    }
}
