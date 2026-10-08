import QtQuick
import QtQuick.Dialogs
import DFEE

// Export: where the file goes (remembered folder, favourites, recents, or next to the
// original), its name (a template with a live example), what happens when the name is
// taken, and the format. Lightroom mode never opens this sheet; it saves straight back.
FlSheet {
    id: sheet
    objectName: "exportSheet"
    title: "Export"
    width: 520
    readonly property int labelWidth: 112
    readonly property var formats: [
        { id: "jpeg", label: "JPEG" }, { id: "png8", label: "8-bit PNG" },
        { id: "png16", label: "16-bit PNG" }, { id: "tiff", label: "16-bit TIFF" }
    ]
    // Re-read whenever anything that names the file changes, and on open (the
    // folder's contents may have changed since). Closed, it checks nothing: a
    // remembered folder on a slow network share must not stall photo or film changes.
    property int refresh: 0
    onOpened: refresh++
    readonly property var target: {
        if (!sheet.visible) return ({});
        refresh; exportPrefs.folderPath; exportPrefs.nextToOriginal; exportPrefs.nameTemplate;
        exportPrefs.collision; exportPrefs.format; exportPrefs.sequence;
        engine.currentFile; engine.stock; engine.imageInfo;
        return engine.exportTarget();
    }
    function formatIndex() {
        for (let i = 0; i < formats.length; ++i) if (formats[i].id === exportPrefs.format) return i;
        return 0;
    }
    function folderName(p) { const parts = p.split("/"); return parts[parts.length - 1] || p; }
    function samePath(a, b) { return a.toLowerCase() === b.toLowerCase(); }
    // Favourite and recent folders for the menu; the default folder always has its own
    // row, and a favourite isn't repeated under recent.
    readonly property var favoriteFolders: exportPrefs.favorites.filter(p => !samePath(p, exportPrefs.defaultFolder))
    readonly property var recentFolders: {
        exportPrefs.favorites;
        return exportPrefs.recents.filter(p => !exportPrefs.isFavorite(p) && !samePath(p, exportPrefs.defaultFolder));
    }
    readonly property string currentFolderId: exportPrefs.nextToOriginal ? "next"
        : exportPrefs.folder.length === 0 ? "default" : "path:" + exportPrefs.folderPath
    function pickFolder(id) {
        if (id === "choose") folderDialog.open();
        else if (id === "next") exportPrefs.useNextToOriginal();
        else if (id === "default") exportPrefs.useDefaultFolder();
        else if (id.startsWith("path:")) exportPrefs.useFolder(id.slice(5));
    }
    FolderDialog {
        id: folderDialog
        title: "Export to folder"
        currentFolder: "file:///" + exportPrefs.folderPath
        onAccepted: exportPrefs.useFolderUrl(selectedFolder)
    }

    Item {                                   // Save to: folder dropdown + favourite star
        width: parent.width
        height: Theme.controlHeight
        Text {
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            text: "Save to"
            color: Theme.textSecondary
            font.pixelSize: Theme.fontLabel
        }
        Rectangle {
            id: folderBox
            objectName: "exportFolderBox"
            x: sheet.labelWidth
            width: parent.width - sheet.labelWidth - favButton.width - 6
            height: Theme.controlHeight
            radius: Theme.radiusControl
            color: Theme.inset
            FlIcon {
                id: folderIcon
                x: 8
                anchors.verticalCenter: parent.verticalCenter
                name: "folder-simple"
                size: 14
                color: Theme.textSecondary
            }
            Text {
                objectName: "exportFolderLabel"
                anchors.left: folderIcon.right
                anchors.leftMargin: 6
                anchors.right: caret.left
                anchors.rightMargin: 6
                anchors.verticalCenter: parent.verticalCenter
                text: exportPrefs.nextToOriginal ? "Next to the original" : sheet.folderName(exportPrefs.folderPath)
                elide: Text.ElideRight
                color: Theme.text
                font.pixelSize: Theme.fontLabel
            }
            FlIcon {
                id: caret
                anchors.right: parent.right
                anchors.rightMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                name: "caret-down"
                size: 12
                color: Theme.textTertiary
            }
            HoverHandler { id: folderHover }
            FlTip {
                visible: folderHover.hovered && !folderPicker.visible
                text: exportPrefs.nextToOriginal ? "Each photo's own folder" : exportPrefs.folderPath
            }
            MouseArea {
                anchors.fill: parent
                onClicked: folderPicker.visible ? folderPicker.close() : folderPicker.open()
            }
            FlFolderMenu {
                id: folderPicker
                objectName: "exportFolderPicker"
                y: parent.height + 4
                width: parent.width
                favorites: sheet.favoriteFolders
                recents: sheet.recentFolders
                defaultFolder: exportPrefs.defaultFolder
                currentId: sheet.currentFolderId
                focusReturn: sheet.contentItem
                onPicked: (id) => sheet.pickFolder(id)
            }
        }
        FlIconButton {
            id: favButton
            objectName: "exportFavoriteButton"
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            visible: !exportPrefs.nextToOriginal
            readonly property bool favorite: { exportPrefs.favorites; return exportPrefs.isFavorite(exportPrefs.folderPath); }
            iconName: favorite ? "star-fill" : "star"
            tip: favorite ? "Remove from favourites" : "Add to favourites"
            onClicked: exportPrefs.toggleFavorite(exportPrefs.folderPath)
        }
    }
    Item {                                   // File name: template field
        width: parent.width
        height: Theme.controlHeight
        Text {
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            text: "File name"
            color: Theme.textSecondary
            font.pixelSize: Theme.fontLabel
        }
        FlTextField {
            id: nameField
            objectName: "exportNameField"
            x: sheet.labelWidth
            width: parent.width - sheet.labelWidth
            tabStop: true
            escReturnsFocus: false
            text: exportPrefs.nameTemplate
            onTextEdited: exportPrefs.nameTemplate = text
        }
    }
    Row {                                    // tokens insert at the cursor
        x: sheet.labelWidth
        spacing: 6
        Repeater {
            model: ["{name}", "{film}", "{date}", "{seq}", "{camera}"]
            delegate: Rectangle {
                objectName: "exportToken_" + modelData.slice(1, -1)
                width: tokenText.implicitWidth + 14
                height: 22
                radius: Theme.radiusControl
                color: tokenArea.containsMouse ? Theme.rowSelected : Theme.inset
                Text {
                    id: tokenText
                    anchors.centerIn: parent
                    text: modelData
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontCaption
                }
                MouseArea {
                    id: tokenArea
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: {
                        nameField.insert(nameField.cursorPosition, modelData);
                        exportPrefs.nameTemplate = nameField.text;
                    }
                }
            }
        }
    }
    Text {
        objectName: "exportNameExample"
        x: sheet.labelWidth
        width: parent.width - sheet.labelWidth
        text: sheet.target.fileName || ""
        elide: Text.ElideMiddle
        color: Theme.textCaption
        font.pixelSize: Theme.fontCaption
    }
    Text {
        objectName: "exportTargetNote"
        x: sheet.labelWidth
        width: parent.width - sheet.labelWidth
        visible: text.length > 0
        wrapMode: Text.Wrap
        text: !sheet.target.exists ? ""
            : sheet.target.skip ? "A file with this name exists, so it will be skipped."
            : sheet.target.replaces ? "Will replace the existing file."
            : "A file with this name exists, so a number is added."
        color: sheet.target.skip || sheet.target.replaces ? Theme.danger : Theme.textCaption
        font.pixelSize: Theme.fontCaption
    }
    Item {                                   // If the name exists
        width: parent.width
        height: Theme.segmentHeight + 4
        Text {
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            text: "If the name exists"
            color: Theme.textSecondary
            font.pixelSize: Theme.fontLabel
        }
        FlSegmented {
            objectName: "exportCollisionSegmented"
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            readonly property var rules: ["number", "replace", "skip"]
            model: ["Add number", "Replace", "Skip"]
            currentIndex: Math.max(0, rules.indexOf(exportPrefs.collision))
            onActivated: (i) => exportPrefs.collision = rules[i]
        }
    }
    FlHairline { width: parent.width }

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
