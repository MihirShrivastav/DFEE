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
    // Re-read whenever anything that names the file changes, and on open (the
    // folder's contents may have changed since).
    property int refresh: 0
    onOpened: refresh++
    readonly property var target: {
        refresh; exportPrefs.folderPath; exportPrefs.nextToOriginal; exportPrefs.nameTemplate;
        exportPrefs.collision; exportPrefs.format; exportPrefs.sequence;
        engine.currentFile; engine.stock; engine.imageInfo;
        return engine.exportTarget();
    }
    function formatIndex() {
        for (let i = 0; i < formats.length; ++i) if (formats[i].id === exportPrefs.format) return i;
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
            onActivated: (i) => exportPrefs.format = sheet.formats[i].id
        }
    }
    FlSliderRow {
        objectName: "jpegQualityRow"
        visible: exportPrefs.format === "jpeg"
        label: "Quality"
        from: 1; to: 100; neutral: 100
        value: exportPrefs.jpegQuality
        onMoved: (v) => exportPrefs.jpegQuality = Math.round(v)
    }
    Item {
        width: parent.width
        height: Theme.controlHeight
        visible: exportPrefs.format === "tiff"
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
                text: String(exportPrefs.dpi)
                validator: IntValidator { bottom: 72; top: 1200 }
                // Live, so Export counts a typed value without Enter.
                onTextEdited: if (acceptableInput) exportPrefs.dpi = parseInt(text)
                onEditingFinished: if (acceptableInput) exportPrefs.dpi = parseInt(text)
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: "dpi"
                color: Theme.textCaption
                font.pixelSize: Theme.fontLabel
            }
        }
    }
    Text {
        objectName: "exportNameExample"
        width: parent.width
        text: sheet.target.fileName || ""
        elide: Text.ElideMiddle
        color: Theme.textCaption
        font.pixelSize: Theme.fontCaption
    }
    Text {
        objectName: "exportTargetNote"
        width: parent.width
        visible: text.length > 0
        wrapMode: Text.Wrap
        text: !sheet.target.exists ? ""
            : sheet.target.skip ? "A file with this name exists, so it will be skipped."
            : sheet.target.replaces ? "Will replace the existing file."
            : "A file with this name exists, so a number is added."
        color: sheet.target.skip || sheet.target.replaces ? Theme.danger : Theme.textCaption
        font.pixelSize: Theme.fontCaption
    }
    Text {
        visible: exportPrefs.format === "tiff"
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
            enabled: engine.hasImage && !engine.exporting && !sheet.target.skip
            onClicked: { engine.exportImage(); sheet.close(); }
        }
    }
}
