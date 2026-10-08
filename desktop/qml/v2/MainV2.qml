import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtCore
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
    // True while a text field (FlTextField) has focus: bare-key shortcuts stay off.
    readonly property bool textEntry: activeFocusItem !== null && activeFocusItem.flTextEntry === true

    // Panel layout (VS Code style), remembered between launches.
    Settings {
        id: layout
        category: "layout"
        location: uiSettingsLocation
        property bool sidebarOpen: true
        property bool trayOpen: true
        property bool inspectorOpen: true
    }
    // What Tab hid, so a second Tab brings back exactly that (null: nothing hidden).
    property var hiddenPanels: null
    function toggleAllPanels() {
        if (hiddenPanels === null) {
            const open = { sidebar: layout.sidebarOpen, tray: layout.trayOpen, inspector: layout.inspectorOpen };
            if (!open.sidebar && !open.tray && !open.inspector) {
                layout.sidebarOpen = true;          // nothing open: show everything
                layout.trayOpen = true;
                layout.inspectorOpen = true;
                return;
            }
            hiddenPanels = open;
            layout.sidebarOpen = false;
            layout.trayOpen = false;
            layout.inspectorOpen = false;
        } else {
            layout.sidebarOpen = hiddenPanels.sidebar;
            layout.trayOpen = hiddenPanels.tray;
            layout.inspectorOpen = hiddenPanels.inspector;
            hiddenPanels = null;
        }
    }
    // A single toggle after Tab starts from the current layout.
    function togglePanel(name) {
        hiddenPanels = null;
        if (name === "sidebar") layout.sidebarOpen = !layout.sidebarOpen;
        else if (name === "tray") layout.trayOpen = !layout.trayOpen;
        else layout.inspectorOpen = !layout.inspectorOpen;
    }
    property int compareMode: 0          // 0 Edited, 1 Split, 2 Side by side
    property bool peekBefore: false

    function cycleStock(dir) {
        const m = engine.stockModel;
        if (!m || m.length === 0) return;
        let cur = 0;
        for (let i = 0; i < m.length; ++i) if (m[i].id === engine.stock) { cur = i; break; }
        engine.stock = m[(cur + dir + m.length) % m.length].id;
    }

    readonly property bool arrowKeysFree: !textEntry
        && !(activeFocusItem && activeFocusItem.keepsArrowKeys === true)
    // Every v2 photo switch goes through here: leaving crop mode applies the crop,
    // so the outgoing photo is saved with it (not with crop mode's full frame).
    function openPhoto(url) {
        if (canvas.cropMode) canvas.applyCropMode();
        engine.openFile(url);
    }
    // Lightroom Edit-In saves straight back; standalone shows the export sheet.
    function exportRequested() {
        if (canvas.cropMode) canvas.applyCropMode();   // export what the user framed
        if (engine.lightroomRoundTrip) engine.exportImage();
        else exportSheet.open();
    }
    function navigatePhoto(step) {
        const files = library.files;
        if (files.length === 0) return;
        const cur = engine.currentFile.toLowerCase();
        let at = -1;
        for (let i = 0; i < files.length; ++i) {
            if (files[i].path.toLowerCase() === cur) { at = i; break; }
        }
        const next = at < 0 ? (step > 0 ? 0 : files.length - 1) : at + step;
        if (next < 0 || next >= files.length || next === at) return;
        root.openPhoto(Qt.resolvedUrl("file:///" + files[next].path));
    }

    Shortcut { sequences: ["Ctrl+S", "Ctrl+Return", "Ctrl+Enter"]; enabled: engine.hasImage && !engine.exporting; onActivated: root.exportRequested() }
    Shortcut { sequence: "Ctrl+O"; enabled: !engine.lightroomRoundTrip && !root.textEntry; onActivated: openDialog.open() }
    Shortcut { sequence: "\\"; enabled: engine.hasBefore && !root.textEntry; onActivated: { root.compareMode = 0; root.peekBefore = !root.peekBefore; } }
    Shortcut { sequence: "B"; enabled: engine.hasBefore && !root.textEntry; onActivated: { root.peekBefore = false; root.compareMode = (root.compareMode + 1) % 3; } }
    Shortcut { sequence: "Ctrl+Z"; enabled: engine.canUndo && !root.textEntry; onActivated: engine.undo() }
    Shortcut { sequences: ["Ctrl+Y", "Ctrl+Shift+Z"]; enabled: engine.canRedo && !root.textEntry; onActivated: engine.redo() }
    Shortcut { sequence: "Ctrl+Shift+C"; enabled: engine.hasImage && !root.textEntry; onActivated: engine.copyLook() }
    Shortcut { sequence: "Ctrl+Shift+V"; enabled: engine.hasImage && engine.hasCopiedLook && !root.textEntry; onActivated: engine.pasteLook() }
    Shortcut { sequence: "["; enabled: engine.hasImage && !root.textEntry; onActivated: root.cycleStock(-1) }
    Shortcut { sequence: "]"; enabled: engine.hasImage && !root.textEntry; onActivated: root.cycleStock(1) }
    Shortcut { sequences: ["C", "R"]; enabled: engine.hasImage && !engine.lightroomRoundTrip && !root.textEntry; onActivated: canvas.cropMode ? canvas.applyCropMode() : canvas.enterCropMode() }
    Shortcut { sequences: ["Return", "Enter"]; enabled: canvas.cropMode && !root.textEntry; onActivated: canvas.applyCropMode() }
    Shortcut { sequence: "Esc"; enabled: canvas.cropMode; onActivated: canvas.cancelCropMode() }
    Shortcut { sequence: "Esc"; enabled: engine.peekStock.length > 0 && !canvas.cropMode; onActivated: engine.endPeek() }
    Shortcut { sequence: "Left"; enabled: !engine.lightroomRoundTrip && root.arrowKeysFree; onActivated: root.navigatePhoto(-1) }
    Shortcut { sequence: "Right"; enabled: !engine.lightroomRoundTrip && root.arrowKeysFree; onActivated: root.navigatePhoto(1) }
    Shortcut { sequence: "Ctrl+Left"; enabled: !engine.lightroomRoundTrip && !root.textEntry; onActivated: root.navigatePhoto(-1) }
    Shortcut { sequence: "Ctrl+Right"; enabled: !engine.lightroomRoundTrip && !root.textEntry; onActivated: root.navigatePhoto(1) }
    Shortcut { sequences: ["?", "F1"]; enabled: !root.textEntry; onActivated: shortcutsSheet.open() }
    Shortcut { sequence: "Ctrl+Shift+R"; enabled: engine.hasImage && !root.textEntry; onActivated: resetSheet.open() }
    Shortcut { sequence: "Ctrl+B"; enabled: !engine.lightroomRoundTrip; onActivated: root.togglePanel("sidebar") }
    Shortcut { sequence: "Ctrl+J"; onActivated: root.togglePanel("tray") }
    Shortcut { sequence: "Ctrl+Alt+B"; onActivated: root.togglePanel("inspector") }
    // Tab hides every panel for a clean look at the photo; Tab again restores them.
    Shortcut { sequence: "Tab"; enabled: !root.textEntry; onActivated: root.toggleAllPanels() }
    Shortcut { sequences: ["F", "Ctrl+F"]; enabled: !root.textEntry; onActivated: tray.showFilmsSearch() }
    Shortcut { sequence: "Ctrl+0"; enabled: engine.hasImage && !root.textEntry; onActivated: canvas.resetZoom() }
    Shortcut { sequence: "Ctrl+1"; enabled: engine.hasImage && !root.textEntry; onActivated: canvas.zoomCentered(2.0) }

    FileDialog {
        id: openDialog
        title: "Open image"
        nameFilters: ["Supported images (*.tif *.tiff *.arw *.nef *.cr2 *.cr3 *.raf *.rw2 *.dng *.orf *.pef *.srw *.3fr)",
                      "RAW photos (*.arw *.nef *.cr2 *.cr3 *.raf *.rw2 *.dng *.orf *.pef *.srw *.3fr)",
                      "TIFF images (*.tif *.tiff)"]
        onAccepted: root.openPhoto(selectedFile)
        onRejected: root.returnFocus()
    }
    FolderDialog {
        id: folderDialog
        title: "Add folder to library"
        onAccepted: library.addFolder(selectedFolder)
        onRejected: root.returnFocus()
    }
    FlLookDialog { id: lookDialog }
    FlExportSheet { id: exportSheet }
    FlShortcutsSheet { id: shortcutsSheet }
    FlResetSheet { id: resetSheet }

    // While exporting: a quiet veil with progress (ported from v1).
    Rectangle {
        objectName: "exportOverlay"
        anchors.fill: parent
        z: 10
        visible: engine.exporting
        color: "#d9101114"
        MouseArea { anchors.fill: parent }        // swallow clicks while saving
        Column {
            anchors.centerIn: parent
            spacing: 12
            BusyIndicator { anchors.horizontalCenter: parent.horizontalCenter; running: engine.exporting; width: 36; height: 36 }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: engine.lightroomRoundTrip ? "Saving to Lightroom…" : "Exporting…"
                color: Theme.text
                font.pixelSize: Theme.fontTitle
                font.weight: Font.DemiBold
            }
        }
    }

    FlToolbar {
        id: toolbar
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        compareMode: root.compareMode
        onCompareChosen: (m) => { root.peekBefore = false; root.compareMode = m; }
        onSidebarToggled: root.togglePanel("sidebar")
        onTrayToggled: root.togglePanel("tray")
        onInspectorToggled: root.togglePanel("inspector")
        onAllPanelsToggled: root.toggleAllPanels()
        sidebarOpen: layout.sidebarOpen
        trayOpen: layout.trayOpen
        inspectorOpen: layout.inspectorOpen
        onOpenRequested: openDialog.open()
        onAddFolderRequested: folderDialog.open()
        onCycleStockRequested: (dir) => root.cycleStock(dir)
        onExportRequested: root.exportRequested()
        onResetAllRequested: resetSheet.open()
        onHelpRequested: shortcutsSheet.open()
        zoom: canvas.zoom
        cropActive: canvas.cropMode
        onFitRequested: canvas.resetZoom()
        onActualSizeRequested: canvas.zoomCentered(2.0)
        onCropRequested: canvas.cropMode ? canvas.applyCropMode() : canvas.enterCropMode()
    }

    FlSidebar {
        id: sidebar
        objectName: "sidebar"
        anchors.left: parent.left
        anchors.top: toolbar.bottom
        anchors.bottom: parent.bottom
        onAddFolderRequested: folderDialog.open()
        width: layout.sidebarOpen && !engine.lightroomRoundTrip ? Theme.sidebarWidth : 0
        visible: width > 0
        Behavior on width { NumberAnimation { duration: Theme.motionNormal; easing.type: Easing.OutCubic } }
    }

    // Inspector (plan 1B) and tray (plan 1C) join the canvas later.
    FlCanvas {
        id: canvas
        anchors.left: sidebar.right
        anchors.right: inspector.left
        anchors.top: toolbar.bottom
        anchors.bottom: tray.top
        compareMode: root.compareMode
        peekBefore: root.peekBefore
        onCompareModeRequested: (m) => { root.peekBefore = false; root.compareMode = m; }
        onBackgroundPressed: root.returnFocus()
    }
    FlTray {
        id: tray
        anchors.left: sidebar.right
        anchors.right: inspector.left
        anchors.bottom: parent.bottom
        height: layout.trayOpen ? Theme.trayHeight : 0
        visible: height > 0
        clip: true
        Behavior on height { NumberAnimation { duration: Theme.motionNormal; easing.type: Easing.OutCubic } }
        onOpenRequested: (u) => root.openPhoto(u)
        onSaveLookRequested: lookDialog.openNew()
        onRenameLookRequested: (id, name, group) => lookDialog.openRename(id, name, group)
    }
    FlInspector {
        id: inspector
        anchors.right: parent.right
        anchors.top: toolbar.bottom
        anchors.bottom: parent.bottom
        width: layout.inspectorOpen ? Theme.inspectorWidth : 0
        visible: width > 0
        clip: true
        Behavior on width { NumberAnimation { duration: Theme.motionNormal; easing.type: Easing.OutCubic } }
        enabled: !canvas.cropMode
        opacity: canvas.cropMode ? 0.4 : 1.0
        Behavior on opacity { NumberAnimation { duration: Theme.motionNormal } }
    }

    // Test hook: `--min-size` starts at the minimum window size (Review Focus 3).
    Component.onCompleted: {
        if (Qt.application.arguments.indexOf("--min-size") >= 0) {
            width = minimumWidth;
            height = minimumHeight;
        }
    }
}
