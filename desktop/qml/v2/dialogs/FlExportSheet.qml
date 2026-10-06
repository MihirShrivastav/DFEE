import QtQuick
import DFEE

// Export: format, JPEG quality or TIFF resolution, and where the file goes (next to
// the original; choosing a folder comes with Phase 4). Lightroom mode never opens
// this sheet — it exports straight back.
FlSheet {
    id: sheet
    objectName: "exportSheet"
    title: "Export"
    width: 440
    readonly property var formats: [
        { id: "jpeg", label: "JPEG" }, { id: "png8", label: "8-bit PNG" },
        { id: "png16", label: "16-bit PNG" }, { id: "tiff", label: "16-bit TIFF" }
    ]
    function formatIndex() {
        for (let i = 0; i < formats.length; ++i) if (formats[i].id === engine.exportFormat) return i;
        return 0;
    }

    Item {
        width: parent.width
        height: Theme.segmentHeight + 4
        Text {
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            text: "Format"
            color: Theme.textSecondary
            font.pixelSize: Theme.fontLabel
        }
        FlSegmented {
            objectName: "exportFormatSegmented"
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            model: sheet.formats.map(f => f.label)
            currentIndex: sheet.formatIndex()
            onActivated: (i) => engine.exportFormat = sheet.formats[i].id
        }
    }
    FlSliderRow {
        objectName: "jpegQualityRow"
        visible: engine.exportFormat === "jpeg"
        label: "Quality"
        from: 1; to: 100; neutral: 100
        value: engine.jpegQuality
        onMoved: (v) => engine.jpegQuality = Math.round(v)
    }
    Item {
        width: parent.width
        height: Theme.controlHeight
        visible: engine.exportFormat === "tiff"
        Text {
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            text: "Resolution"
            color: Theme.textSecondary
            font.pixelSize: Theme.fontLabel
        }
        Row {
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            spacing: 6
            FlTextField {
                objectName: "exportDpiField"
                width: 72
                escReturnsFocus: false
                horizontalAlignment: TextInput.AlignRight
                text: String(engine.exportDpi)
                validator: IntValidator { bottom: 72; top: 1200 }
                // Live, so Export counts a typed value without Enter.
                onTextEdited: if (acceptableInput) engine.exportDpi = parseInt(text)
                onEditingFinished: if (acceptableInput) engine.exportDpi = parseInt(text)
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: "dpi"
                color: Theme.textCaption
                font.pixelSize: Theme.fontLabel
            }
        }
    }
    Item {
        width: parent.width
        height: 20
        Text {
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            text: "Saves to"
            color: Theme.textSecondary
            font.pixelSize: Theme.fontLabel
        }
        Text {
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            text: "Next to the original"
            color: Theme.text
            font.pixelSize: Theme.fontLabel
        }
    }
    Text {
        visible: engine.exportFormat === "tiff"
        text: "16-bit TIFF is uncompressed sRGB."
        color: Theme.textCaption
        font.pixelSize: Theme.fontCaption
    }
    Row {
        anchors.right: parent.right
        spacing: 8
        FlButton { kind: "quiet"; text: "Cancel"; onClicked: sheet.close() }
        FlButton {
            objectName: "exportConfirm"
            kind: "accent"
            text: "Export"
            enabled: engine.hasImage && !engine.exporting
            onClicked: { engine.exportImage(); sheet.close(); }
        }
    }
}
