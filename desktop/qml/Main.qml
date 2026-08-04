import QtQuick
import QtQuick.Window
import QtQuick.Controls
import QtQuick.Dialogs

Window {
    id: root

    width: 1280
    height: 800
    minimumWidth: 960
    minimumHeight: 620
    visible: true
    title: "DFEE"
    color: bg

    // Graphite palette — charcoal, monochrome. Color lives only in the photo + boxart.
    readonly property color bg: "#0f0f10"
    readonly property color canvas: "#0f0f10"
    readonly property color panel: "#1a1a1c"
    readonly property color panelRaised: "#202023"
    readonly property color inset: "#141416"          // recessed fields / slider grooves
    readonly property color border: "#26262b"
    readonly property color hair: "#14ffffff"           // ~0.08 white hairline (#AARRGGBB)
    readonly property color textPrimary: "#c7c7cc"      // softened — no pure white
    readonly property color textSecondary: "#8b8b90"
    readonly property color textMuted: "#5a5a60"
    readonly property color textValue: "#74747a"        // dim right-hand slider readouts
    readonly property color accent: "#e9e9ec"           // inverted light chip (checkbox tick only)
    readonly property color accentDark: "#c9c9ce"       // pressed light chip
    readonly property color accentText: "#161618"       // text on a light accent fill
    readonly property color knob: "#c4c4c9"             // slider knob
    readonly property color danger: "#e0655b"
    property bool libraryOpen: true       // left folders pane
    property bool filmstripOpen: true     // bottom thumbnail strip

    function exportFormatLabel(format) {
        if (format === "png8") return "8-bit PNG"
        if (format === "png16") return "16-bit PNG"
        if (format === "jpeg") return "JPEG"
        return "16-bit TIFF"
    }

    // Boxart tile for a stock id (bundled SVGs at :/boxart/<id>.svg). Empty for "none".
    function boxartFor(stockId) {
        return (stockId && stockId !== "none") ? ("qrc:/boxart/" + stockId + ".svg") : ""
    }

    component BoxartSwatch: Rectangle {
        property string stockId: ""
        property int cell: 26
        width: cell
        height: cell
        radius: 4
        clip: true
        color: root.inset
        border.width: 1
        border.color: root.hair
        Image {
            anchors.fill: parent
            sourceSize.width: parent.cell * 2
            sourceSize.height: parent.cell * 2
            fillMode: Image.PreserveAspectCrop
            smooth: true
            source: root.boxartFor(parent.stockId)
            visible: source != ""
        }
    }

    FileDialog {
        id: openDialog
        title: "Open image"
        nameFilters: ["Supported images (*.tif *.tiff *.arw *.nef *.cr3 *.raf *.rw2 *.dng)"]
        onAccepted: engine.openFile(selectedFile)
    }

    FolderDialog {
        id: folderDialog
        title: "Add folder to library"
        onAccepted: library.addFolder(selectedFolder)
    }

    // A library thumbnail tile (grid + filmstrip share it).
    component ThumbTile: Rectangle {
        property string path: ""
        property string name: ""
        property int edge: 128
        radius: 6
        color: root.inset
        border.width: 1
        border.color: root.hair
        clip: true
        Image {
            anchors.fill: parent
            anchors.margins: 1
            asynchronous: true
            cache: true
            fillMode: Image.PreserveAspectCrop
            sourceSize.width: parent.edge
            sourceSize.height: parent.edge
            source: parent.path !== "" ? ("image://thumb/" + encodeURIComponent(parent.path)) : ""
        }
        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: engine.openFile(Qt.resolvedUrl("file:///" + parent.path.replace(/\\/g, "/")))
        }
    }

    component InspectorLabel: Text {
        color: root.textSecondary
        font.pixelSize: 12
        font.weight: Font.Medium
    }

    // Graphite-styled hover tooltip. Pair with a HoverHandler: `visible: hh.hovered`.
    // Caps its own width so long copy wraps instead of stretching across the screen.
    component GraphiteTip: ToolTip {
        id: tip
        delay: 450
        padding: 10
        // Cap the tooltip width; short copy shrinks, long copy wraps at 260.
        width: Math.min(implicitWidth, 260)
        contentItem: Text {
            text: tip.text
            color: root.textPrimary
            font.pixelSize: 11
            lineHeight: 1.3
            wrapMode: Text.WordWrap
            width: tip.availableWidth
        }
        background: Rectangle {
            color: "#232327"
            radius: 7
            border.width: 1
            border.color: root.hair
        }
    }

    // Live RGB histogram of the current preview — additive channel fills, sqrt-scaled
    // so low counts stay visible. Reads engine.histogramR/G/B (256 raw-count bins).
    component Histogram: Rectangle {
        width: parent.width
        height: 84
        radius: 10
        color: root.inset
        border.width: 1
        border.color: root.hair

        Canvas {
            anchors.fill: parent
            anchors.margins: 7
            property var hr: engine.histogramR
            property var hg: engine.histogramG
            property var hb: engine.histogramB
            onHrChanged: requestPaint()
            onHgChanged: requestPaint()
            onHbChanged: requestPaint()
            onPaint: {
                var ctx = getContext("2d");
                ctx.reset();
                var w = width, hgt = height;
                ctx.clearRect(0, 0, w, hgt);
                if (!hr || hr.length < 2) return;
                var n = hr.length;
                var mx = 1;
                for (var i = 1; i < n - 1; ++i) {
                    if (hr[i] > mx) mx = hr[i];
                    if (hg[i] > mx) mx = hg[i];
                    if (hb[i] > mx) mx = hb[i];
                }
                function drawCh(arr, style) {
                    ctx.beginPath();
                    ctx.moveTo(0, hgt);
                    for (var i = 0; i < n; ++i) {
                        var v = Math.sqrt(arr[i] / mx);
                        if (v > 1) v = 1;
                        ctx.lineTo(i / (n - 1) * w, hgt - v * hgt);
                    }
                    ctx.lineTo(w, hgt);
                    ctx.closePath();
                    ctx.fillStyle = style;
                    ctx.fill();
                }
                ctx.globalCompositeOperation = "lighter";
                drawCh(hb, "rgba(70,120,235,0.5)");
                drawCh(hg, "rgba(70,200,110,0.5)");
                drawCh(hr, "rgba(235,80,80,0.5)");
                ctx.globalCompositeOperation = "source-over";
            }
        }

        Text {
            anchors.centerIn: parent
            visible: !engine.hasImage
            text: "Histogram"
            color: root.textMuted
            font.pixelSize: 11
        }
    }

    // Small down/up chevron used in card headers (and reused as the combo indicator style).
    component ChevronToggle: Canvas {
        property bool open: true
        width: 12
        height: 7
        onOpenChanged: requestPaint()
        onPaint: {
            var c = getContext("2d");
            c.reset();
            c.strokeStyle = "#8b8b90";
            c.lineWidth = 1.5;
            c.lineCap = "round";
            c.beginPath();
            // collapsed → point down (expand); expanded → point up (collapse)
            if (open) { c.moveTo(1, 6); c.lineTo(6, 1); c.lineTo(11, 6); }
            else { c.moveTo(1, 1); c.lineTo(6, 6); c.lineTo(11, 1); }
            c.stroke();
        }
    }

    // Beveled action button for the Geometry tab (rotate / flip / reset). `active`
    // highlights it as an engaged toggle (used by the Flip buttons).
    component GeoButton: Button {
        id: geoBtn
        property bool active: false
        property string tooltip: ""
        height: 30
        contentItem: Text {
            text: geoBtn.text
            color: geoBtn.active ? root.textPrimary : root.textSecondary
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            font.pixelSize: 12
            font.weight: Font.Medium
        }
        background: Rectangle {
            radius: 7
            color: geoBtn.active ? "#3a3a42" : (geoBtn.down ? "#26262b" : "#2e2e34")
            border.width: 1
            border.color: geoBtn.active ? "#4a4a52" : root.hair
            Rectangle { anchors { left: parent.left; right: parent.right; top: parent.top; margins: 1 } height: 1; radius: 1; color: "#16ffffff" }
        }
        HoverHandler { id: geoHover }
        GraphiteTip {
            parent: geoBtn
            x: 0
            y: geoBtn.height + 4
            visible: geoHover.hovered && geoBtn.tooltip.length > 0
            text: geoBtn.tooltip
        }
    }

    // Tactile checkbox — recessed square when off, light chip with a drawn tick when on.
    component GraphiteCheck: CheckBox {
        id: cb
        property string caption: ""
        property string tooltip: ""
        spacing: 9
        implicitHeight: 20

        HoverHandler { id: cbHover; enabled: cb.tooltip.length > 0 }
        GraphiteTip {
            parent: cb
            x: 0
            y: cb.height + 4
            visible: cbHover.hovered && cb.tooltip.length > 0
            text: cb.tooltip
        }

        indicator: Rectangle {
            implicitWidth: 18
            implicitHeight: 18
            x: 0
            y: (cb.height - height) / 2
            radius: 5
            // Tactile: recessed inset when off, raised dark bevel chip when on
            // (matches the buttons and segmented — no clashing flat white).
            gradient: Gradient {
                GradientStop { position: 0.0; color: cb.checked ? "#34343a" : root.inset }
                GradientStop { position: 1.0; color: cb.checked ? "#242429" : root.inset }
            }
            border.width: 1
            border.color: root.hair
            opacity: cb.enabled ? 1.0 : 0.5
            // top bevel highlight when checked
            Rectangle {
                visible: cb.checked
                anchors { left: parent.left; right: parent.right; top: parent.top; margins: 1 }
                height: 1; radius: 1; color: "#16ffffff"
            }
            Canvas {
                anchors.fill: parent
                visible: cb.checked
                onVisibleChanged: if (visible) requestPaint()
                onPaint: {
                    var c = getContext("2d");
                    c.reset();
                    c.strokeStyle = "#d7d7db";
                    c.lineWidth = 2;
                    c.lineCap = "round";
                    c.lineJoin = "round";
                    c.beginPath();
                    c.moveTo(4.5, 9); c.lineTo(8, 12.5); c.lineTo(13.5, 5.5);
                    c.stroke();
                }
            }
        }

        contentItem: Text {
            text: cb.caption !== "" ? cb.caption : cb.text
            color: cb.enabled ? root.textSecondary : root.textMuted
            leftPadding: cb.indicator.width + cb.spacing
            verticalAlignment: Text.AlignVCenter
            font.pixelSize: 12
        }
    }

    // Number field in Graphite style: recessed inset value + raised bevel -/+ chips.
    component GraphiteSpin: SpinBox {
        id: spin
        implicitHeight: 30
        implicitWidth: 118
        font.pixelSize: 12

        contentItem: TextInput {
            z: 2
            text: spin.textFromValue(spin.value, spin.locale)
            color: root.textPrimary
            font: spin.font
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            readOnly: !spin.editable
            validator: spin.validator
            inputMethodHints: Qt.ImhFormattedNumbersOnly
            selectByMouse: true
            selectionColor: root.textSecondary
        }

        background: Rectangle {
            radius: 8
            color: root.inset
            border.width: 1
            border.color: root.hair
        }

        down.indicator: Rectangle {
            x: 0; y: 0
            width: 32
            height: spin.height
            topLeftRadius: 8; bottomLeftRadius: 8
            gradient: Gradient {
                GradientStop { position: 0.0; color: spin.down.pressed ? "#26262b" : "#33333a" }
                GradientStop { position: 1.0; color: spin.down.pressed ? "#1d1d20" : "#242429" }
            }
            border.width: 1
            border.color: root.hair
            Rectangle { anchors { left: parent.left; right: parent.right; top: parent.top; margins: 1 } height: 1; color: "#16ffffff" }
            Text { anchors.centerIn: parent; text: "−"; color: root.textPrimary; font.pixelSize: 15 }
        }

        up.indicator: Rectangle {
            x: spin.width - width; y: 0
            width: 32
            height: spin.height
            topRightRadius: 8; bottomRightRadius: 8
            gradient: Gradient {
                GradientStop { position: 0.0; color: spin.up.pressed ? "#26262b" : "#33333a" }
                GradientStop { position: 1.0; color: spin.up.pressed ? "#1d1d20" : "#242429" }
            }
            border.width: 1
            border.color: root.hair
            Rectangle { anchors { left: parent.left; right: parent.right; top: parent.top; margins: 1 } height: 1; color: "#16ffffff" }
            Text { anchors.centerIn: parent; text: "+"; color: root.textPrimary; font.pixelSize: 15 }
        }
    }

    // Draggable color-grading wheel: angle = hue, radius = saturation. Reads/writes
    // engine.filmControls["cg_<zone>_hue"/"_sat"]. Double-click resets to neutral.
    component ColorWheel: Item {
        id: wheel
        property string zone: ""
        property string label: ""
        property int diameter: 84
        width: diameter
        height: diameter + 18
        readonly property real hueVal: Number(engine.filmControls["cg_" + zone + "_hue"])
        readonly property real satVal: Number(engine.filmControls["cg_" + zone + "_sat"])

        Rectangle {
            id: disc
            width: wheel.diameter; height: wheel.diameter; radius: wheel.diameter / 2
            anchors.horizontalCenter: parent.horizontalCenter
            color: root.inset
            border.width: 1; border.color: root.hair
            clip: true

            Canvas {
                anchors.fill: parent
                onPaint: {
                    var ctx = getContext("2d"); ctx.reset();
                    var w = width, cx = w / 2, cy = w / 2, r = w / 2;
                    for (var a = 0; a < 360; a += 3) {
                        ctx.beginPath();
                        ctx.moveTo(cx, cy);
                        ctx.arc(cx, cy, r, (-(a + 3)) * Math.PI / 180, (-a) * Math.PI / 180, false);
                        ctx.closePath();
                        ctx.fillStyle = "hsl(" + a + ",68%,52%)";
                        ctx.fill();
                    }
                    var g = ctx.createRadialGradient(cx, cy, 0, cx, cy, r);
                    g.addColorStop(0.0, "rgba(20,20,22,1.0)");
                    g.addColorStop(0.35, "rgba(20,20,22,0.35)");
                    g.addColorStop(1.0, "rgba(20,20,22,0.0)");
                    ctx.fillStyle = g;
                    ctx.beginPath(); ctx.arc(cx, cy, r, 0, 2 * Math.PI); ctx.fill();
                }
            }

            Rectangle {                          // handle
                width: 13; height: 13; radius: 7
                border.width: 2; border.color: "#f4f4f6"
                x: disc.width / 2 + Math.cos(wheel.hueVal * Math.PI / 180) * (wheel.satVal / 100) * (disc.width / 2 - 9) - width / 2
                y: disc.height / 2 - Math.sin(wheel.hueVal * Math.PI / 180) * (wheel.satVal / 100) * (disc.height / 2 - 9) - height / 2
                color: wheel.satVal > 0 ? Qt.hsla(((wheel.hueVal % 360) + 360) % 360 / 360, Math.min(wheel.satVal / 100, 1.0), 0.55, 1.0) : root.panelRaised
            }

            MouseArea {
                id: wheelDrag
                anchors.fill: parent
                cursorShape: Qt.CrossCursor
                onPressed: (mouse) => wheel.pick(mouse.x, mouse.y)
                onPositionChanged: (mouse) => { if (pressed) wheel.pick(mouse.x, mouse.y) }
                onDoubleClicked: {
                    engine.setFilmControl("cg_" + wheel.zone + "_hue", 0);
                    engine.setFilmControl("cg_" + wheel.zone + "_sat", 0);
                }
            }

            HoverHandler { id: wheelHover }
            GraphiteTip {
                parent: wheel
                x: 0
                y: wheel.height + 4
                visible: wheelHover.hovered && !wheelDrag.pressed
                text: "Tints the " + wheel.label.toLowerCase() + " — drag from the centre to add color, double-click to reset."
            }
        }

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            text: wheel.label
            color: root.textSecondary
            font.pixelSize: 11
        }

        function pick(mx, my) {
            var cx = wheel.diameter / 2, cy = wheel.diameter / 2;
            var dx = mx - cx, dy = my - cy;
            var ang = Math.atan2(-dy, dx) * 180 / Math.PI;
            if (ang < 0) ang += 360;
            var rad = Math.min(Math.sqrt(dx * dx + dy * dy) / (wheel.diameter / 2), 1.0);
            engine.setFilmControl("cg_" + wheel.zone + "_hue", Math.round(ang));
            engine.setFilmControl("cg_" + wheel.zone + "_sat", Math.round(rad * 100));
        }
    }

    component InspectorSlider: Slider {
        id: control
        width: parent.width
        implicitHeight: 24

        background: Rectangle {
            x: control.leftPadding
            y: control.topPadding + control.availableHeight / 2 - height / 2
            width: control.availableWidth
            height: 4
            radius: 2
            color: root.inset                       // recessed groove
            // subtle top inset line for depth
            Rectangle { width: parent.width; height: 1; radius: 1; color: "#66000000" }
            Rectangle {                              // filled portion — subtle grey, monochrome
                width: control.visualPosition * parent.width
                height: parent.height
                radius: parent.radius
                color: "#3c3c41"
            }
        }

        handle: Rectangle {                          // raised metallic knob
            x: control.leftPadding + control.visualPosition * (control.availableWidth - width)
            y: control.topPadding + control.availableHeight / 2 - height / 2
            width: 14
            height: 14
            radius: 7
            gradient: Gradient {
                GradientStop { position: 0.0; color: control.pressed ? "#d7d7db" : "#cdcdd2" }
                GradientStop { position: 1.0; color: "#9a9aa1" }
            }
            border.width: 1
            border.color: "#6e000000"
        }
    }

    // Primary action — a raised dark chip with a top bevel highlight (no more white).
    component PrimaryButton: Button {
        id: button
        width: parent.width
        height: 38
        font.pixelSize: 13
        font.weight: Font.Medium

        contentItem: Text {
            text: button.text
            color: button.enabled ? root.textPrimary : root.textMuted
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            font: button.font
        }

        background: Rectangle {
            radius: 8
            gradient: Gradient {
                GradientStop { position: 0.0; color: !button.enabled ? root.panel : (button.down ? "#26262b" : "#34343a") }
                GradientStop { position: 1.0; color: !button.enabled ? root.panel : (button.down ? "#1d1d20" : "#242429") }
            }
            border.width: 1
            border.color: root.hair
            Rectangle {
                anchors { left: parent.left; right: parent.right; top: parent.top; margins: 1 }
                height: 1; radius: 1; color: "#16ffffff"; visible: button.enabled
            }
        }
    }

    // Secondary action — flatter, recessed, hairline outline.
    component SecondaryButton: Button {
        id: button
        width: parent.width
        height: 38
        font.pixelSize: 13
        font.weight: Font.Medium

        contentItem: Text {
            text: button.text
            color: button.enabled ? root.textSecondary : root.textMuted
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            font: button.font
        }

        background: Rectangle {
            radius: 8
            color: button.down ? "#1c1c1f" : root.inset
            border.width: 1
            border.color: root.hair
        }
    }

    // Beveled group card — subtle raised gradient, hairline border, top highlight, and a
    // collapsible header. Content is provided inline (avoids the QML default-property trap).
    component FilmSlider: Column {
        id: sliderRow
        property string controlKey: ""
        property string label: ""
        property string tooltip: ""
        property real minimum: 0
        property real maximum: 100
        property real increment: 1
        property real neutral: 0
        property bool bipolar: false
        property bool decimals: false
        property bool available: true
        property bool autoValue: false
        width: parent.width
        spacing: 4

        readonly property real currentValue: Number(engine.filmControls[controlKey])
        readonly property bool dirty: Math.abs(currentValue - neutral) > 0.0001

        // Fixed height so the row never grows when the Reset button appears —
        // the slider below must not shift. Children are vertically centred.
        Row {
            width: parent.width
            height: 18
            InspectorLabel {
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width - valueLabel.width - resetButton.width - 8
                text: sliderRow.label
                color: sliderRow.available ? root.textSecondary : root.textMuted
                elide: Text.ElideRight
            }
            Button {
                id: resetButton
                anchors.verticalCenter: parent.verticalCenter
                visible: sliderRow.available && sliderRow.dirty
                width: visible ? 38 : 0
                height: 18
                text: "Reset"
                font.pixelSize: 10
                onClicked: engine.setFilmControl(sliderRow.controlKey, sliderRow.neutral)
                contentItem: Text { text: parent.text; color: root.textSecondary; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; font: parent.font }
                background: Rectangle { color: "transparent" }
            }
            Text {
                id: valueLabel
                anchors.verticalCenter: parent.verticalCenter
                width: sliderRow.autoValue ? 34 : 42
                text: sliderRow.autoValue ? "Auto" : ((sliderRow.bipolar && sliderRow.currentValue > 0 ? "+" : "") + (sliderRow.decimals ? sliderRow.currentValue.toFixed(2) : sliderRow.currentValue.toFixed(0)))
                color: sliderRow.available && sliderRow.dirty ? root.textPrimary : root.textValue
                horizontalAlignment: Text.AlignRight
                font.pixelSize: 12
            }
        }
        InspectorSlider {
            id: sliderControl
            width: parent.width
            enabled: sliderRow.available
            from: sliderRow.minimum
            to: sliderRow.maximum
            stepSize: sliderRow.increment
            value: sliderRow.autoValue ? sliderRow.neutral : sliderRow.currentValue
            opacity: sliderRow.available ? 1.0 : 0.35
            onMoved: engine.setFilmControl(sliderRow.controlKey, value)
        }

        HoverHandler { id: sliderHover; enabled: sliderRow.tooltip.length > 0 }
        GraphiteTip {
            parent: sliderRow
            x: 0
            y: sliderRow.height + 4
            // Hide while dragging the slider — a tooltip over a moving control is distracting.
            visible: sliderHover.hovered && !sliderControl.pressed && sliderRow.tooltip.length > 0
            text: sliderRow.tooltip
        }
    }

    // ── Library pane (left) — pinned FOLDERS only. Collapsible. Standalone only. ──
    Rectangle {
        id: libraryPane
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: filmstrip.top
        width: engine.lightroomRoundTrip ? 0 : (root.libraryOpen ? 232 : 22)
        visible: !engine.lightroomRoundTrip
        color: root.bg
        border.width: 1
        border.color: root.border

        // Collapsed: a slim rail with a reopen chevron.
        Item {
            anchors.fill: parent
            visible: !root.libraryOpen
            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.libraryOpen = true }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.top: parent.top; anchors.topMargin: 18
                text: "›"; color: root.textSecondary; font.pixelSize: 16
            }
        }

        // Expanded content.
        Column {
            anchors.fill: parent
            anchors.margins: 14
            spacing: 12
            visible: root.libraryOpen

            Item {
                width: parent.width
                height: 26
                Text {
                    anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter
                    text: "Library"; color: root.textPrimary; font.pixelSize: 14; font.weight: Font.Medium
                }
                Row {
                    anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                    spacing: 6
                    Button {
                        id: addFolderBtn
                        width: 50; height: 24
                        text: "Add"
                        onClicked: folderDialog.open()
                        contentItem: Text { text: addFolderBtn.text; color: root.textPrimary; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; font.pixelSize: 11; font.weight: Font.Medium }
                        background: Rectangle { radius: 6; color: addFolderBtn.down ? "#26262b" : "#2e2e34"; border.width: 1; border.color: root.hair }
                    }
                    Button {
                        id: libCollapseBtn
                        width: 24; height: 24
                        text: "‹"
                        onClicked: root.libraryOpen = false
                        contentItem: Text { text: libCollapseBtn.text; color: root.textSecondary; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; font.pixelSize: 16 }
                        background: Rectangle { radius: 6; color: libCollapseBtn.down ? "#20ffffff" : "transparent" }
                    }
                }
            }

            // Pinned folders (folders only — thumbnails live in the filmstrip)
            ListView {
                id: folderList
                width: parent.width
                height: parent.height - y
                clip: true
                model: library.folders
                spacing: 2
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded; width: 8 }
                delegate: Rectangle {
                    width: folderList.width
                    height: 30
                    radius: 6
                    readonly property bool selected: library.currentFolder === modelData.path
                    color: selected ? "#20ffffff" : (folderHover.hovered ? "#12ffffff" : "transparent")
                    Text {
                        anchors.left: parent.left; anchors.leftMargin: 10
                        anchors.right: rmBtn.left; anchors.verticalCenter: parent.verticalCenter
                        text: modelData.name; elide: Text.ElideMiddle
                        color: parent.selected ? root.textPrimary : root.textSecondary
                        font.pixelSize: 12
                    }
                    HoverHandler { id: folderHover }
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: library.selectFolder(modelData.path) }
                    Button {
                        id: rmBtn
                        anchors.right: parent.right; anchors.rightMargin: 4; anchors.verticalCenter: parent.verticalCenter
                        width: 22; height: 22
                        visible: folderHover.hovered
                        text: "×"
                        onClicked: library.removeFolder(modelData.path)
                        contentItem: Text { text: rmBtn.text; color: root.textMuted; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; font.pixelSize: 14 }
                        background: Rectangle { color: "transparent" }
                    }
                }
            }
        }
        // Empty-state hint
        Text {
            visible: root.libraryOpen && library.folders.length === 0
            anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
            anchors.margins: 14; anchors.topMargin: 56
            text: "Add a folder to browse your shots."
            color: root.textMuted; font.pixelSize: 11; wrapMode: Text.WordWrap
        }
    }

    // ── Filmstrip (bottom) — current folder's thumbnails. Collapsible. Standalone only. ──
    Rectangle {
        id: filmstrip
        anchors.left: parent.left
        anchors.right: inspector.left
        anchors.bottom: parent.bottom
        height: engine.lightroomRoundTrip ? 0 : (root.filmstripOpen ? 108 : 22)
        visible: !engine.lightroomRoundTrip
        color: root.bg
        border.width: 1
        border.color: root.border

        // Collapsed: a slim rail with a reopen chevron.
        Item {
            anchors.fill: parent
            visible: !root.filmstripOpen
            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.filmstripOpen = true }
            Text { anchors.centerIn: parent; text: "▴"; color: root.textSecondary; font.pixelSize: 12 }
        }

        // Expanded: thumbnails + collapse toggle.
        Item {
            anchors.fill: parent
            visible: root.filmstripOpen

            ListView {
                anchors.fill: parent
                anchors.margins: 8
                anchors.rightMargin: 28
                orientation: ListView.Horizontal
                clip: true
                spacing: 6
                model: library.files
                ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded; height: 7 }
                delegate: ThumbTile {
                    width: height * 1.4
                    height: filmstrip.height - 26
                    edge: 200
                    path: modelData.path
                    name: modelData.name
                }
            }
            Button {
                id: filmCollapseBtn
                anchors.top: parent.top; anchors.right: parent.right; anchors.margins: 4
                width: 22; height: 22
                text: "▾"
                onClicked: root.filmstripOpen = false
                contentItem: Text { text: filmCollapseBtn.text; color: root.textSecondary; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; font.pixelSize: 12 }
                background: Rectangle { radius: 6; color: filmCollapseBtn.down ? "#20ffffff" : "transparent" }
            }
            Text {
                anchors.centerIn: parent
                visible: library.files.length === 0
                text: "No images in this folder"
                color: root.textMuted; font.pixelSize: 12
            }
        }
    }

    Rectangle {
        id: previewCanvas
        anchors.left: libraryPane.right
        anchors.top: parent.top
        anchors.bottom: filmstrip.top
        anchors.right: inspector.left
        color: root.canvas
        property int compareMode: 0   // 0 = Edited, 1 = Split, 2 = Side by side

        // ── Crop tool state (Slice 1c) ──────────────────────────────────
        // While cropMode is on, the engine renders the FULL frame (crop = whole
        // image) and the overlay below lets the user draw the crop rect; the rect
        // is applied to the engine only on "Apply". cropAspect 0 = free.
        property bool cropMode: false
        property real cropAspect: 0
        property real cropX: 0
        property real cropY: 0
        property real cropW: 1
        property real cropH: 1

        function imageAspect() {
            return (afterImg.paintedHeight > 0) ? (afterImg.paintedWidth / afterImg.paintedHeight) : 1.0;
        }
        function enterCropMode() {
            cropX = engine.filmControls.crop_x;
            cropY = engine.filmControls.crop_y;
            cropW = engine.filmControls.crop_w;
            cropH = engine.filmControls.crop_h;
            compareMode = 0;
            cropMode = true;
            engine.setCrop(0, 0, 1, 1);   // show the whole frame underneath
        }
        function applyCropMode() {
            cropMode = false;
            engine.setCrop(cropX, cropY, cropW, cropH);
        }
        function selectAspect(r) {
            if (r < 0) {                    // Original — clear the crop
                cropAspect = 0;
                cropX = 0; cropY = 0; cropW = 1; cropH = 1;
                if (!cropMode) engine.setCrop(0, 0, 1, 1);
                return;
            }
            if (!cropMode) enterCropMode();
            if (r === 0) { cropAspect = 0; return; }   // free — keep current rect
            cropAspect = r;
            var A = imageAspect();
            var R = r / A;                              // normalized width/height
            var wN, hN;
            if (R >= 1) { wN = 1; hN = 1 / R; } else { hN = 1; wN = R; }
            cropW = wN; cropH = hN;
            cropX = (1 - wN) / 2; cropY = (1 - hN) / 2;
        }
        // Resize the crop rect from a handle drag. grab is a 1-or-2 char code of
        // edges (l/r/t/b); normalized pointer (nx,ny). Enforces a min size and,
        // when an aspect is locked, keeps the ratio (corner grabs only).
        function updateCrop(grab, nx, ny) {
            var minS = 0.05;
            nx = Math.max(0, Math.min(1, nx));
            ny = Math.max(0, Math.min(1, ny));
            var l = cropX, t = cropY, r = cropX + cropW, b = cropY + cropH;
            if (grab.indexOf("l") >= 0) l = Math.min(nx, r - minS);
            if (grab.indexOf("r") >= 0) r = Math.max(nx, l + minS);
            if (grab.indexOf("t") >= 0) t = Math.min(ny, b - minS);
            if (grab.indexOf("b") >= 0) b = Math.max(ny, t + minS);
            if (cropAspect > 0 && grab.length === 2) {
                var ratioN = cropAspect / imageAspect();     // normalized w/h
                var ax = (grab.indexOf("l") >= 0) ? r : l;   // anchor = opposite corner
                var ay = (grab.indexOf("t") >= 0) ? b : t;
                var wN = Math.abs(((grab.indexOf("l") >= 0) ? l : r) - ax);
                var hN = Math.abs(((grab.indexOf("t") >= 0) ? t : b) - ay);
                var w2 = Math.max(wN, hN * ratioN);
                if ((grab.indexOf("l") >= 0) && ax - w2 < 0) w2 = ax;
                if ((grab.indexOf("r") >= 0) && ax + w2 > 1) w2 = 1 - ax;
                var h2 = w2 / ratioN;
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
            anchors.fill: parent
            anchors.margins: 28
            // Reserve room for the floating compare switch so it never overlaps the
            // image (matters for tall/portrait frames that fill the height).
            anchors.topMargin: (engine.hasImage && engine.hasBefore) ? 62 : 28
            visible: engine.hasImage

            readonly property string afterSrc: engine.hasImage ? ("image://preview/frame?rev=" + engine.previewRevision) : ""
            readonly property string beforeSrc: engine.hasBefore ? ("image://preview/before?rev=" + engine.beforeRevision) : ""
            readonly property bool split: previewCanvas.compareMode === 1 && engine.hasBefore
            readonly property bool sideBySide: previewCanvas.compareMode === 2 && engine.hasBefore

            // Edited / Split — the film ("after") fills; "before" is clipped on the left.
            Image {
                id: afterImg
                anchors.fill: parent
                fillMode: Image.PreserveAspectFit
                cache: false
                source: imageArea.afterSrc
                visible: !imageArea.sideBySide
            }
            Item {
                anchors { top: parent.top; bottom: parent.bottom; left: parent.left }
                width: divider.x
                clip: true
                visible: imageArea.split
                Image {
                    width: imageArea.width
                    height: imageArea.height
                    fillMode: Image.PreserveAspectFit
                    cache: false
                    source: imageArea.beforeSrc
                }
            }
            Text {
                visible: imageArea.split
                anchors { left: parent.left; top: parent.top; margins: 6 }
                text: "Before"
                color: "#e9e9ec"
                font.pixelSize: 11
                style: Text.Outline; styleColor: "#80000000"
            }
            Item {
                id: divider
                visible: imageArea.split
                y: 0
                x: imageArea.width / 2
                width: 2
                height: imageArea.height
                Rectangle { anchors.fill: parent; color: "#e9e9ec" }
                Rectangle {
                    anchors.centerIn: parent
                    width: 28; height: 28; radius: 14
                    color: "#e9e9ec"
                    border.width: 1; border.color: "#40000000"
                    Text { anchors.centerIn: parent; text: "↔"; color: "#161618"; font.pixelSize: 14 }
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

            // Side by side — before | after.
            Row {
                anchors.fill: parent
                visible: imageArea.sideBySide
                spacing: 2
                Item {
                    width: (parent.width - 2) / 2
                    height: parent.height
                    Image { anchors.fill: parent; fillMode: Image.PreserveAspectFit; cache: false; source: imageArea.beforeSrc }
                    Text { anchors { horizontalCenter: parent.horizontalCenter; top: parent.top; topMargin: 2 } text: "Before"; color: root.textSecondary; font.pixelSize: 11 }
                }
                Item {
                    width: (parent.width - 2) / 2
                    height: parent.height
                    Image { anchors.fill: parent; fillMode: Image.PreserveAspectFit; cache: false; source: imageArea.afterSrc }
                    Text { anchors { horizontalCenter: parent.horizontalCenter; top: parent.top; topMargin: 2 } text: "After"; color: root.textSecondary; font.pixelSize: 11 }
                }
            }

            // ── Interactive crop overlay (Slice 1c) ──────────────────────
            Item {
                id: cropOverlay
                anchors.fill: parent
                visible: previewCanvas.cropMode && !imageArea.split && !imageArea.sideBySide && engine.hasImage
                // Painted image rect (PreserveAspectFit) within imageArea.
                readonly property real pw: afterImg.paintedWidth
                readonly property real ph: afterImg.paintedHeight
                readonly property real ox: (width - pw) / 2
                readonly property real oy: (height - ph) / 2
                // Crop rect in overlay/screen coords.
                readonly property real rx: ox + previewCanvas.cropX * pw
                readonly property real ry: oy + previewCanvas.cropY * ph
                readonly property real rw: previewCanvas.cropW * pw
                readonly property real rh: previewCanvas.cropH * ph
                readonly property bool free: previewCanvas.cropAspect <= 0

                // Dim the four regions outside the crop rect.
                Rectangle { color: "#99000000"; x: cropOverlay.ox; y: cropOverlay.oy; width: cropOverlay.pw; height: Math.max(0, cropOverlay.ry - cropOverlay.oy) }
                Rectangle { color: "#99000000"; x: cropOverlay.ox; y: cropOverlay.ry + cropOverlay.rh; width: cropOverlay.pw; height: Math.max(0, (cropOverlay.oy + cropOverlay.ph) - (cropOverlay.ry + cropOverlay.rh)) }
                Rectangle { color: "#99000000"; x: cropOverlay.ox; y: cropOverlay.ry; width: Math.max(0, cropOverlay.rx - cropOverlay.ox); height: cropOverlay.rh }
                Rectangle { color: "#99000000"; x: cropOverlay.rx + cropOverlay.rw; y: cropOverlay.ry; width: Math.max(0, (cropOverlay.ox + cropOverlay.pw) - (cropOverlay.rx + cropOverlay.rw)); height: cropOverlay.rh }

                // Crop frame + rule-of-thirds grid.
                Rectangle {
                    x: cropOverlay.rx; y: cropOverlay.ry; width: cropOverlay.rw; height: cropOverlay.rh
                    color: "transparent"; border.width: 1; border.color: "#f0ffffff"
                    Rectangle { color: "#40ffffff"; width: 1; height: parent.height; x: Math.round(parent.width / 3) }
                    Rectangle { color: "#40ffffff"; width: 1; height: parent.height; x: Math.round(2 * parent.width / 3) }
                    Rectangle { color: "#40ffffff"; height: 1; width: parent.width; y: Math.round(parent.height / 3) }
                    Rectangle { color: "#40ffffff"; height: 1; width: parent.width; y: Math.round(2 * parent.height / 3) }
                }

                // Handles: 4 corners always, 4 edge-midpoints only when aspect is free.
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
                    id: cropMouse
                    anchors.fill: parent
                    enabled: previewCanvas.cropMode
                    cursorShape: grab === "" ? Qt.ArrowCursor : (grab === "move" ? Qt.SizeAllCursor : Qt.CrossCursor)
                    property string grab: ""
                    property real startNx: 0
                    property real startNy: 0
                    property real startCropX: 0
                    property real startCropY: 0
                    onPressed: (m) => {
                        var hs = 16;
                        var l = cropOverlay.rx, t = cropOverlay.ry;
                        var r = cropOverlay.rx + cropOverlay.rw, b = cropOverlay.ry + cropOverlay.rh;
                        var cx = (l + r) / 2, cy = (t + b) / 2;
                        var near = function(px, py) { return Math.abs(m.x - px) <= hs && Math.abs(m.y - py) <= hs; };
                        var free = cropOverlay.free;
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
                            startCropX = previewCanvas.cropX;
                            startCropY = previewCanvas.cropY;
                        }
                    }
                    onPositionChanged: (m) => {
                        if (grab === "" || cropOverlay.pw <= 0 || cropOverlay.ph <= 0) return;
                        var nx = (m.x - cropOverlay.ox) / cropOverlay.pw;
                        var ny = (m.y - cropOverlay.oy) / cropOverlay.ph;
                        if (grab === "move") {
                            var dx = nx - startNx, dy = ny - startNy;
                            previewCanvas.cropX = Math.max(0, Math.min(1 - previewCanvas.cropW, startCropX + dx));
                            previewCanvas.cropY = Math.max(0, Math.min(1 - previewCanvas.cropH, startCropY + dy));
                        } else {
                            previewCanvas.updateCrop(grab, nx, ny);
                        }
                    }
                    onReleased: grab = ""
                }
            }
        }

        // Floating before/after mode switch (only when a "before" is available).
        Rectangle {
            visible: engine.hasImage && engine.hasBefore
            anchors.top: parent.top
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.topMargin: 16
            width: compareRow.width + 8
            height: 32
            radius: 9
            color: "#cc161618"
            border.width: 1
            border.color: root.hair
            Row {
                id: compareRow
                anchors.centerIn: parent
                spacing: 4
                Repeater {
                    model: [{ label: "Edited", m: 0 }, { label: "Split", m: 1 }, { label: "Side by side", m: 2 }]
                    delegate: Button {
                        id: cmpBtn
                        height: 26
                        width: cmpText.implicitWidth + 20
                        readonly property bool selected: previewCanvas.compareMode === modelData.m
                        onClicked: previewCanvas.compareMode = modelData.m
                        contentItem: Text { id: cmpText; text: modelData.label; color: cmpBtn.selected ? root.textPrimary : root.textSecondary; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; font.pixelSize: 11; font.weight: Font.Medium }
                        background: Rectangle {
                            radius: 7
                            color: cmpBtn.selected ? "#33333a" : "transparent"
                            border.width: cmpBtn.selected ? 1 : 0
                            border.color: root.hair
                        }
                    }
                }
            }
        }

        Column {
            anchors.centerIn: parent
            visible: !engine.hasImage
            spacing: 8

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: "DFEE"
                color: root.textPrimary
                font.pixelSize: 22
                font.weight: Font.Medium
            }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: engine.lightroomRoundTrip ? "Lightroom round-trip TIFF" : "Open a RAW or TIFF image to begin"
                color: root.textMuted
                font.pixelSize: 13
            }
        }

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: statusText.visible ? 38 : 0
            color: "#cc101114"
            visible: height > 0

            Text {
                id: statusText
                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 16
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideMiddle
                text: engine.status
                color: engine.status.startsWith("Open failed:") || engine.status.startsWith("Render failed:") || engine.status.startsWith("Export failed:") ? root.danger : root.textSecondary
                font.pixelSize: 12
                visible: text.length > 0
            }
        }
    }

    Rectangle {
        id: inspector
        property int activeTab: 0                     // 0 = Develop, 1 = Geometry, 2 = Export
        // Leaving the Geometry tab commits an in-progress crop so the overlay never
        // lingers over the preview on another tab.
        onActiveTabChanged: if (activeTab !== 1 && previewCanvas.cropMode) previewCanvas.applyCropMode()
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        width: 356
        color: root.bg                               // darker rail so the cards read as raised
        border.width: 1
        border.color: root.border

        // Fixed top: header + histogram + tab switcher (pinned, does not scroll).
        Column {
            id: inspectorTop
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.margins: 18
            spacing: 12

            Item {
                width: parent.width
                height: 32
                Text {
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    text: "Film Lab"
                    color: root.textPrimary
                    font.pixelSize: 20
                    font.weight: Font.Medium
                }
                Row {
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 8

                    Button {
                        id: resetAllBtn
                        width: visible ? 68 : 0
                        height: 27
                        // Reset is meaningless with no image; disable rather than hide it
                        // so its slot in the header never jumps around (Lightroom-style).
                        enabled: engine.hasImage
                        visible: true
                        text: "Reset"
                        onClicked: engine.resetAllEdits()
                        contentItem: Text { text: resetAllBtn.text; color: resetAllBtn.enabled ? root.textPrimary : root.textMuted; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; font.pixelSize: 12; font.weight: Font.Medium }
                        background: Rectangle {
                            radius: 7
                            color: resetAllBtn.down ? "#26262b" : "#2e2e34"
                            border.width: 1
                            border.color: root.hair
                            opacity: resetAllBtn.enabled ? 1.0 : 0.5
                            Rectangle { anchors { left: parent.left; right: parent.right; top: parent.top; margins: 1 } height: 1; radius: 1; color: "#16ffffff" }
                        }
                        HoverHandler { id: resetAllHover }
                        GraphiteTip {
                            parent: resetAllBtn
                            x: 0
                            y: resetAllBtn.height + 4
                            visible: resetAllHover.hovered && resetAllBtn.enabled
                            text: "Reset every edit — film stock, exposure and all develop controls — back to how this image first opened."
                        }
                    }

                    Button {
                        id: openBtn
                        width: visible ? 68 : 0
                        height: 27
                        visible: !engine.lightroomRoundTrip
                        text: "Open"
                        onClicked: openDialog.open()
                        contentItem: Text { text: openBtn.text; color: root.textPrimary; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; font.pixelSize: 12; font.weight: Font.Medium }
                        background: Rectangle {
                            radius: 7
                            color: openBtn.down ? "#26262b" : "#2e2e34"
                            border.width: 1
                            border.color: root.hair
                            Rectangle { anchors { left: parent.left; right: parent.right; top: parent.top; margins: 1 } height: 1; radius: 1; color: "#16ffffff" }
                        }
                    }
                }
            }

            Histogram {}

            Text {
                width: parent.width
                text: engine.lightroomRoundTrip ? "Lightroom round-trip" : (engine.hasImage ? "Native preview" : "No image loaded")
                color: root.textMuted
                font.pixelSize: 11
                elide: Text.ElideMiddle
            }

            // Tab switcher — hidden in Lightroom edit-in mode (Develop only there).
            Rectangle {
                width: parent.width
                height: 34
                radius: 9
                color: root.inset
                border.width: 1
                border.color: root.hair
                visible: !engine.lightroomRoundTrip
                Row {
                    anchors.fill: parent
                    anchors.margins: 3
                    spacing: 4
                    Repeater {
                        model: ["Develop", "Geometry", "Export"]
                        delegate: Button {
                            id: tabBtn
                            width: (parent.width - 8) / 3
                            height: parent.height
                            text: modelData
                            readonly property bool selected: inspector.activeTab === index
                            onClicked: inspector.activeTab = index
                            contentItem: Text { text: tabBtn.text; color: tabBtn.selected ? root.textPrimary : root.textSecondary; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; font.pixelSize: 12; font.weight: Font.Medium }
                            background: Rectangle {
                                radius: 7
                                color: tabBtn.selected ? "#33333a" : "transparent"
                                border.width: tabBtn.selected ? 1 : 0
                                border.color: root.hair
                                Rectangle { visible: tabBtn.selected; anchors { left: parent.left; right: parent.right; top: parent.top; margins: 1 } height: 1; radius: 1; color: "#16ffffff" }
                            }
                        }
                    }
                }
            }
        }

        Flickable {
            anchors.top: inspectorTop.bottom
            anchors.topMargin: 14
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            clip: true
            contentWidth: width
            contentHeight: controls.implicitHeight + 40
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {
                policy: ScrollBar.AsNeeded
                width: 9
                contentItem: Rectangle {
                    implicitWidth: 5
                    radius: 3
                    color: "#5a5a60"
                    opacity: parent.pressed ? 0.9 : (parent.hovered ? 0.65 : 0.4)
                }
                background: Rectangle { color: "transparent" }
            }

            Column {
                id: controls
                x: 18
                y: 4
                width: parent.width - 36
                spacing: 14

                // ── Develop tab ────────────────────────────────────────
                Column {
                    id: developContent
                    width: parent.width
                    spacing: 14
                    visible: inspector.activeTab === 0 || engine.lightroomRoundTrip

                // ── Film recipe card ───────────────────────────────────
                Rectangle {
                    id: recipeCard
                    property bool open: true
                    width: parent.width
                    radius: 14
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: "#1d1d20" }
                        GradientStop { position: 1.0; color: "#191a1c" }
                    }
                    border.width: 1
                    border.color: root.hair
                    implicitHeight: recipeCol.implicitHeight + 32
                    Rectangle { anchors { left: parent.left; right: parent.right; top: parent.top; margins: 1 } height: 1; radius: 1; color: "#12ffffff" }

                    Column {
                        id: recipeCol
                        x: 16; y: 16
                        width: parent.width - 32
                        spacing: 14

                        Item {
                            width: parent.width
                            height: 20
                            Text { anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter; text: "Film Recipe"; color: root.textPrimary; font.pixelSize: 13; font.weight: Font.Medium }
                            ChevronToggle { anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; open: recipeCard.open }
                            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: recipeCard.open = !recipeCard.open }
                        }

                        Column {
                            width: parent.width
                            spacing: 8
                            visible: recipeCard.open

                            InspectorLabel { text: "Film stock" }
                            ComboBox {
                                id: stockBox
                                width: parent.width
                                height: 52
                                model: engine.stockModel
                                textRole: "name"
                                valueRole: "id"
                                currentIndex: stockBox.indexOfValue(engine.stock)
                                onActivated: engine.stock = stockBox.currentValue

                                contentItem: Item {
                                    Row {
                                        anchors.left: parent.left
                                        anchors.leftMargin: 10
                                        anchors.right: parent.right
                                        anchors.rightMargin: 32
                                        anchors.verticalCenter: parent.verticalCenter
                                        spacing: 12
                                        BoxartSwatch {
                                            anchors.verticalCenter: parent.verticalCenter
                                            cell: 34
                                            stockId: engine.stock
                                        }
                                        Column {
                                            anchors.verticalCenter: parent.verticalCenter
                                            width: parent.width - 46
                                            spacing: 2
                                            Text {
                                                width: parent.width
                                                text: stockBox.displayText
                                                color: root.textPrimary
                                                elide: Text.ElideRight
                                                font.pixelSize: 13
                                                font.weight: Font.Medium
                                            }
                                            Text {
                                                width: parent.width
                                                text: engine.stock === "none" ? "No film stock"
                                                      : (stockBox.currentIndex >= 0 && engine.stockModel[stockBox.currentIndex]
                                                         ? engine.stockModel[stockBox.currentIndex].typeLabel : "")
                                                color: root.textMuted
                                                elide: Text.ElideRight
                                                font.pixelSize: 11
                                            }
                                        }
                                    }
                                }
                                background: Rectangle {
                                    radius: 8
                                    color: root.inset
                                    border.width: 1
                                    border.color: stockBox.activeFocus ? root.border : root.hair
                                }
                                indicator: ChevronToggle {
                                    x: stockBox.width - 24
                                    y: (stockBox.height - 7) / 2
                                    open: false
                                }
                                popup: Popup {
                                    y: stockBox.height + 4
                                    width: stockBox.width
                                    implicitHeight: Math.min(contentItem.implicitHeight + 8, 320)
                                    padding: 4
                                    contentItem: ListView {
                                        clip: true
                                        implicitHeight: contentHeight
                                        model: stockBox.popup.visible ? stockBox.delegateModel : null
                                        currentIndex: stockBox.highlightedIndex
                                        ScrollIndicator.vertical: ScrollIndicator { }
                                        section.property: "typeLabel"
                                        section.criteria: ViewSection.FullString
                                        section.delegate: Item {
                                            width: ListView.view.width
                                            height: section === "" ? 0 : 24
                                            visible: section !== ""
                                            Text {
                                                anchors.left: parent.left
                                                anchors.leftMargin: 10
                                                anchors.bottom: parent.bottom
                                                anchors.bottomMargin: 4
                                                text: section
                                                color: root.textMuted
                                                font.pixelSize: 10
                                                font.weight: Font.Medium
                                            }
                                        }
                                    }
                                    background: Rectangle {
                                        radius: 10
                                        color: root.panelRaised
                                        border.width: 1
                                        border.color: root.border
                                    }
                                }
                                delegate: ItemDelegate {
                                    id: stockItem
                                    width: stockBox.width - 8
                                    height: 50
                                    highlighted: stockBox.highlightedIndex === index
                                    contentItem: Row {
                                        leftPadding: 6
                                        spacing: 12
                                        BoxartSwatch {
                                            anchors.verticalCenter: parent.verticalCenter
                                            cell: 32
                                            stockId: modelData.id
                                        }
                                        Text {
                                            anchors.verticalCenter: parent.verticalCenter
                                            text: modelData.name
                                            color: root.textPrimary
                                            elide: Text.ElideRight
                                            font.pixelSize: 13
                                        }
                                    }
                                    background: Rectangle {
                                        radius: 8
                                        color: stockItem.highlighted ? "#16ffffff" : "transparent"
                                    }
                                }
                            }
                        }
                    }
                }

                // ── Print finish card ──────────────────────────────────
                Rectangle {
                    id: printCard
                    property bool open: false
                    readonly property bool active: engine.filmControls.print_stock !== undefined && engine.filmControls.print_stock !== "none"
                    width: parent.width
                    radius: 14
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: "#1d1d20" }
                        GradientStop { position: 1.0; color: "#191a1c" }
                    }
                    border.width: 1
                    border.color: root.hair
                    implicitHeight: printCol.implicitHeight + 32
                    Rectangle { anchors { left: parent.left; right: parent.right; top: parent.top; margins: 1 } height: 1; radius: 1; color: "#12ffffff" }

                    Column {
                        id: printCol
                        x: 16; y: 16
                        width: parent.width - 32
                        spacing: 14

                        Item {
                            width: parent.width
                            height: 20
                            Text { anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter; text: "Print Finish"; color: root.textPrimary; font.pixelSize: 13; font.weight: Font.Medium }
                            Text { anchors.right: chev1.left; anchors.rightMargin: 8; anchors.verticalCenter: parent.verticalCenter; visible: printCard.active && !printCard.open; text: "On"; color: root.textValue; font.pixelSize: 11 }
                            ChevronToggle { id: chev1; anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; open: printCard.open }
                            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: printCard.open = !printCard.open }
                        }

                        Column {
                            width: parent.width
                            spacing: 12
                            visible: printCard.open

                            InspectorLabel { text: "Print stock" }
                            ComboBox {
                                id: printBox
                                width: parent.width
                                height: 38
                                model: engine.printStockNames
                                currentIndex: {
                                    for (var i = 0; i < engine.printStockNames.length; ++i)
                                        if (engine.printStockIdAt(i) === engine.filmControls.print_stock) return i;
                                    return 0;
                                }
                                onActivated: engine.setFilmControl("print_stock", engine.printStockIdAt(currentIndex))
                                contentItem: Text {
                                    leftPadding: 12; rightPadding: 32
                                    text: printBox.displayText
                                    color: root.textPrimary
                                    verticalAlignment: Text.AlignVCenter
                                    elide: Text.ElideRight
                                    font.pixelSize: 13
                                }
                                background: Rectangle {
                                    radius: 8
                                    color: root.inset
                                    border.width: 1
                                    border.color: printBox.activeFocus ? root.border : root.hair
                                }
                                indicator: ChevronToggle { x: printBox.width - 24; y: (printBox.height - 7) / 2; open: false }
                                popup: Popup {
                                    y: printBox.height + 4
                                    width: printBox.width
                                    implicitHeight: Math.min(contentItem.implicitHeight + 8, 300)
                                    padding: 4
                                    contentItem: ListView {
                                        clip: true
                                        implicitHeight: contentHeight
                                        model: printBox.popup.visible ? printBox.delegateModel : null
                                        currentIndex: printBox.highlightedIndex
                                        ScrollIndicator.vertical: ScrollIndicator { }
                                    }
                                    background: Rectangle { radius: 10; color: root.panelRaised; border.width: 1; border.color: root.border }
                                }
                                delegate: ItemDelegate {
                                    id: printItem
                                    width: printBox.width - 8
                                    height: 36
                                    highlighted: printBox.highlightedIndex === index
                                    contentItem: Text { leftPadding: 8; text: modelData; color: root.textPrimary; verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight; font.pixelSize: 13 }
                                    background: Rectangle { radius: 8; color: printItem.highlighted ? "#16ffffff" : "transparent" }
                                }
                            }

                            Column {
                                width: parent.width
                                spacing: 12
                                visible: printCard.active
                                opacity: printCard.active ? 1.0 : 0.4
                                FilmSlider { controlKey: "print_strength"; label: "Print strength"; minimum: 0; maximum: 2; increment: 0.05; decimals: true; neutral: 1.0; tooltip: "How strongly the print-stock emulation is applied over the negative." }
                                FilmSlider { controlKey: "print_c"; label: "Color head: cyan"; minimum: -100; maximum: 100; bipolar: true; tooltip: "Enlarger color head — subtractive cyan filtration (removes red). Forward cools the print." }
                                FilmSlider { controlKey: "print_m"; label: "Color head: magenta"; minimum: -100; maximum: 100; bipolar: true; tooltip: "Enlarger color head — subtractive magenta filtration (removes green)." }
                                FilmSlider { controlKey: "print_y"; label: "Color head: yellow"; minimum: -100; maximum: 100; bipolar: true; tooltip: "Enlarger color head — subtractive yellow filtration (removes blue). Forward warms the print." }
                                FilmSlider { controlKey: "print_contrast"; label: "Print contrast"; minimum: -100; maximum: 100; bipolar: true; tooltip: "Steepness of the print's tone curve — the paper grade." }
                                FilmSlider { controlKey: "print_black_point"; label: "Black point (lift)"; minimum: -100; maximum: 100; bipolar: true; tooltip: "Print base density — lifts or deepens the darkest blacks of the print." }
                            }
                        }
                    }
                }

                // ── Film exposure card ─────────────────────────────────
                Rectangle {
                    id: exposureCard
                    property bool open: true
                    width: parent.width
                    radius: 14
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: "#1d1d20" }
                        GradientStop { position: 1.0; color: "#191a1c" }
                    }
                    border.width: 1
                    border.color: root.hair
                    implicitHeight: exposureCol.implicitHeight + 32
                    Rectangle { anchors { left: parent.left; right: parent.right; top: parent.top; margins: 1 } height: 1; radius: 1; color: "#12ffffff" }

                    Column {
                        id: exposureCol
                        x: 16; y: 16
                        width: parent.width - 32
                        spacing: 14

                        Item {
                            width: parent.width
                            height: 20
                            Text { anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter; text: "Film Exposure"; color: root.textPrimary; font.pixelSize: 13; font.weight: Font.Medium }
                            ChevronToggle { anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; open: exposureCard.open }
                            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: exposureCard.open = !exposureCard.open }
                        }

                        Column {
                            width: parent.width
                            spacing: 12
                            visible: exposureCard.open

                            InspectorLabel { text: "Scene placement" }
                            Rectangle {                              // recessed segmented track
                                id: placementTrack
                                width: parent.width
                                height: 36
                                radius: 9
                                color: root.inset
                                border.width: 1
                                border.color: root.hair
                                HoverHandler { id: placementHover }
                                GraphiteTip {
                                    parent: placementTrack
                                    x: 0
                                    y: placementTrack.height + 4
                                    visible: placementHover.hovered
                                    text: "Auto balanced sets a stock-aware starting exposure for the scene. As shot preserves the RAW's own exposure placement before the film response."
                                }
                                Row {
                                    anchors.fill: parent
                                    anchors.margins: 3
                                    spacing: 4
                                    Repeater {
                                        model: [{ label: "Auto balanced", value: "auto_balanced" }, { label: "As shot", value: "as_shot" }]
                                        delegate: Button {
                                            id: segBtn
                                            width: (parent.width - 4) / 2
                                            height: parent.height
                                            text: modelData.label
                                            readonly property bool selected: engine.filmControls.exposure_placement === modelData.value
                                            onClicked: engine.setFilmControl("exposure_placement", modelData.value)
                                            contentItem: Text { text: segBtn.text; color: segBtn.selected ? root.textPrimary : root.textSecondary; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; font.pixelSize: 11; font.weight: Font.Medium }
                                            background: Rectangle {
                                                radius: 7
                                                color: segBtn.selected ? "#33333a" : "transparent"
                                                border.width: segBtn.selected ? 1 : 0
                                                border.color: root.hair
                                                Rectangle { visible: segBtn.selected; anchors { left: parent.left; right: parent.right; top: parent.top; margins: 1 } height: 1; radius: 1; color: "#16ffffff" }
                                            }
                                        }
                                    }
                                }
                            }
                            FilmSlider { controlKey: "film_exposure_ev"; label: "Film exposure"; minimum: -3; maximum: 3; increment: 0.05; decimals: true; bipolar: true; tooltip: "Virtual exposure (in stops) reaching the film before its tone and color response — like rating the stock faster or slower." }
                        }
                    }
                }

                // ── Film tone card ─────────────────────────────────────
                Rectangle {
                    id: toneCard
                    property bool open: true
                    width: parent.width
                    radius: 14
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: "#1d1d20" }
                        GradientStop { position: 1.0; color: "#191a1c" }
                    }
                    border.width: 1
                    border.color: root.hair
                    implicitHeight: toneCol.implicitHeight + 32
                    Rectangle { anchors { left: parent.left; right: parent.right; top: parent.top; margins: 1 } height: 1; radius: 1; color: "#12ffffff" }

                    Column {
                        id: toneCol
                        x: 16; y: 16
                        width: parent.width - 32
                        spacing: 14

                        Item {
                            width: parent.width
                            height: 20
                            Text { anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter; text: "Film Tone"; color: root.textPrimary; font.pixelSize: 13; font.weight: Font.Medium }
                            ChevronToggle { anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; open: toneCard.open }
                            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: toneCard.open = !toneCard.open }
                        }

                        Column {
                            width: parent.width
                            spacing: 12
                            visible: toneCard.open

                            // Applies before the film tone, and only to already-rendered
                            // (TIFF) inputs — disabled for RAW, Lightroom-style.
                            FilmSlider {
                                controlKey: "rendered_input"; label: "Preserve rendered tone"
                                minimum: 0; maximum: 100; neutral: 80
                                available: engine.renderedInput
                                tooltip: "For files that are already developed (TIFF/JPEG, e.g. sent from Lightroom): higher keeps the file's existing exposure and tone and applies the film look gently, protecting skies and bright highlights from being re-pushed. Lower treats it like a RAW and applies the full film tone. Has no effect on RAW files."
                            }
                            GraphiteCheck {
                                caption: "Adaptive scene tone"
                                checked: engine.filmControls.adaptive
                                onToggled: engine.setFilmControl("adaptive", checked)
                                tooltip: "Lets the film read the scene and auto-adjust its tone for flat, high-dynamic-range files. Turn off for a fixed, predictable response."
                            }
                            FilmSlider { controlKey: "profile_strength"; label: "Film profile strength"; minimum: 0; maximum: 200; neutral: 100; tooltip: "Master strength of this stock's authored tone curve. At 100, you get the stock's intended baseline; lower softens its toe, midtones, and shoulder together, while higher reinforces them within that stock's safe limits." }
                            FilmSlider { controlKey: "highlight_rolloff"; label: "Highlight rolloff"; minimum: 0; maximum: 200; neutral: 100; tooltip: "How gently the brightest tones roll off instead of clipping — higher for softer, glowier film highlights." }
                            FilmSlider { controlKey: "film_contrast"; label: "Film contrast"; minimum: 0; maximum: 200; neutral: 100; tooltip: "The punch of the film's tone curve — higher for a deeper, more contrasty look; lower for flatter." }
                            FilmSlider { controlKey: "shadow_lift"; label: "Shadow lift"; minimum: -100; maximum: 100; bipolar: true; tooltip: "Base-fog fade in the deepest shadows, the way negative film never quite reaches pure black. Forward lifts shadows into a soft matte; back deepens them toward true black." }
                        }
                    }
                }

                // ── Color character card ───────────────────────────────
                Rectangle {
                    id: colorCard
                    property bool open: true
                    width: parent.width
                    radius: 14
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: "#1d1d20" }
                        GradientStop { position: 1.0; color: "#191a1c" }
                    }
                    border.width: 1
                    border.color: root.hair
                    implicitHeight: colorCol.implicitHeight + 32
                    Rectangle { anchors { left: parent.left; right: parent.right; top: parent.top; margins: 1 } height: 1; radius: 1; color: "#12ffffff" }

                    Column {
                        id: colorCol
                        x: 16; y: 16
                        width: parent.width - 32
                        spacing: 14

                        Item {
                            width: parent.width
                            height: 20
                            Text { anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter; text: "Color Character"; color: root.textPrimary; font.pixelSize: 13; font.weight: Font.Medium }
                            ChevronToggle { anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; open: colorCard.open }
                            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: colorCard.open = !colorCard.open }
                        }

                        Column {
                            width: parent.width
                            spacing: 12
                            visible: colorCard.open
                            opacity: engine.currentStockMonochrome ? 0.45 : 1.0

                            Text { visible: engine.currentStockMonochrome; text: "Unavailable for monochrome stocks"; color: root.textMuted; font.pixelSize: 11 }
                            FilmSlider { controlKey: "film_color_density"; label: "Color density"; minimum: 0; maximum: 200; neutral: 100; available: !engine.currentStockMonochrome; tooltip: "How dense and cohesive the film's colors are — forward for richer, deeper, more film-like color; back for a thinner, more digital look." }
                            FilmSlider { controlKey: "emulsion_color_density"; label: "Color boost"; minimum: -100; maximum: 100; bipolar: true; available: !engine.currentStockMonochrome; tooltip: "Overall saturation of the stock's color dyes — forward for punchier color, back for a muted look." }
                            FilmSlider { controlKey: "highlight_color_hold"; label: "Highlight saturation"; minimum: -100; maximum: 100; bipolar: true; available: !engine.currentStockMonochrome; tooltip: "How much color survives in the highlights — back bleaches bright areas toward clean white (rescues blown, over-warm highlights)." }
                            FilmSlider { controlKey: "shadow_color_retention"; label: "Shadow saturation"; minimum: -100; maximum: 100; bipolar: true; available: !engine.currentStockMonochrome; tooltip: "How much color survives in the shadows — forward keeps darks colorful, back mutes them toward neutral." }
                            FilmSlider { controlKey: "cg_crossbalance"; label: "Film crossbalance"; minimum: -100; maximum: 100; bipolar: true; available: !engine.currentStockMonochrome; tooltip: "One-knob split-tone: forward for teal shadows and warm highlights, back for the inverse." }
                        }
                    }
                }

                // ── Color balance card ────────────────────────────────
                Rectangle {
                    id: colorBalanceCard
                    property bool open: false
                    width: parent.width
                    radius: 14
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: "#1d1d20" }
                        GradientStop { position: 1.0; color: "#191a1c" }
                    }
                    border.width: 1
                    border.color: root.hair
                    implicitHeight: colorBalanceCol.implicitHeight + 32
                    Rectangle { anchors { left: parent.left; right: parent.right; top: parent.top; margins: 1 } height: 1; radius: 1; color: "#12ffffff" }

                    Column {
                        id: colorBalanceCol
                        x: 16; y: 16
                        width: parent.width - 32
                        spacing: 14

                        Item {
                            width: parent.width
                            height: 20
                            Text { anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter; text: "Color Balance"; color: root.textPrimary; font.pixelSize: 13; font.weight: Font.Medium }
                            ChevronToggle { anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; open: colorBalanceCard.open }
                            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: colorBalanceCard.open = !colorBalanceCard.open }
                        }

                        Column {
                            width: parent.width
                            spacing: 12
                            visible: colorBalanceCard.open
                            FilmSlider { controlKey: "temp"; label: "Temperature"; minimum: -100; maximum: 100; bipolar: true; tooltip: "White balance warmth — forward warms (more amber), back cools (more blue)." }
                            FilmSlider { controlKey: "tint"; label: "Tint"; minimum: -100; maximum: 100; bipolar: true; tooltip: "White balance green/magenta — forward toward magenta, back toward green." }
                            FilmSlider { controlKey: "vibrance"; label: "Vibrance"; minimum: -100; maximum: 100; bipolar: true; tooltip: "Smart saturation that protects skin tones and already-saturated colors." }
                            FilmSlider { controlKey: "saturation"; label: "Saturation"; minimum: -100; maximum: 100; bipolar: true; tooltip: "Overall color intensity, applied evenly to all hues." }
                        }
                    }
                }

                // ── Light card (basic tone) ────────────────────────────
                Rectangle {
                    id: lightCard
                    property bool open: false
                    width: parent.width
                    radius: 14
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: "#1d1d20" }
                        GradientStop { position: 1.0; color: "#191a1c" }
                    }
                    border.width: 1
                    border.color: root.hair
                    implicitHeight: lightCol.implicitHeight + 32
                    Rectangle { anchors { left: parent.left; right: parent.right; top: parent.top; margins: 1 } height: 1; radius: 1; color: "#12ffffff" }

                    Column {
                        id: lightCol
                        x: 16; y: 16
                        width: parent.width - 32
                        spacing: 14

                        Item {
                            width: parent.width
                            height: 20
                            Text { anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter; text: "Light"; color: root.textPrimary; font.pixelSize: 13; font.weight: Font.Medium }
                            ChevronToggle { anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; open: lightCard.open }
                            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: lightCard.open = !lightCard.open }
                        }

                        Column {
                            width: parent.width
                            spacing: 12
                            visible: lightCard.open
                            FilmSlider { controlKey: "exposure"; label: "Exposure"; minimum: -3; maximum: 3; increment: 0.05; decimals: true; bipolar: true; tooltip: "Overall brightness of the finished image, in stops — a grade applied after the film response. For the film's own exposure (which drives its tone and rolloff), use Film exposure in the Film Recipe." }
                            FilmSlider { controlKey: "contrast"; label: "Contrast"; minimum: -100; maximum: 100; bipolar: true; tooltip: "Global contrast — spreads or compresses the tonal range around the midtones." }
                            FilmSlider { controlKey: "highlights"; label: "Highlights"; minimum: -100; maximum: 100; bipolar: true; tooltip: "Recovers or brightens the brighter tones without moving whites." }
                            FilmSlider { controlKey: "shadows"; label: "Shadows"; minimum: -100; maximum: 100; bipolar: true; tooltip: "Opens or deepens the darker tones without moving blacks." }
                            FilmSlider { controlKey: "whites"; label: "Whites"; minimum: -100; maximum: 100; bipolar: true; tooltip: "Sets the white clipping point — how bright the brightest tones become." }
                            FilmSlider { controlKey: "blacks"; label: "Blacks"; minimum: -100; maximum: 100; bipolar: true; tooltip: "Sets the black clipping point — how deep the darkest tones become." }
                            FilmSlider { controlKey: "midtones"; label: "Midtones"; minimum: -100; maximum: 100; bipolar: true; tooltip: "Brightness of the mid-tones, leaving the extremes anchored." }
                        }
                    }
                }

                // ── Detail card ────────────────────────────────────────
                Rectangle {
                    id: detailCard
                    property bool open: false
                    width: parent.width
                    radius: 14
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: "#1d1d20" }
                        GradientStop { position: 1.0; color: "#191a1c" }
                    }
                    border.width: 1
                    border.color: root.hair
                    implicitHeight: detailCol.implicitHeight + 32
                    Rectangle { anchors { left: parent.left; right: parent.right; top: parent.top; margins: 1 } height: 1; radius: 1; color: "#12ffffff" }

                    Column {
                        id: detailCol
                        x: 16; y: 16
                        width: parent.width - 32
                        spacing: 14

                        Item {
                            width: parent.width
                            height: 20
                            Text { anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter; text: "Detail"; color: root.textPrimary; font.pixelSize: 13; font.weight: Font.Medium }
                            ChevronToggle { anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; open: detailCard.open }
                            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: detailCard.open = !detailCard.open }
                        }

                        Column {
                            width: parent.width
                            spacing: 12
                            visible: detailCard.open
                            FilmSlider { controlKey: "texture"; label: "Texture"; minimum: -100; maximum: 100; bipolar: true; tooltip: "Medium-scale detail like skin and foliage — forward enhances, back smooths." }
                            FilmSlider { controlKey: "clarity"; label: "Clarity"; minimum: -100; maximum: 100; bipolar: true; tooltip: "Midtone local contrast — forward adds punch and presence, back softens." }
                            FilmSlider { controlKey: "dehaze"; label: "Dehaze"; minimum: -100; maximum: 100; bipolar: true; tooltip: "Cuts or adds atmospheric haze and low-contrast veiling." }
                            FilmSlider { controlKey: "sharpness"; label: "Detail sharpness"; minimum: 0; maximum: 2; increment: 0.05; decimals: true; tooltip: "Edge sharpening amount." }
                            FilmSlider { controlKey: "sharpness_mask"; label: "Luminance mask"; minimum: 0; maximum: 1; increment: 0.05; decimals: true; neutral: 0.5; tooltip: "Limits sharpening to edges, protecting smooth areas (like skies) from being sharpened into noise." }
                        }
                    }
                }

                // ── HSL card ───────────────────────────────────────────
                Rectangle {
                    id: hslCard
                    property bool open: false
                    property string suffix: "h"
                    width: parent.width
                    radius: 14
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: "#1d1d20" }
                        GradientStop { position: 1.0; color: "#191a1c" }
                    }
                    border.width: 1
                    border.color: root.hair
                    implicitHeight: hslCol.implicitHeight + 32
                    Rectangle { anchors { left: parent.left; right: parent.right; top: parent.top; margins: 1 } height: 1; radius: 1; color: "#12ffffff" }

                    Column {
                        id: hslCol
                        x: 16; y: 16
                        width: parent.width - 32
                        spacing: 14

                        Item {
                            width: parent.width
                            height: 20
                            Text { anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter; text: "HSL"; color: root.textPrimary; font.pixelSize: 13; font.weight: Font.Medium }
                            ChevronToggle { anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; open: hslCard.open }
                            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: hslCard.open = !hslCard.open }
                        }

                        Column {
                            width: parent.width
                            spacing: 12
                            visible: hslCard.open

                            Rectangle {                          // H/S/L tab track
                                width: parent.width
                                height: 34
                                radius: 9
                                color: root.inset
                                border.width: 1
                                border.color: root.hair
                                Row {
                                    anchors.fill: parent
                                    anchors.margins: 3
                                    spacing: 4
                                    Repeater {
                                        model: [{ label: "Hue", v: "h" }, { label: "Saturation", v: "s" }, { label: "Luminance", v: "l" }]
                                        delegate: Button {
                                            id: hslTabBtn
                                            width: (parent.width - 8) / 3
                                            height: parent.height
                                            text: modelData.label
                                            readonly property bool selected: hslCard.suffix === modelData.v
                                            onClicked: hslCard.suffix = modelData.v
                                            contentItem: Text { text: hslTabBtn.text; color: hslTabBtn.selected ? root.textPrimary : root.textSecondary; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; font.pixelSize: 11; font.weight: Font.Medium }
                                            background: Rectangle {
                                                radius: 7
                                                color: hslTabBtn.selected ? "#33333a" : "transparent"
                                                border.width: hslTabBtn.selected ? 1 : 0
                                                border.color: root.hair
                                                Rectangle { visible: hslTabBtn.selected; anchors { left: parent.left; right: parent.right; top: parent.top; margins: 1 } height: 1; radius: 1; color: "#16ffffff" }
                                            }
                                        }
                                    }
                                }
                            }

                            Repeater {
                                model: [
                                    { key: "red", label: "Red", dot: "#f25c5c" },
                                    { key: "orange", label: "Orange", dot: "#f2944a" },
                                    { key: "yellow", label: "Yellow", dot: "#d4c62a" },
                                    { key: "green", label: "Green", dot: "#4db858" },
                                    { key: "aqua", label: "Aqua", dot: "#38c0c0" },
                                    { key: "blue", label: "Blue", dot: "#4a85e8" },
                                    { key: "purple", label: "Purple", dot: "#9b5de5" },
                                    { key: "magenta", label: "Magenta", dot: "#d44fa8" }
                                ]
                                delegate: FilmSlider {
                                    controlKey: "hsl_" + modelData.key + "_" + hslCard.suffix
                                    label: modelData.label
                                    minimum: -100; maximum: 100; bipolar: true
                                }
                            }
                        }
                    }
                }

                // ── Color grading card ────────────────────────────────
                Rectangle {
                    id: gradeCard
                    property bool open: false
                    width: parent.width
                    radius: 14
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: "#1d1d20" }
                        GradientStop { position: 1.0; color: "#191a1c" }
                    }
                    border.width: 1
                    border.color: root.hair
                    implicitHeight: gradeCol.implicitHeight + 32
                    Rectangle { anchors { left: parent.left; right: parent.right; top: parent.top; margins: 1 } height: 1; radius: 1; color: "#12ffffff" }

                    Column {
                        id: gradeCol
                        x: 16; y: 16
                        width: parent.width - 32
                        spacing: 14

                        Item {
                            width: parent.width
                            height: 20
                            Text { anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter; text: "Color Grading"; color: root.textPrimary; font.pixelSize: 13; font.weight: Font.Medium }
                            ChevronToggle { anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; open: gradeCard.open }
                            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: gradeCard.open = !gradeCard.open }
                        }

                        Column {
                            width: parent.width
                            spacing: 14
                            visible: gradeCard.open

                            Grid {
                                anchors.horizontalCenter: parent.horizontalCenter
                                columns: 2
                                columnSpacing: 26
                                rowSpacing: 14
                                ColorWheel { zone: "shadow"; label: "Shadows" }
                                ColorWheel { zone: "midtone"; label: "Midtones" }
                                ColorWheel { zone: "highlight"; label: "Highlights" }
                                ColorWheel { zone: "global"; label: "Global" }
                            }

                            FilmSlider { controlKey: "cg_shadow_lum"; label: "Shadow luminance"; minimum: -100; maximum: 100; bipolar: true; tooltip: "Brightness of the shadow zone only." }
                            FilmSlider { controlKey: "cg_midtone_lum"; label: "Midtone luminance"; minimum: -100; maximum: 100; bipolar: true; tooltip: "Brightness of the midtone zone only." }
                            FilmSlider { controlKey: "cg_highlight_lum"; label: "Highlight luminance"; minimum: -100; maximum: 100; bipolar: true; tooltip: "Brightness of the highlight zone only." }
                            FilmSlider { controlKey: "cg_global_lum"; label: "Global luminance"; minimum: -100; maximum: 100; bipolar: true; tooltip: "Overall brightness applied by the grade." }

                            InspectorLabel { text: "Grade" }
                            FilmSlider { controlKey: "cg_balance"; label: "Balance"; minimum: -100; maximum: 100; bipolar: true; tooltip: "Shifts where shadows end and highlights begin, weighting the grade toward darks or lights." }
                            FilmSlider { controlKey: "cg_blending"; label: "Blending"; minimum: 0; maximum: 100; tooltip: "How softly the shadow, midtone and highlight zones overlap." }
                        }
                    }
                }

                // ── Material finish card ───────────────────────────────
                Rectangle {
                    id: materialCard
                    property bool open: true
                    width: parent.width
                    radius: 14
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: "#1d1d20" }
                        GradientStop { position: 1.0; color: "#191a1c" }
                    }
                    border.width: 1
                    border.color: root.hair
                    implicitHeight: materialCol.implicitHeight + 32
                    Rectangle { anchors { left: parent.left; right: parent.right; top: parent.top; margins: 1 } height: 1; radius: 1; color: "#12ffffff" }

                    Column {
                        id: materialCol
                        x: 16; y: 16
                        width: parent.width - 32
                        spacing: 14

                        Item {
                            width: parent.width
                            height: 20
                            Text { anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter; text: "Material Finish"; color: root.textPrimary; font.pixelSize: 13; font.weight: Font.Medium }
                            ChevronToggle { anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; open: materialCard.open }
                            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: materialCard.open = !materialCard.open }
                        }

                        Column {
                            width: parent.width
                            spacing: 12
                            visible: materialCard.open

                            InspectorLabel { text: "Grain" }
                            GraphiteCheck {
                                caption: engine.grainResolving ? "Resolving stock grain" : "Match grain to film speed"
                                checked: engine.filmControls.grain_auto
                                enabled: !engine.grainResolving
                                onToggled: engine.setAutoGrain(checked)
                                tooltip: "Automatically matches grain to the film speed (ISO) and stock. Turn off to seed and edit Strength, Size and Roughness manually."
                            }
                            FilmSlider { controlKey: "grain_strength"; label: "Strength"; minimum: 0; maximum: 2; increment: 0.05; decimals: true; available: !engine.filmControls.grain_auto; autoValue: engine.filmControls.grain_auto; tooltip: "How visible the grain is — the apparent film speed." }
                            FilmSlider { controlKey: "grain_size"; label: "Size"; minimum: 0.1; maximum: 2; increment: 0.05; decimals: true; neutral: 0.6; available: !engine.filmControls.grain_auto; autoValue: engine.filmControls.grain_auto; tooltip: "Particle size — larger reads as a coarser, higher-ISO stock." }
                            FilmSlider { controlKey: "grain_roughness"; label: "Roughness"; minimum: 0; maximum: 1; increment: 0.05; decimals: true; neutral: 0.5; available: !engine.filmControls.grain_auto; autoValue: engine.filmControls.grain_auto; tooltip: "Irregularity of the grain clumping — higher is grittier and more organic, lower is finer and more even." }
                            InspectorLabel { text: "Halation" }
                            FilmSlider { controlKey: "halation_strength"; label: "Strength"; minimum: 0; maximum: 200; neutral: 100; tooltip: "Strength of the warm red-orange glow that bleeds around bright edges against dark backgrounds." }
                            FilmSlider { controlKey: "halation_threshold"; label: "Threshold"; minimum: 0; maximum: 100; neutral: 50; tooltip: "How bright an area must be before it starts to halate — higher restricts the glow to the brightest highlights." }
                            InspectorLabel { text: "Bloom" }
                            FilmSlider { controlKey: "bloom"; label: "Amount"; minimum: 0; maximum: 100; tooltip: "Soft optical glow spreading from the highlights, like light diffusing in the lens." }
                        }
                    }
                }
                }

                // ── Geometry tab ───────────────────────────────────────
                Column {
                    id: geometryContent
                    width: parent.width
                    spacing: 14
                    visible: inspector.activeTab === 1 && !engine.lightroomRoundTrip

                    // ── Crop card ──────────────────────────────────────
                    Rectangle {
                        id: cropCard
                        property bool open: true
                        width: parent.width
                        radius: 14
                        gradient: Gradient {
                            GradientStop { position: 0.0; color: "#1d1d20" }
                            GradientStop { position: 1.0; color: "#191a1c" }
                        }
                        border.width: 1
                        border.color: root.hair
                        implicitHeight: cropCol.implicitHeight + 32
                        Rectangle { anchors { left: parent.left; right: parent.right; top: parent.top; margins: 1 } height: 1; radius: 1; color: "#12ffffff" }

                        Column {
                            id: cropCol
                            x: 16; y: 16
                            width: parent.width - 32
                            spacing: 14

                            Item {
                                width: parent.width
                                height: 20
                                Text { anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter; text: "Crop"; color: root.textPrimary; font.pixelSize: 13; font.weight: Font.Medium }
                                ChevronToggle { anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; open: cropCard.open }
                                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: cropCard.open = !cropCard.open }
                            }

                            Column {
                                width: parent.width
                                spacing: 12
                                visible: cropCard.open

                                InspectorLabel { text: "Aspect ratio" }
                                ComboBox {
                                    id: aspectBox
                                    width: parent.width
                                    height: 34
                                    textRole: "label"
                                    model: [
                                        { label: "Original", r: -1 },
                                        { label: "Free", r: 0 },
                                        { label: "1:1", r: 1 },
                                        { label: "3:2", r: 1.5 },
                                        { label: "2:3", r: 0.6666667 },
                                        { label: "4:5", r: 0.8 },
                                        { label: "5:4", r: 1.25 },
                                        { label: "16:9", r: 1.7777778 },
                                        { label: "9:16", r: 0.5625 }
                                    ]
                                    onActivated: previewCanvas.selectAspect(model[currentIndex].r)
                                    contentItem: Text { leftPadding: 10; text: aspectBox.displayText; color: root.textPrimary; verticalAlignment: Text.AlignVCenter; font.pixelSize: 12 }
                                    background: Rectangle { radius: 7; color: "#26262b"; border.width: 1; border.color: root.hair }
                                }

                                GeoButton {
                                    width: parent.width
                                    text: previewCanvas.cropMode ? "Apply crop" : "Crop image"
                                    active: previewCanvas.cropMode
                                    onClicked: previewCanvas.cropMode ? previewCanvas.applyCropMode() : previewCanvas.enterCropMode()
                                    tooltip: "Draw a crop on the image — drag the handles or inside to reposition. Pick an aspect ratio to lock the shape."
                                }
                                Text {
                                    width: parent.width
                                    visible: previewCanvas.cropMode
                                    text: "Drag the handles to crop; drag inside to move. Click Apply crop when done."
                                    color: root.textMuted
                                    font.pixelSize: 11
                                    wrapMode: Text.WordWrap
                                }
                            }
                        }
                    }

                    // ── Rotate & Flip card ─────────────────────────────
                    Rectangle {
                        id: orientCard
                        property bool open: true
                        width: parent.width
                        radius: 14
                        gradient: Gradient {
                            GradientStop { position: 0.0; color: "#1d1d20" }
                            GradientStop { position: 1.0; color: "#191a1c" }
                        }
                        border.width: 1
                        border.color: root.hair
                        implicitHeight: orientCol.implicitHeight + 32
                        Rectangle { anchors { left: parent.left; right: parent.right; top: parent.top; margins: 1 } height: 1; radius: 1; color: "#12ffffff" }

                        Column {
                            id: orientCol
                            x: 16; y: 16
                            width: parent.width - 32
                            spacing: 14

                            Item {
                                width: parent.width
                                height: 20
                                Text { anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter; text: "Rotate & Flip"; color: root.textPrimary; font.pixelSize: 13; font.weight: Font.Medium }
                                ChevronToggle { anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; open: orientCard.open }
                                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: orientCard.open = !orientCard.open }
                            }

                            Column {
                                width: parent.width
                                spacing: 12
                                visible: orientCard.open

                                FilmSlider { controlKey: "straighten_deg"; label: "Straighten"; minimum: -45; maximum: 45; increment: 0.1; decimals: true; bipolar: true; tooltip: "Level a tilted horizon — rotates by a fine angle and trims the corners so there's no black edge." }

                                InspectorLabel { text: "Rotate" }
                                Row {
                                    width: parent.width
                                    spacing: 8
                                    GeoButton { text: "⟲ 90°"; width: (parent.width - 8) / 2; onClicked: engine.rotateQuadrant(-1); tooltip: "Rotate 90° counter-clockwise." }
                                    GeoButton { text: "⟳ 90°"; width: (parent.width - 8) / 2; onClicked: engine.rotateQuadrant(1); tooltip: "Rotate 90° clockwise." }
                                }

                                InspectorLabel { text: "Flip" }
                                Row {
                                    width: parent.width
                                    spacing: 8
                                    GeoButton { text: "⇄ Horizontal"; width: (parent.width - 8) / 2; active: engine.filmControls.flip_h; onClicked: engine.setFilmControl("flip_h", !engine.filmControls.flip_h); tooltip: "Mirror the image left to right." }
                                    GeoButton { text: "⇅ Vertical"; width: (parent.width - 8) / 2; active: engine.filmControls.flip_v; onClicked: engine.setFilmControl("flip_v", !engine.filmControls.flip_v); tooltip: "Mirror the image top to bottom." }
                                }

                                Item { width: parent.width; height: 2 }
                                GeoButton { width: parent.width; text: "Reset geometry"; onClicked: engine.resetGeometry(); tooltip: "Clear crop, straighten, rotation and flips." }
                            }
                        }
                    }
                }

                // ── Export tab ─────────────────────────────────────────
                Column {
                    id: exportContent
                    width: parent.width
                    spacing: 14
                    visible: inspector.activeTab === 2 && !engine.lightroomRoundTrip

                    Rectangle {
                        width: parent.width
                        radius: 14
                        gradient: Gradient {
                            GradientStop { position: 0.0; color: "#1d1d20" }
                            GradientStop { position: 1.0; color: "#191a1c" }
                        }
                        border.width: 1
                        border.color: root.hair
                        implicitHeight: exportCol.implicitHeight + 32
                        Rectangle { anchors { left: parent.left; right: parent.right; top: parent.top; margins: 1 } height: 1; radius: 1; color: "#12ffffff" }

                        Column {
                            id: exportCol
                            x: 16; y: 16
                            width: parent.width - 32
                            spacing: 12

                            InspectorLabel { text: "Format" }
                            Grid {
                                width: parent.width
                                columns: 2
                                columnSpacing: 6
                                rowSpacing: 6
                                Repeater {
                                    model: [
                                        { id: "png8", label: "8-bit PNG" },
                                        { id: "png16", label: "16-bit PNG" },
                                        { id: "tiff", label: "16-bit TIFF" },
                                        { id: "jpeg", label: "JPEG" }
                                    ]
                                    delegate: Button {
                                        id: fmtBtn
                                        width: (parent.width - 6) / 2
                                        height: 32
                                        text: modelData.label
                                        readonly property bool selected: engine.exportFormat === modelData.id
                                        enabled: !engine.exporting
                                        onClicked: engine.exportFormat = modelData.id
                                        contentItem: Text { text: fmtBtn.text; color: fmtBtn.selected ? root.textPrimary : root.textSecondary; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; font.pixelSize: 11; font.weight: Font.Medium }
                                        background: Rectangle {
                                            radius: 6
                                            color: fmtBtn.selected ? "#33333a" : "transparent"
                                            border.width: 1
                                            border.color: fmtBtn.selected ? root.hair : root.border
                                            Rectangle { visible: fmtBtn.selected; anchors { left: parent.left; right: parent.right; top: parent.top; margins: 1 } height: 1; radius: 1; color: "#16ffffff" }
                                        }
                                    }
                                }
                            }
                            Row {
                                width: parent.width
                                visible: engine.exportFormat === "jpeg"
                                spacing: 8
                                InspectorLabel { text: "JPEG quality"; width: parent.width - qualityBox.width - 8; anchors.verticalCenter: parent.verticalCenter }
                                GraphiteSpin {
                                    id: qualityBox
                                    from: 1; to: 100
                                    value: engine.jpegQuality
                                    editable: true
                                    enabled: !engine.exporting
                                    onValueModified: engine.jpegQuality = value
                                }
                            }
                            Row {
                                width: parent.width
                                visible: engine.exportFormat === "tiff"
                                spacing: 8
                                InspectorLabel { text: "TIFF DPI"; width: parent.width - dpiBox.width - 8; anchors.verticalCenter: parent.verticalCenter }
                                GraphiteSpin {
                                    id: dpiBox
                                    from: 72; to: 1200; stepSize: 1
                                    value: engine.exportDpi
                                    editable: true
                                    enabled: !engine.exporting
                                    onValueModified: engine.exportDpi = value
                                }
                            }
                        }
                    }

                    Text {
                        width: parent.width
                        text: "Saved as a new file beside the original. 16-bit TIFF is uncompressed sRGB."
                        color: root.textMuted
                        font.pixelSize: 11
                        wrapMode: Text.WordWrap
                    }

                    PrimaryButton {
                        text: engine.exporting ? "Exporting…" : ("Export " + root.exportFormatLabel(engine.exportFormat))
                        enabled: engine.hasImage && !engine.exporting
                        onClicked: engine.exportImage()
                    }
                }

                // Save & Return — Lightroom edit-in mode only
                PrimaryButton {
                    visible: engine.lightroomRoundTrip
                    text: engine.exporting ? "Saving back…" : "Save & Return to Lightroom"
                    enabled: engine.hasImage && !engine.exporting
                    onClicked: engine.exportImage()
                }

                Text {
                    width: parent.width
                    topPadding: 4
                    text: "DFEE Native Engine"
                    color: root.textMuted
                    font.pixelSize: 11
                }
            }
        }
    }

    Rectangle {
        anchors.fill: parent
        z: 10
        visible: engine.exporting
        color: "#d9101114"

        Column {
            anchors.centerIn: parent
            spacing: 12

            BusyIndicator {
                anchors.horizontalCenter: parent.horizontalCenter
                running: engine.exporting
                width: 42
                height: 42
            }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: engine.lightroomRoundTrip ? "Saving back to Lightroom" : "Exporting full resolution"
                color: root.textPrimary
                font.pixelSize: 16
                font.weight: Font.Medium
            }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: engine.lightroomRoundTrip ? "Rendering and atomically replacing the working TIFF" : "Rendering and saving " + root.exportFormatLabel(engine.exportFormat)
                color: root.textSecondary
                font.pixelSize: 12
            }
        }
    }
}
