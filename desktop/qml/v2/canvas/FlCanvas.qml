import QtQuick
import DFEE

// Photo canvas: Edited / Split / Side by side, zoom + pan, crop overlay, empty state,
// status line. Logic ported unchanged from v1 Main.qml (previewCanvas).
Rectangle {
    id: canvas
    objectName: "photoCanvas"
    color: Theme.canvas
    property int compareMode: 0          // bound from MainV2; never assigned here
    property bool peekBefore: false
    property alias zoom: imageArea.zoom
    signal compareModeRequested(int mode)
    signal backgroundPressed()

    // ── Crop state ───────────────────────────────────────────────────
    property bool cropMode: false
    property real cropAspect: 0
    property real cropX: 0
    property real cropY: 0
    property real cropW: 1
    property real cropH: 1
    // The crop the photo had when crop mode started (Esc restores it).
    property real cropStartX: 0
    property real cropStartY: 0
    property real cropStartW: 1
    property real cropStartH: 1

    // Crop mode belongs to one photo; never carry it (or its rectangle) to the next.
    Connections {
        target: engine
        function onCurrentFileChanged() { canvas.cropMode = false; }
    }

    function resetZoom() { imageArea.resetZoom(); }
    function setZoom(z, fx, fy) { imageArea.setZoom(z, fx, fy); }
    // Zoom about the photo's centre (imageArea coordinates, not the canvas's).
    function zoomCentered(z) { imageArea.setZoom(z, imageArea.width / 2, imageArea.height / 2); }
    function imageAspect() {
        return (afterImg.paintedHeight > 0) ? (afterImg.paintedWidth / afterImg.paintedHeight) : 1.0;
    }
    function enterCropMode() {
        imageArea.resetZoom();
        cropX = engine.filmControls.crop_x;
        cropY = engine.filmControls.crop_y;
        cropW = engine.filmControls.crop_w;
        cropH = engine.filmControls.crop_h;
        cropStartX = cropX; cropStartY = cropY; cropStartW = cropW; cropStartH = cropH;
        compareModeRequested(0);
        cropMode = true;
        engine.setCrop(0, 0, 1, 1);
    }
    function applyCropMode() {
        cropMode = false;
        engine.setCrop(cropX, cropY, cropW, cropH);
    }
    function cancelCropMode() {
        cropMode = false;
        engine.setCrop(cropStartX, cropStartY, cropStartW, cropStartH);
    }
    function resetCropMode() {
        engine.resetGeometry();
        cropAspect = 0;
        cropX = 0; cropY = 0; cropW = 1; cropH = 1;
    }
    function selectAspect(r) {
        if (r < 0) {
            cropAspect = 0;
            cropX = 0; cropY = 0; cropW = 1; cropH = 1;
            if (!cropMode) engine.setCrop(0, 0, 1, 1);
            return;
        }
        if (!cropMode) enterCropMode();
        if (r === 0) { cropAspect = 0; return; }
        const A = imageAspect();
        // Presets follow the photo's orientation: 3:2 on a portrait photo crops 2:3.
        if ((A < 1) !== (r < 1) && Math.abs(r - 1) > 0.0001) r = 1 / r;
        cropAspect = r;
        const R = r / A;
        let wN, hN;
        if (R >= 1) { wN = 1; hN = 1 / R; } else { hN = 1; wN = R; }
        cropW = wN; cropH = hN;
        cropX = (1 - wN) / 2; cropY = (1 - hN) / 2;
    }
    function updateCrop(grab, nx, ny) {
        const minS = 0.05;
        nx = Math.max(0, Math.min(1, nx));
        ny = Math.max(0, Math.min(1, ny));
        let l = cropX, t = cropY, r = cropX + cropW, b = cropY + cropH;
        if (grab.indexOf("l") >= 0) l = Math.min(nx, r - minS);
        if (grab.indexOf("r") >= 0) r = Math.max(nx, l + minS);
        if (grab.indexOf("t") >= 0) t = Math.min(ny, b - minS);
        if (grab.indexOf("b") >= 0) b = Math.max(ny, t + minS);
        if (cropAspect > 0 && grab.length === 2) {
            const ratioN = cropAspect / imageAspect();
            const ax = (grab.indexOf("l") >= 0) ? r : l;
            const ay = (grab.indexOf("t") >= 0) ? b : t;
            const wN = Math.abs(((grab.indexOf("l") >= 0) ? l : r) - ax);
            const hN = Math.abs(((grab.indexOf("t") >= 0) ? t : b) - ay);
            let w2 = Math.max(wN, hN * ratioN);
            if ((grab.indexOf("l") >= 0) && ax - w2 < 0) w2 = ax;
            if ((grab.indexOf("r") >= 0) && ax + w2 > 1) w2 = 1 - ax;
            let h2 = w2 / ratioN;
            if ((grab.indexOf("t") >= 0) && ay - h2 < 0) { h2 = ay; w2 = h2 * ratioN; }
            if ((grab.indexOf("b") >= 0) && ay + h2 > 1) { h2 = 1 - ay; w2 = h2 * ratioN; }
            l = (grab.indexOf("l") >= 0) ? ax - w2 : ax;
            r = (grab.indexOf("l") >= 0) ? ax : ax + w2;
            t = (grab.indexOf("t") >= 0) ? ay - h2 : ay;
            b = (grab.indexOf("t") >= 0) ? ay : ay + h2;
        }
        cropX = l; cropY = t; cropW = r - l; cropH = b - t;
    }

    Item {
        id: imageArea
        objectName: "imageArea"
        anchors.fill: parent
        anchors.margins: 28
        anchors.bottomMargin: 28 + statusBar.height
        visible: engine.hasImage
        clip: true

        readonly property string afterSrc: engine.hasImage ? ("image://preview/frame?rev=" + engine.previewRevision) : ""
        readonly property string beforeSrc: engine.hasBefore ? ("image://preview/before?rev=" + engine.beforeRevision) : ""
        readonly property bool split: canvas.compareMode === 1 && engine.hasBefore
        readonly property bool sideBySide: canvas.compareMode === 2 && engine.hasBefore
        readonly property bool peeking: canvas.peekBefore && engine.hasBefore && canvas.compareMode === 0
        property real zoom: 1.0
        property real panX: 0.0
        property real panY: 0.0
        readonly property bool zoomed: zoom > 1.001

        function clampPan() {
            const maxX = Math.max(0, width * (zoom - 1) / 2);
            const maxY = Math.max(0, height * (zoom - 1) / 2);
            panX = Math.max(-maxX, Math.min(maxX, panX));
            panY = Math.max(-maxY, Math.min(maxY, panY));
        }
        function resetZoom() { zoom = 1.0; panX = 0.0; panY = 0.0; }
        function setZoom(nextZoom, focusX, focusY) {
            const oldZoom = zoom;
            const bounded = Math.max(1.0, Math.min(4.0, nextZoom));
            if (Math.abs(bounded - oldZoom) < 0.0001) return;
            const dx = focusX - width / 2;
            const dy = focusY - height / 2;
            panX = dx - (dx - panX) * bounded / oldZoom;
            panY = dy - (dy - panY) * bounded / oldZoom;
            zoom = bounded;
            clampPan();
        }

        Item {
            id: zoomSurface
            anchors.fill: parent
            transformOrigin: Item.Center
            scale: imageArea.zoom
            transform: Translate { x: imageArea.panX; y: imageArea.panY }
            Image {
                id: afterImg
                anchors.fill: parent
                fillMode: Image.PreserveAspectFit
                cache: false
                source: imageArea.peeking ? imageArea.beforeSrc : imageArea.afterSrc
                visible: !imageArea.sideBySide
            }
            Rectangle {
                visible: imageArea.peeking && !imageArea.sideBySide
                anchors { left: parent.left; top: parent.top; margins: 6 }
                width: peekLabel.implicitWidth + 16; height: 22; radius: 5
                color: "#8c000000"
                Text { id: peekLabel; anchors.centerIn: parent; text: "Before"; color: Theme.text; font.pixelSize: Theme.fontCaption; font.weight: Font.Medium }
            }
            Item {
                anchors { top: parent.top; bottom: parent.bottom; left: parent.left }
                width: divider.x
                clip: true
                visible: imageArea.split
                Image { width: imageArea.width; height: imageArea.height; fillMode: Image.PreserveAspectFit; cache: false; source: imageArea.beforeSrc }
            }
            Rectangle {
                visible: imageArea.split
                anchors { left: parent.left; top: parent.top; margins: 6 }
                width: splitLabel.implicitWidth + 16; height: 22; radius: 5
                color: "#8c000000"
                Text { id: splitLabel; anchors.centerIn: parent; text: "Before"; color: Theme.text; font.pixelSize: Theme.fontCaption; font.weight: Font.Medium }
            }
            Item {
                id: divider
                visible: imageArea.split
                y: 0
                x: imageArea.width / 2
                width: 2
                height: imageArea.height
                Rectangle { anchors.fill: parent; color: Theme.knob }
                Rectangle {
                    anchors.centerIn: parent
                    width: 28; height: 28; radius: 14
                    color: Theme.knob
                    FlIcon { anchors.centerIn: parent; name: "arrows-left-right"; size: 16; color: Theme.inset }
                }
                MouseArea {
                    anchors.fill: parent
                    anchors.margins: -14
                    cursorShape: Qt.SizeHorCursor
                    drag.target: divider
                    drag.axis: Drag.XAxis
                    drag.minimumX: 0
                    drag.maximumX: imageArea.width
                }
            }
            Row {
                anchors.fill: parent
                visible: imageArea.sideBySide
                spacing: 2
                Item {
                    width: (parent.width - 2) / 2
                    height: parent.height
                    Image { anchors.fill: parent; fillMode: Image.PreserveAspectFit; cache: false; source: imageArea.beforeSrc }
                    Text { anchors { horizontalCenter: parent.horizontalCenter; top: parent.top; topMargin: 2 } text: "Before"; color: Theme.textSecondary; font.pixelSize: Theme.fontCaption }
                }
                Item {
                    width: (parent.width - 2) / 2
                    height: parent.height
                    Image { anchors.fill: parent; fillMode: Image.PreserveAspectFit; cache: false; source: imageArea.afterSrc }
                    Text { anchors { horizontalCenter: parent.horizontalCenter; top: parent.top; topMargin: 2 } text: "After"; color: Theme.textSecondary; font.pixelSize: Theme.fontCaption }
                }
            }
        }

        // Crop overlay (logic unchanged from v1).
        Item {
            id: cropOverlay
            anchors.fill: parent
            visible: canvas.cropMode && !imageArea.split && !imageArea.sideBySide && engine.hasImage
            readonly property real pw: afterImg.paintedWidth
            readonly property real ph: afterImg.paintedHeight
            readonly property real ox: (width - pw) / 2
            readonly property real oy: (height - ph) / 2
            readonly property real rx: ox + canvas.cropX * pw
            readonly property real ry: oy + canvas.cropY * ph
            readonly property real rw: canvas.cropW * pw
            readonly property real rh: canvas.cropH * ph
            readonly property bool free: canvas.cropAspect <= 0
            Rectangle { color: "#9e000000"; x: cropOverlay.ox; y: cropOverlay.oy; width: cropOverlay.pw; height: Math.max(0, cropOverlay.ry - cropOverlay.oy) }
            Rectangle { color: "#9e000000"; x: cropOverlay.ox; y: cropOverlay.ry + cropOverlay.rh; width: cropOverlay.pw; height: Math.max(0, (cropOverlay.oy + cropOverlay.ph) - (cropOverlay.ry + cropOverlay.rh)) }
            Rectangle { color: "#9e000000"; x: cropOverlay.ox; y: cropOverlay.ry; width: Math.max(0, cropOverlay.rx - cropOverlay.ox); height: cropOverlay.rh }
            Rectangle { color: "#9e000000"; x: cropOverlay.rx + cropOverlay.rw; y: cropOverlay.ry; width: Math.max(0, (cropOverlay.ox + cropOverlay.pw) - (cropOverlay.rx + cropOverlay.rw)); height: cropOverlay.rh }
            Rectangle {
                x: cropOverlay.rx; y: cropOverlay.ry; width: cropOverlay.rw; height: cropOverlay.rh
                color: "transparent"; border.width: 1; border.color: "#e6ffffff"
                Rectangle { color: "#47ffffff"; width: 1; height: parent.height; x: Math.round(parent.width / 3) }
                Rectangle { color: "#47ffffff"; width: 1; height: parent.height; x: Math.round(2 * parent.width / 3) }
                Rectangle { color: "#47ffffff"; height: 1; width: parent.width; y: Math.round(parent.height / 3) }
                Rectangle { color: "#47ffffff"; height: 1; width: parent.width; y: Math.round(2 * parent.height / 3) }
            }
            Repeater {
                model: [
                    { k: "lt", hx: cropOverlay.rx,                    hy: cropOverlay.ry,                    corner: true },
                    { k: "rt", hx: cropOverlay.rx + cropOverlay.rw,   hy: cropOverlay.ry,                    corner: true },
                    { k: "lb", hx: cropOverlay.rx,                    hy: cropOverlay.ry + cropOverlay.rh,   corner: true },
                    { k: "rb", hx: cropOverlay.rx + cropOverlay.rw,   hy: cropOverlay.ry + cropOverlay.rh,   corner: true },
                    { k: "t",  hx: cropOverlay.rx + cropOverlay.rw/2, hy: cropOverlay.ry,                    corner: false },
                    { k: "b",  hx: cropOverlay.rx + cropOverlay.rw/2, hy: cropOverlay.ry + cropOverlay.rh,   corner: false },
                    { k: "l",  hx: cropOverlay.rx,                    hy: cropOverlay.ry + cropOverlay.rh/2, corner: false },
                    { k: "r",  hx: cropOverlay.rx + cropOverlay.rw,   hy: cropOverlay.ry + cropOverlay.rh/2, corner: false }
                ]
                delegate: Rectangle {
                    visible: modelData.corner || cropOverlay.free
                    width: modelData.corner ? 12 : 10
                    height: width
                    radius: modelData.corner ? 2 : 5
                    color: "#f2ffffff"
                    border.width: 1; border.color: "#60000000"
                    x: modelData.hx - width / 2
                    y: modelData.hy - height / 2
                }
            }
            MouseArea {
                anchors.fill: parent
                enabled: canvas.cropMode
                cursorShape: grab === "" ? Qt.ArrowCursor : (grab === "move" ? Qt.SizeAllCursor : Qt.CrossCursor)
                property string grab: ""
                property real startNx: 0
                property real startNy: 0
                property real startCropX: 0
                property real startCropY: 0
                onPressed: (m) => {
                    const hs = 16;
                    const l = cropOverlay.rx, t = cropOverlay.ry;
                    const r = cropOverlay.rx + cropOverlay.rw, b = cropOverlay.ry + cropOverlay.rh;
                    const cx = (l + r) / 2, cy = (t + b) / 2;
                    const near = (px, py) => Math.abs(m.x - px) <= hs && Math.abs(m.y - py) <= hs;
                    const free = cropOverlay.free;
                    grab = "";
                    if (near(l, t)) grab = "lt";
                    else if (near(r, t)) grab = "rt";
                    else if (near(l, b)) grab = "lb";
                    else if (near(r, b)) grab = "rb";
                    else if (free && near(cx, t)) grab = "t";
                    else if (free && near(cx, b)) grab = "b";
                    else if (free && near(l, cy)) grab = "l";
                    else if (free && near(r, cy)) grab = "r";
                    else if (m.x > l && m.x < r && m.y > t && m.y < b) {
                        grab = "move";
                        startNx = (m.x - cropOverlay.ox) / cropOverlay.pw;
                        startNy = (m.y - cropOverlay.oy) / cropOverlay.ph;
                        startCropX = canvas.cropX;
                        startCropY = canvas.cropY;
                    }
                }
                onPositionChanged: (m) => {
                    if (grab === "" || cropOverlay.pw <= 0 || cropOverlay.ph <= 0) return;
                    const nx = (m.x - cropOverlay.ox) / cropOverlay.pw;
                    const ny = (m.y - cropOverlay.oy) / cropOverlay.ph;
                    if (grab === "move") {
                        canvas.cropX = Math.max(0, Math.min(1 - canvas.cropW, startCropX + nx - startNx));
                        canvas.cropY = Math.max(0, Math.min(1 - canvas.cropH, startCropY + ny - startNy));
                    } else {
                        canvas.updateCrop(grab, nx, ny);
                    }
                }
                onReleased: grab = ""
            }
        }

        MouseArea {
            anchors.fill: parent
            enabled: !canvas.cropMode
            hoverEnabled: true
            cursorShape: pressed && imageArea.zoomed ? Qt.SizeAllCursor : Qt.ArrowCursor
            property real startX: 0
            property real startY: 0
            property real startPanX: 0
            property real startPanY: 0
            onPressed: (mouse) => {
                canvas.backgroundPressed();
                if (!imageArea.zoomed) { mouse.accepted = false; return; }
                startX = mouse.x; startY = mouse.y;
                startPanX = imageArea.panX; startPanY = imageArea.panY;
            }
            onPositionChanged: (mouse) => {
                if (!pressed || !imageArea.zoomed) return;
                imageArea.panX = startPanX + mouse.x - startX;
                imageArea.panY = startPanY + mouse.y - startY;
                imageArea.clampPan();
            }
            onWheel: (wheel) => {
                imageArea.setZoom(imageArea.zoom * (wheel.angleDelta.y > 0 ? 1.16 : 1 / 1.16), wheel.x, wheel.y);
                wheel.accepted = true;
            }
            onDoubleClicked: (mouse) => {
                if (imageArea.zoomed) imageArea.resetZoom();
                else imageArea.setZoom(2.0, mouse.x, mouse.y);
            }
        }
    }

    FlCropToolbar {
        visible: canvas.cropMode
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 18 + statusBar.height
        compact: canvas.width < 560
        onAspectChosen: (r) => canvas.selectAspect(r)
        onResetRequested: canvas.resetCropMode()
        onDoneRequested: canvas.applyCropMode()
    }

    Column {
        anchors.centerIn: parent
        visible: !engine.hasImage
        spacing: 8
        Text { anchors.horizontalCenter: parent.horizontalCenter; text: "Film Lab"; color: Theme.text; font.pixelSize: 28; font.weight: Font.DemiBold }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: engine.lightroomRoundTrip ? "Preparing your photo…" : "Open a photo or pick one from your library"
            color: Theme.textCaption
            font.pixelSize: Theme.fontBody
        }
    }

    Rectangle {
        id: statusBar
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: engine.status.length > 0 ? Math.max(36, statusText.implicitHeight + 18) : 0
        visible: height > 0
        color: "#e6161618"
        FlHairline { anchors.top: parent.top }
        Text {
            id: statusText
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.leftMargin: 16
            anchors.rightMargin: 16
            wrapMode: Text.Wrap
            maximumLineCount: 3
            elide: Text.ElideRight
            text: engine.status
            color: engine.status.startsWith("Open failed:") || engine.status.startsWith("Render failed:") || engine.status.startsWith("Export failed:") ? Theme.danger : Theme.textSecondary
            font.pixelSize: Theme.fontLabel
        }
    }
}
