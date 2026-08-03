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

    // Graphite palette — charcoal, monochrome. Colour lives only in the photo + boxart.
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

    component InspectorLabel: Text {
        color: root.textSecondary
        font.pixelSize: 12
        font.weight: Font.Medium
    }

    // Graphite-styled hover tooltip. Pair with a HoverHandler: `visible: hh.hovered`.
    component GraphiteTip: ToolTip {
        id: tip
        delay: 450
        padding: 9
        contentItem: Text {
            text: tip.text
            color: root.textPrimary
            font.pixelSize: 11
            lineHeight: 1.25
            wrapMode: Text.WordWrap
            width: Math.min(implicitWidth, 230)
        }
        background: Rectangle {
            color: "#232327"
            radius: 7
            border.width: 1
            border.color: root.hair
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

    // Tactile checkbox — recessed square when off, light chip with a drawn tick when on.
    component GraphiteCheck: CheckBox {
        id: cb
        property string caption: ""
        property string tooltip: ""
        spacing: 9
        implicitHeight: 20

        HoverHandler { id: cbHover; enabled: cb.tooltip.length > 0 }
        GraphiteTip { visible: cbHover.hovered && cb.tooltip.length > 0; text: cb.tooltip }

        indicator: Rectangle {
            implicitWidth: 18
            implicitHeight: 18
            x: 0
            y: (cb.height - height) / 2
            radius: 5
            color: cb.checked ? root.accent : root.inset
            border.width: 1
            border.color: cb.checked ? root.accent : root.hair
            opacity: cb.enabled ? 1.0 : 0.5
            // top bevel highlight when checked
            Rectangle {
                visible: cb.checked
                anchors { left: parent.left; right: parent.right; top: parent.top; margins: 1 }
                height: 1; radius: 1; color: "#40ffffff"
            }
            Canvas {
                anchors.fill: parent
                visible: cb.checked
                onVisibleChanged: if (visible) requestPaint()
                onPaint: {
                    var c = getContext("2d");
                    c.reset();
                    c.strokeStyle = "#161618";
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

    // Draggable colour-grading wheel: angle = hue, radius = saturation. Reads/writes
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
            GraphiteTip { visible: wheelHover.hovered; text: "Tints the " + wheel.label.toLowerCase() + " — drag from the centre to add colour, double-click to reset." }
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

        Row {
            width: parent.width
            InspectorLabel {
                width: parent.width - valueLabel.width - resetButton.width - 8
                text: sliderRow.label
                color: sliderRow.available ? root.textSecondary : root.textMuted
                elide: Text.ElideRight
            }
            Button {
                id: resetButton
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
                width: sliderRow.autoValue ? 34 : 42
                text: sliderRow.autoValue ? "Auto" : ((sliderRow.bipolar && sliderRow.currentValue > 0 ? "+" : "") + (sliderRow.decimals ? sliderRow.currentValue.toFixed(2) : sliderRow.currentValue.toFixed(0)))
                color: sliderRow.available && sliderRow.dirty ? root.textPrimary : root.textValue
                horizontalAlignment: Text.AlignRight
                font.pixelSize: 12
            }
        }
        InspectorSlider {
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
        GraphiteTip { visible: sliderHover.hovered && sliderRow.tooltip.length > 0; text: sliderRow.tooltip }
    }

    Rectangle {
        id: previewCanvas
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.right: inspector.left
        color: root.canvas

        Image {
            anchors.fill: parent
            anchors.margins: 28
            fillMode: Image.PreserveAspectFit
            cache: false
            source: engine.hasImage ? ("image://preview/frame?rev=" + engine.previewRevision) : ""
            visible: engine.hasImage
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
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        width: 356
        color: root.bg                               // darker rail so the cards read as raised
        border.width: 1
        border.color: root.border

        Flickable {
            anchors.fill: parent
            clip: true
            contentWidth: width
            contentHeight: controls.implicitHeight + 44
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

            Column {
                id: controls
                x: 18
                y: 20
                width: parent.width - 36
                spacing: 14

                // ── Header ─────────────────────────────────────────────
                Item {
                    width: parent.width
                    height: 40
                    Text {
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        text: "Film lab"
                        color: root.textPrimary
                        font.pixelSize: 20
                        font.weight: Font.Medium
                    }
                    Text {
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        text: engine.hasImage ? "Native preview" : "No image"
                        color: root.textMuted
                        font.pixelSize: 12
                    }
                }

                // ── Actions ────────────────────────────────────────────
                PrimaryButton {
                    text: "Open image"
                    enabled: !engine.lightroomRoundTrip
                    onClicked: openDialog.open()
                }

                SecondaryButton {
                    text: engine.exporting ? (engine.lightroomRoundTrip ? "Saving back..." : "Exporting...") : (engine.lightroomRoundTrip ? "Save & Return to Lightroom" : "Export " + root.exportFormatLabel(engine.exportFormat))
                    enabled: engine.hasImage && !engine.exporting
                    onClicked: engine.exportImage()
                }

                Column {
                    width: parent.width
                    visible: engine.hasImage && !engine.lightroomRoundTrip
                    spacing: 7

                    InspectorLabel { text: "Export format" }
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
                                height: 31
                                text: modelData.label
                                readonly property bool selected: engine.exportFormat === modelData.id
                                enabled: !engine.exporting
                                onClicked: engine.exportFormat = modelData.id
                                contentItem: Text { text: fmtBtn.text; color: fmtBtn.selected ? root.textPrimary : root.textSecondary; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; font.pixelSize: 11; font.weight: Font.Medium }
                                background: Rectangle {
                                    radius: 6
                                    color: fmtBtn.selected ? "#2f2f35" : "transparent"
                                    border.width: 1
                                    border.color: fmtBtn.selected ? root.hair : root.border
                                    Rectangle { visible: fmtBtn.selected; anchors { left: parent.left; right: parent.right; top: parent.top; margins: 1 } height: 1; radius: 1; color: "#14ffffff" }
                                }
                            }
                        }
                    }
                    Row {
                        width: parent.width
                        visible: engine.exportFormat === "jpeg"
                        spacing: 8
                        InspectorLabel { text: "JPEG quality"; width: parent.width - qualityBox.width - 8; anchors.verticalCenter: parent.verticalCenter }
                        SpinBox {
                            id: qualityBox
                            from: 1
                            to: 100
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
                        SpinBox {
                            id: dpiBox
                            from: 72
                            to: 1200
                            stepSize: 1
                            value: engine.exportDpi
                            editable: true
                            enabled: !engine.exporting
                            onValueModified: engine.exportDpi = value
                        }
                    }
                }

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
                            Text { anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter; text: "Film recipe"; color: root.textPrimary; font.pixelSize: 13; font.weight: Font.Medium }
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
                                model: engine.stockNames
                                currentIndex: {
                                    for (var i = 0; i < engine.stockNames.length; ++i)
                                        if (engine.stockIdAt(i) === engine.stock) return i;
                                    return 0;
                                }
                                onActivated: engine.stock = engine.stockIdAt(currentIndex)

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
                                            stockId: engine.stockIdAt(stockBox.currentIndex)
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
                                                text: engine.currentStockMonochrome ? "Monochrome negative" : (engine.stock === "none" ? "No film stock" : "Colour negative")
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
                                            cell: 34
                                            stockId: engine.stockIdAt(index)
                                        }
                                        Text {
                                            anchors.verticalCenter: parent.verticalCenter
                                            text: modelData
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
                            Text { anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter; text: "Print finish"; color: root.textPrimary; font.pixelSize: 13; font.weight: Font.Medium }
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
                                FilmSlider { controlKey: "print_c"; label: "Color head: cyan"; minimum: -100; maximum: 100; bipolar: true; tooltip: "Enlarger colour head — subtractive cyan filtration (removes red). Forward cools the print." }
                                FilmSlider { controlKey: "print_m"; label: "Color head: magenta"; minimum: -100; maximum: 100; bipolar: true; tooltip: "Enlarger colour head — subtractive magenta filtration (removes green)." }
                                FilmSlider { controlKey: "print_y"; label: "Color head: yellow"; minimum: -100; maximum: 100; bipolar: true; tooltip: "Enlarger colour head — subtractive yellow filtration (removes blue). Forward warms the print." }
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
                            Text { anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter; text: "Film exposure"; color: root.textPrimary; font.pixelSize: 13; font.weight: Font.Medium }
                            ChevronToggle { anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; open: exposureCard.open }
                            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: exposureCard.open = !exposureCard.open }
                        }

                        Column {
                            width: parent.width
                            spacing: 12
                            visible: exposureCard.open

                            InspectorLabel { text: "Scene placement" }
                            Rectangle {                              // recessed segmented track
                                width: parent.width
                                height: 36
                                radius: 9
                                color: root.inset
                                border.width: 1
                                border.color: root.hair
                                HoverHandler { id: placementHover }
                                GraphiteTip { visible: placementHover.hovered; text: "Auto balanced sets a stock-aware starting exposure for the scene. As shot preserves the RAW's own exposure placement before the film response." }
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
                            FilmSlider { controlKey: "film_exposure_ev"; label: "Film exposure"; minimum: -3; maximum: 3; increment: 0.05; decimals: true; bipolar: true; tooltip: "Virtual exposure (in stops) reaching the film before its tone and colour response — like rating the stock faster or slower." }
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
                            Text { anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter; text: "Film tone"; color: root.textPrimary; font.pixelSize: 13; font.weight: Font.Medium }
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
                            Text { anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter; text: "Color character"; color: root.textPrimary; font.pixelSize: 13; font.weight: Font.Medium }
                            ChevronToggle { anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; open: colorCard.open }
                            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: colorCard.open = !colorCard.open }
                        }

                        Column {
                            width: parent.width
                            spacing: 12
                            visible: colorCard.open
                            opacity: engine.currentStockMonochrome ? 0.45 : 1.0

                            Text { visible: engine.currentStockMonochrome; text: "Unavailable for monochrome stocks"; color: root.textMuted; font.pixelSize: 11 }
                            FilmSlider { controlKey: "film_color_density"; label: "Color density"; minimum: 0; maximum: 200; neutral: 100; available: !engine.currentStockMonochrome; tooltip: "How dense and cohesive the film's colours are — forward for richer, deeper, more film-like colour; back for a thinner, more digital look." }
                            FilmSlider { controlKey: "film_color_compression"; label: "Color compression"; minimum: 0; maximum: 200; neutral: 100; available: !engine.currentStockMonochrome; tooltip: "Harmonises the palette toward the stock's characteristic hues — higher gathers colours into a more cohesive film palette." }
                            FilmSlider { controlKey: "emulsion_color_density"; label: "Color boost"; minimum: -100; maximum: 100; bipolar: true; available: !engine.currentStockMonochrome; tooltip: "Overall saturation of the stock's colour dyes — forward for punchier colour, back for a muted look." }
                            FilmSlider { controlKey: "palette_range"; label: "Palette range"; minimum: -100; maximum: 100; bipolar: true; available: !engine.currentStockMonochrome; tooltip: "How far colours are allowed to diverge from the stock's core hues — forward widens the palette, back pulls it tighter." }
                            FilmSlider { controlKey: "highlight_color_hold"; label: "Highlight saturation"; minimum: -100; maximum: 100; bipolar: true; available: !engine.currentStockMonochrome; tooltip: "How much colour survives in the highlights — back bleaches bright areas toward clean white (rescues blown, over-warm highlights)." }
                            FilmSlider { controlKey: "shadow_color_retention"; label: "Shadow saturation"; minimum: -100; maximum: 100; bipolar: true; available: !engine.currentStockMonochrome; tooltip: "How much colour survives in the shadows — forward keeps darks colourful, back mutes them toward neutral." }
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
                            FilmSlider { controlKey: "exposure"; label: "Exposure"; minimum: -3; maximum: 3; increment: 0.05; decimals: true; bipolar: true; tooltip: "Overall image brightness, in stops. A general grade on top of the film response." }
                            FilmSlider { controlKey: "contrast"; label: "Contrast"; minimum: -100; maximum: 100; bipolar: true; tooltip: "Global contrast — spreads or compresses the tonal range around the midtones." }
                            FilmSlider { controlKey: "highlights"; label: "Highlights"; minimum: -100; maximum: 100; bipolar: true; tooltip: "Recovers or brightens the brighter tones without moving whites." }
                            FilmSlider { controlKey: "shadows"; label: "Shadows"; minimum: -100; maximum: 100; bipolar: true; tooltip: "Opens or deepens the darker tones without moving blacks." }
                            FilmSlider { controlKey: "whites"; label: "Whites"; minimum: -100; maximum: 100; bipolar: true; tooltip: "Sets the white clipping point — how bright the brightest tones become." }
                            FilmSlider { controlKey: "blacks"; label: "Blacks"; minimum: -100; maximum: 100; bipolar: true; tooltip: "Sets the black clipping point — how deep the darkest tones become." }
                            FilmSlider { controlKey: "midtones"; label: "Midtones"; minimum: -100; maximum: 100; bipolar: true; tooltip: "Brightness of the mid-tones, leaving the extremes anchored." }
                        }
                    }
                }

                // ── Colour balance card ────────────────────────────────
                Rectangle {
                    id: colourCard
                    property bool open: false
                    width: parent.width
                    radius: 14
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: "#1d1d20" }
                        GradientStop { position: 1.0; color: "#191a1c" }
                    }
                    border.width: 1
                    border.color: root.hair
                    implicitHeight: colourCol.implicitHeight + 32
                    Rectangle { anchors { left: parent.left; right: parent.right; top: parent.top; margins: 1 } height: 1; radius: 1; color: "#12ffffff" }

                    Column {
                        id: colourCol
                        x: 16; y: 16
                        width: parent.width - 32
                        spacing: 14

                        Item {
                            width: parent.width
                            height: 20
                            Text { anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter; text: "Colour balance"; color: root.textPrimary; font.pixelSize: 13; font.weight: Font.Medium }
                            ChevronToggle { anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; open: colourCard.open }
                            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: colourCard.open = !colourCard.open }
                        }

                        Column {
                            width: parent.width
                            spacing: 12
                            visible: colourCard.open
                            FilmSlider { controlKey: "temp"; label: "Temperature"; minimum: -100; maximum: 100; bipolar: true; tooltip: "White balance warmth — forward warms (more amber), back cools (more blue)." }
                            FilmSlider { controlKey: "tint"; label: "Tint"; minimum: -100; maximum: 100; bipolar: true; tooltip: "White balance green/magenta — forward toward magenta, back toward green." }
                            FilmSlider { controlKey: "vibrance"; label: "Vibrance"; minimum: -100; maximum: 100; bipolar: true; tooltip: "Smart saturation that protects skin tones and already-saturated colours." }
                            FilmSlider { controlKey: "saturation"; label: "Saturation"; minimum: -100; maximum: 100; bipolar: true; tooltip: "Overall colour intensity, applied evenly to all hues." }
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

                // ── Colour grading card ────────────────────────────────
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
                            Text { anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter; text: "Colour grading"; color: root.textPrimary; font.pixelSize: 13; font.weight: Font.Medium }
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
                            FilmSlider { controlKey: "cg_crossbalance"; label: "Film crossbalance"; minimum: -100; maximum: 100; bipolar: true; tooltip: "One-knob split-tone: forward for teal shadows and warm highlights, back for the inverse." }
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
                            Text { anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter; text: "Material finish"; color: root.textPrimary; font.pixelSize: 13; font.weight: Font.Medium }
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

                Text {
                    width: parent.width
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
