import QtQuick
import DFEE

// Print: an optional print stock (the paper the negative is printed on), its
// strength, color head and contrast. Controls dim until a print stock is chosen.
FlInspectorSection {
    id: sec
    title: "Print"
    group: "print"
    readonly property string printId: engine.filmControls.print_stock || "none"
    readonly property bool active: printId !== "none"
    readonly property string printName: {
        const names = engine.printStockNames;
        for (let i = 0; i < names.length; ++i) if (engine.printStockIdAt(i) === sec.printId) return names[i];
        return "None";
    }
    summary: active ? printName : "Off"

    Item {
        width: parent.width
        height: Theme.controlHeight
        Text {
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            text: "Print stock"
            color: Theme.textSecondary
            font.pixelSize: Theme.fontLabel
        }
        Rectangle {
            id: field
            objectName: "printField"
            anchors.right: parent.right
            width: 172
            height: parent.height
            radius: Theme.radiusControl
            color: fieldHover.hovered ? Theme.controlHover : Theme.control
            HoverHandler { id: fieldHover }
            MouseArea { anchors.fill: parent; onClicked: printPicker.visible ? printPicker.close() : printPicker.open() }
            Text {
                anchors.left: parent.left
                anchors.leftMargin: 10
                anchors.right: caret.left
                anchors.rightMargin: 6
                anchors.verticalCenter: parent.verticalCenter
                text: sec.printName
                color: Theme.text
                font.pixelSize: Theme.fontLabel
                elide: Text.ElideRight
            }
            FlIcon {
                id: caret
                name: "caret-up-down"
                size: 12
                color: Theme.textCaption
                anchors.right: parent.right
                anchors.rightMargin: 8
                anchors.verticalCenter: parent.verticalCenter
            }
            FlListPopup {
                id: printPicker
                objectName: "printPicker"
                rowPrefix: "print_"
                width: 240
                x: field.width - width
                y: field.height + 4
                currentId: sec.printId
                rows: engine.printStockNames.map((name, i) => ({ id: engine.printStockIdAt(i), name: name, group: "", detail: "", art: "" }))
                onPicked: (id) => engine.setFilmControl("print_stock", id)
            }
        }
    }
    FlFilmSlider {
        controlKey: "print_strength"; label: "Print strength"
        from: 0; to: 1; stepSize: 0.05; decimals: 2; neutral: 1.0; available: sec.active
        tip: "Blend between the source and the selected print material. One is the calibrated stock response."
    }
    FlFilmSlider {
        controlKey: "print_c"; label: "Color head: cyan"
        from: -100; to: 100; bipolar: true; available: sec.active
        tip: "A bounded cyan printer-timing correction. It cools the printable mid-scale without clipping red."
    }
    FlFilmSlider {
        controlKey: "print_m"; label: "Color head: magenta"
        from: -100; to: 100; bipolar: true; available: sec.active
        tip: "A bounded magenta printer-timing correction. It shifts the printable mid-scale without clipping green."
    }
    FlFilmSlider {
        controlKey: "print_y"; label: "Color head: yellow"
        from: -100; to: 100; bipolar: true; available: sec.active
        tip: "A bounded yellow printer-timing correction. It warms the printable mid-scale without clipping blue."
    }
    FlFilmSlider {
        controlKey: "print_contrast"; label: "Print contrast"
        from: -100; to: 100; bipolar: true; available: sec.active
        tip: "Adjust the middle slope of the selected print material while retaining its toe and shoulder."
    }
    FlFilmSlider {
        controlKey: "print_black_point"; label: "Paper black"
        from: -100; to: 100; bipolar: true; available: sec.active
        tip: "Positive opens the paper base and lower shadows; negative produces a denser, tighter print toe. It never globally lifts or clips the image."
    }
}
