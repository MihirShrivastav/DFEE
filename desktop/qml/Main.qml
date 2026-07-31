import QtQuick
import QtQuick.Window
import QtQuick.Controls

Window {
    width: 1280; height: 800; visible: true; title: "DFEE"; color: "#0f0f10"

    Column {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 20
        width: 300
        spacing: 12

        Text { text: "Film stock"; color: "#8b8b90"; font.pixelSize: 12 }
        ComboBox {
            id: stockBox
            width: parent.width
            model: engine.stockNames
            onActivated: engine.stock = engine.stockIdAt(currentIndex)
        }
    }
}
