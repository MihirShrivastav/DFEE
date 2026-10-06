import QtQuick
import DFEE

// The inspector's pinned scope: histogram (default) or vectorscope; a click asks the
// owner to switch. Drawing ported from v1 Main.qml (Histogram, Vectorscope).
Rectangle {
    id: scope
    property string mode: "histogram"     // "histogram" | "vectorscope"
    signal switchRequested()
    width: parent ? parent.width : 268
    height: mode === "histogram" ? 64 : 152
    radius: Theme.radiusTile
    color: Theme.inset
    clip: true
    Behavior on height { NumberAnimation { duration: Theme.motionNormal; easing.type: Easing.OutCubic } }

    Canvas {
        id: hist
        anchors.fill: parent
        anchors.margins: 4
        visible: scope.mode === "histogram"
        property var hr: engine.histogramR
        property var hg: engine.histogramG
        property var hb: engine.histogramB
        onHrChanged: requestPaint()
        onHgChanged: requestPaint()
        onHbChanged: requestPaint()
        onVisibleChanged: if (visible) requestPaint()
        onPaint: {
            const ctx = getContext("2d");
            ctx.reset();
            const w = width, h = height;
            if (!hr || hr.length < 2) return;
            const n = hr.length;
            let mx = 1;
            for (let i = 1; i < n - 1; ++i) mx = Math.max(mx, hr[i], hg[i], hb[i]);
            function channel(arr, style) {
                ctx.beginPath();
                ctx.moveTo(0, h);
                for (let i = 0; i < n; ++i) {
                    const v = Math.min(1, Math.sqrt(arr[i] / mx));
                    ctx.lineTo(i / (n - 1) * w, h - v * h);
                }
                ctx.lineTo(w, h);
                ctx.closePath();
                ctx.fillStyle = style;
                ctx.fill();
            }
            ctx.globalCompositeOperation = "lighter";
            channel(hb, "rgba(90,150,255,0.32)");
            channel(hg, "rgba(90,210,120,0.30)");
            channel(hr, "rgba(255,90,80,0.32)");
            ctx.globalCompositeOperation = "source-over";
        }
    }

    // Rec.709 Cb/Cr density from the worker (64×64 cells), colored by each cell's hue.
    Canvas {
        id: vec
        anchors.fill: parent
        anchors.margins: 8
        visible: scope.mode === "vectorscope"
        property var samples: engine.vectorscope
        onSamplesChanged: requestPaint()
        onVisibleChanged: if (visible) requestPaint()
        onPaint: {
            const ctx = getContext("2d");
            ctx.reset();
            if (!samples || samples.length !== 4096) return;
            const side = Math.min(width, height);
            const ox = width * 0.5;
            const oy = height * 0.5;
            const radius = side * 0.455;
            ctx.strokeStyle = "rgba(220,220,226,0.12)";
            ctx.lineWidth = 1;
            ctx.beginPath(); ctx.arc(ox, oy, radius, 0, Math.PI * 2); ctx.stroke();
            ctx.beginPath(); ctx.arc(ox, oy, radius * 0.5, 0, Math.PI * 2); ctx.stroke();
            ctx.beginPath();
            ctx.moveTo(ox - radius, oy); ctx.lineTo(ox + radius, oy);
            ctx.moveTo(ox, oy - radius); ctx.lineTo(ox, oy + radius);
            ctx.stroke();
            let maxCount = 1;
            for (let i = 0; i < samples.length; ++i) maxCount = Math.max(maxCount, samples[i]);
            const logMax = Math.log(1 + maxCount);
            const cells = 64;
            const cell = (radius * 2) / cells;
            ctx.globalCompositeOperation = "lighter";
            for (let yy = 0; yy < cells; ++yy) {
                for (let xx = 0; xx < cells; ++xx) {
                    const count = samples[yy * cells + xx];
                    if (count <= 0) continue;
                    const dx = (xx + 0.5) / cells * 2 - 1;
                    const dy = (yy + 0.5) / cells * 2 - 1;
                    if (dx * dx + dy * dy > 1) continue;
                    const density = Math.log(1 + count) / logMax;
                    const cb = dx * 0.5, cr = -dy * 0.5;
                    let r = 0.5 + 1.5748 * cr, b = 0.5 + 1.8556 * cb;
                    let g = (0.5 - 0.2126 * r - 0.0722 * b) / 0.7152;
                    r = Math.max(0, Math.min(1, r)); g = Math.max(0, Math.min(1, g)); b = Math.max(0, Math.min(1, b));
                    ctx.fillStyle = "rgba(" + Math.round(r * 255) + "," + Math.round(g * 255) + ","
                        + Math.round(b * 255) + "," + Math.min(0.5, 0.05 + 0.42 * density) + ")";
                    ctx.beginPath();
                    ctx.arc(ox - radius + (xx + 0.5) * cell, oy - radius + (yy + 0.5) * cell,
                            Math.max(1.1, cell * (0.8 + density * 0.45)), 0, Math.PI * 2);
                    ctx.fill();
                }
            }
            ctx.globalCompositeOperation = "source-over";
        }
    }

    Text {
        anchors.centerIn: parent
        visible: !engine.hasImage
        text: scope.mode === "histogram" ? "Histogram" : "Vectorscope"
        color: Theme.textTertiary
        font.pixelSize: Theme.fontCaption
    }
    HoverHandler { id: hover }
    MouseArea {
        anchors.fill: parent
        onClicked: {
            // A slider keeps the arrows only until something else is clicked.
            const w = scope.Window.window;
            if (w && w.activeFocusItem && w.activeFocusItem.keepsArrowKeys === true && w.returnFocus) w.returnFocus();
            scope.switchRequested();
        }
    }
    FlTip {
        visible: hover.hovered
        text: scope.mode === "histogram" ? "Click for the vectorscope" : "Click for the histogram"
    }
}
