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
    readonly property color accent: "#e9e9ec"           // inverted light chip for active states
    readonly property color accentDark: "#c9c9ce"       // pressed light chip
    readonly property color accentText: "#161618"       // text on a light accent fill
    readonly property color knob: "#c4c4c9"             // slider knob
    readonly property color danger: "#e0655b"
    property bool exposureOpen: true
    property bool toneOpen: true
    property bool colorOpen: true
    property bool materialOpen: true

    function exportFormatLabel(format) {
        if (format === "png8") return "8-bit PNG"
        if (format === "png16") return "16-bit PNG"
        if (format === "jpeg") return "JPEG"
        return "16-bit TIFF"
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
            color: root.border

            Rectangle {
                width: control.visualPosition * parent.width
                height: parent.height
                radius: parent.radius
                color: root.accent
            }
        }

        handle: Rectangle {
            x: control.leftPadding + control.visualPosition * (control.availableWidth - width)
            y: control.topPadding + control.availableHeight / 2 - height / 2
            width: 14
            height: 14
            radius: 7
            color: control.pressed ? root.textPrimary : root.accent
            border.width: 2
            border.color: root.panel
        }
    }

    component PrimaryButton: Button {
        id: button
        width: parent.width
        height: 38
        font.pixelSize: 13
        font.weight: Font.Medium

        contentItem: Text {
            text: button.text
            color: button.enabled ? root.bg : root.textMuted
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            font: button.font
        }

        background: Rectangle {
            radius: 5
            color: !button.enabled ? root.panelRaised : button.down ? root.accentDark : root.accent
        }
    }

    component SecondaryButton: Button {
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
            radius: 5
            color: button.down ? root.panelRaised : "transparent"
            border.width: 1
            border.color: button.enabled ? root.border : root.panelRaised
        }
    }

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
                contentItem: Text { text: parent.text; color: root.accent; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; font: parent.font }
                background: Rectangle { color: "transparent" }
            }
            Text {
                id: valueLabel
                width: sliderRow.autoValue ? 34 : 42
                text: sliderRow.autoValue ? "Auto" : ((sliderRow.bipolar && sliderRow.currentValue > 0 ? "+" : "") + (sliderRow.decimals ? sliderRow.currentValue.toFixed(2) : sliderRow.currentValue.toFixed(0)))
                color: sliderRow.available && sliderRow.dirty ? root.accent : root.textMuted
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
        width: 340
        color: root.panel
        border.width: 1
        border.color: root.border

        Flickable {
            anchors.fill: parent
            clip: true
            contentWidth: width
            contentHeight: controls.implicitHeight + 40
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

            Column {
                id: controls
                x: 20
                y: 20
                width: parent.width - 40
                spacing: 16

            Column {
                width: parent.width
                spacing: 4

                Text {
                    text: "Film lab"
                    color: root.textPrimary
                    font.pixelSize: 19
                    font.weight: Font.Medium
                }
                Text {
                    text: engine.hasImage ? "Native preview" : "No image loaded"
                    color: root.textMuted
                    font.pixelSize: 12
                }
            }

            Rectangle {
                width: parent.width
                height: 1
                color: root.border
            }

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
                            width: (parent.width - 6) / 2
                            height: 31
                            text: modelData.label
                            readonly property bool selected: engine.exportFormat === modelData.id
                            enabled: !engine.exporting
                            onClicked: engine.exportFormat = modelData.id
                            contentItem: Text { text: parent.text; color: parent.selected ? root.bg : root.textSecondary; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; font.pixelSize: 11; font.weight: Font.Medium }
                            background: Rectangle { radius: 4; color: parent.selected ? root.accent : root.panelRaised; border.width: 1; border.color: parent.selected ? root.accent : root.border }
                        }
                    }
                }
                Row {
                    width: parent.width
                    visible: engine.exportFormat === "jpeg"
                    spacing: 8
                    InspectorLabel { text: "JPEG quality"; width: parent.width - qualityBox.width - 8 }
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
                    InspectorLabel { text: "TIFF DPI"; width: parent.width - dpiBox.width - 8 }
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

            Column {
                width: parent.width
                spacing: 7

                InspectorLabel { text: "Film stock" }
                ComboBox {
                    id: stockBox
                    width: parent.width
                    height: 38
                    model: engine.stockNames
                    onActivated: engine.stock = engine.stockIdAt(currentIndex)

                    contentItem: Text {
                        leftPadding: 12
                        rightPadding: 34
                        text: stockBox.displayText
                        color: root.textPrimary
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                        font.pixelSize: 13
                    }
                    background: Rectangle {
                        radius: 5
                        color: root.panelRaised
                        border.width: 1
                        border.color: stockBox.activeFocus ? root.accentDark : root.border
                    }
                    indicator: Text {
                        x: stockBox.width - width - 12
                        y: stockBox.topPadding + (stockBox.availableHeight - height) / 2
                        text: "⌄"
                        color: root.textSecondary
                        font.pixelSize: 16
                    }
                    popup: Popup {
                        y: stockBox.height + 4
                        width: stockBox.width
                        implicitHeight: Math.min(contentItem.implicitHeight + 8, 280)
                        padding: 4
                        contentItem: ListView {
                            clip: true
                            implicitHeight: contentHeight
                            model: stockBox.popup.visible ? stockBox.delegateModel : null
                            currentIndex: stockBox.highlightedIndex
                            ScrollIndicator.vertical: ScrollIndicator { }
                        }
                        background: Rectangle {
                            radius: 5
                            color: root.panelRaised
                            border.width: 1
                            border.color: root.border
                        }
                    }
                    delegate: ItemDelegate {
                        width: stockBox.width - 8
                        height: 34
                        highlighted: stockBox.highlightedIndex === index
                        contentItem: Text {
                            text: modelData
                            color: root.textPrimary
                            verticalAlignment: Text.AlignVCenter
                            elide: Text.ElideRight
                            font.pixelSize: 13
                        }
                        background: Rectangle {
                            radius: 4
                            color: parent.highlighted ? root.border : "transparent"
                        }
                    }
                }
            }

            Rectangle { width: parent.width; height: 1; color: root.border }

            Item {
                width: parent.width
                height: 30
                Text { anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter; text: "Film exposure"; color: root.textPrimary; font.pixelSize: 13; font.weight: Font.Medium }
                Text { anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; text: root.exposureOpen ? "-" : "+"; color: root.textMuted; font.pixelSize: 16 }
                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.exposureOpen = !root.exposureOpen }
            }
            Column {
                width: parent.width
                visible: root.exposureOpen
                spacing: 9
                InspectorLabel { text: "Scene placement" }
                Row {
                    width: parent.width
                    spacing: 6
                    Repeater {
                        model: [{ label: "Auto balanced", value: "auto_balanced" }, { label: "As shot", value: "as_shot" }]
                        delegate: Button {
                            width: (parent.width - 6) / 2
                            height: 32
                            text: modelData.label
                            readonly property bool selected: engine.filmControls.exposure_placement === modelData.value
                            onClicked: engine.setFilmControl("exposure_placement", modelData.value)
                            contentItem: Text { text: parent.text; color: parent.selected ? root.bg : root.textSecondary; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; font.pixelSize: 11; font.weight: Font.Medium }
                            background: Rectangle { radius: 4; color: parent.selected ? root.accent : root.panelRaised; border.width: 1; border.color: parent.selected ? root.accent : root.border }
                        }
                    }
                }
                FilmSlider { controlKey: "film_exposure_ev"; label: "Film exposure"; minimum: -3; maximum: 3; increment: 0.05; decimals: true; bipolar: true }
            }

            Item {
                width: parent.width
                height: 30
                Text { anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter; text: "Film tone"; color: root.textPrimary; font.pixelSize: 13; font.weight: Font.Medium }
                Text { anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; text: root.toneOpen ? "-" : "+"; color: root.textMuted; font.pixelSize: 16 }
                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.toneOpen = !root.toneOpen }
            }
            Column {
                width: parent.width
                visible: root.toneOpen
                spacing: 10
                CheckBox {
                    text: "Adaptive scene tone"
                    checked: engine.filmControls.adaptive
                    onToggled: engine.setFilmControl("adaptive", checked)
                    contentItem: Text { text: parent.text; color: root.textSecondary; leftPadding: parent.indicator.width + 8; verticalAlignment: Text.AlignVCenter; font.pixelSize: 12 }
                }
                FilmSlider { controlKey: "highlight_rolloff"; label: "Highlight rolloff"; minimum: 0; maximum: 200; neutral: 100 }
                FilmSlider { controlKey: "film_contrast"; label: "Film contrast"; minimum: 0; maximum: 200; neutral: 100 }
                FilmSlider { controlKey: "shadow_lift"; label: "Shadow lift"; minimum: -100; maximum: 100; bipolar: true }
            }

            Item {
                width: parent.width
                height: 30
                Text { anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter; text: "Color character"; color: root.textPrimary; font.pixelSize: 13; font.weight: Font.Medium }
                Text { anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; text: root.colorOpen ? "-" : "+"; color: root.textMuted; font.pixelSize: 16 }
                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.colorOpen = !root.colorOpen }
            }
            Column {
                width: parent.width
                visible: root.colorOpen
                spacing: 10
                opacity: engine.currentStockMonochrome ? 0.45 : 1.0
                Text { visible: engine.currentStockMonochrome; text: "Unavailable for monochrome stocks"; color: root.textMuted; font.pixelSize: 11 }
                FilmSlider { controlKey: "film_color_density"; label: "Color density"; minimum: 0; maximum: 200; neutral: 100; available: !engine.currentStockMonochrome }
                FilmSlider { controlKey: "emulsion_color_density"; label: "Color boost"; minimum: -100; maximum: 100; bipolar: true; available: !engine.currentStockMonochrome }
                FilmSlider { controlKey: "highlight_color_hold"; label: "Highlight saturation"; minimum: -100; maximum: 100; bipolar: true; available: !engine.currentStockMonochrome }
                FilmSlider { controlKey: "shadow_color_retention"; label: "Shadow saturation"; minimum: -100; maximum: 100; bipolar: true; available: !engine.currentStockMonochrome }
            }

            Item {
                width: parent.width
                height: 30
                Text { anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter; text: "Material finish"; color: root.textPrimary; font.pixelSize: 13; font.weight: Font.Medium }
                Text { anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; text: root.materialOpen ? "-" : "+"; color: root.textMuted; font.pixelSize: 16 }
                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.materialOpen = !root.materialOpen }
            }
            Column {
                width: parent.width
                visible: root.materialOpen
                spacing: 10
                InspectorLabel { text: "Grain" }
                CheckBox {
                    text: engine.grainResolving ? "Resolving stock grain" : "Match grain to film speed"
                    checked: engine.filmControls.grain_auto
                    enabled: !engine.grainResolving
                    onToggled: engine.setAutoGrain(checked)
                    contentItem: Text { text: parent.text; color: parent.enabled ? root.textSecondary : root.textMuted; leftPadding: parent.indicator.width + 8; verticalAlignment: Text.AlignVCenter; font.pixelSize: 12 }
                }
                FilmSlider { controlKey: "grain_strength"; label: "Strength"; minimum: 0; maximum: 2; increment: 0.05; decimals: true; available: !engine.filmControls.grain_auto; autoValue: engine.filmControls.grain_auto }
                FilmSlider { controlKey: "grain_size"; label: "Size"; minimum: 0.1; maximum: 2; increment: 0.05; decimals: true; neutral: 0.6; available: !engine.filmControls.grain_auto; autoValue: engine.filmControls.grain_auto }
                FilmSlider { controlKey: "grain_roughness"; label: "Roughness"; minimum: 0; maximum: 1; increment: 0.05; decimals: true; neutral: 0.5; available: !engine.filmControls.grain_auto; autoValue: engine.filmControls.grain_auto }
                InspectorLabel { text: "Halation" }
                FilmSlider { controlKey: "halation_strength"; label: "Strength"; minimum: 0; maximum: 200; neutral: 100 }
                FilmSlider { controlKey: "halation_threshold"; label: "Threshold"; minimum: 0; maximum: 100; neutral: 50 }
                InspectorLabel { text: "Bloom" }
                FilmSlider { controlKey: "bloom"; label: "Amount"; minimum: 0; maximum: 100 }
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
