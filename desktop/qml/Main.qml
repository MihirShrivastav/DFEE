import QtQuick
import QtQuick.Window
import QtQuick.Controls
import QtQuick.Dialogs

Window {
    width: 1280; height: 800; visible: true; title: "DFEE"; color: "#0f0f10"

    FileDialog {
        id: openDialog
        nameFilters: ["Images (*.tif *.tiff *.arw *.nef *.cr3 *.raf *.rw2 *.dng)"]
        onAccepted: engine.openFile(selectedFile)
    }

    // Preview canvas (everything left of the right panel)
    Rectangle {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.right: parent.right
        anchors.rightMargin: 340
        color: "#0f0f10"

        Image {
            anchors.centerIn: parent
            width: parent.width - 40
            height: parent.height - 40
            fillMode: Image.PreserveAspectFit
            cache: false
            source: engine.hasImage ? ("image://preview/frame?rev=" + engine.previewRevision) : ""
            visible: engine.hasImage
        }

        Text {
            anchors.centerIn: parent
            visible: !engine.hasImage
            text: "Open an image to begin"
            color: "#5a5a60"
            font.pixelSize: 15
        }

        // Status bar at bottom of canvas
        Text {
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.margins: 12
            visible: engine.status.length > 0
            text: engine.status
            color: "#8b8b90"
            font.pixelSize: 12
        }
    }

    // Right control panel
    Column {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 20
        width: 300
        spacing: 12

        Button {
            text: "Open…"
            width: parent.width
            onClicked: openDialog.open()
        }

        Button {
            text: "Export TIFF"
            width: parent.width
            enabled: engine.hasImage
            onClicked: engine.exportImage()
        }

        Text { text: "Film stock"; color: "#8b8b90"; font.pixelSize: 12 }
        ComboBox {
            id: stockBox
            width: parent.width
            model: engine.stockNames
            onActivated: engine.stock = engine.stockIdAt(currentIndex)
        }

        Text { text: "Film exposure"; color: "#8b8b90"; font.pixelSize: 12 }
        Slider {
            width: parent.width; from: -5; to: 5; value: engine.filmExposure
            onMoved: engine.filmExposure = value
        }

        Text { text: "Shadow lift"; color: "#8b8b90"; font.pixelSize: 12 }
        Slider {
            width: parent.width; from: -100; to: 100; value: engine.shadowLift
            onMoved: engine.shadowLift = value
        }
    }
}
