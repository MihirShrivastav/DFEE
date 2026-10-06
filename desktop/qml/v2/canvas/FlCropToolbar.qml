import QtQuick
import DFEE

// Floating crop toolbar (DESIGN.md): 44 tall, radius 12, translucent toolbar color
// with an inner hairline and a soft shadow, centred at the canvas bottom. Aspect,
// straighten, rotate, flip, Reset, Done. `compact` drops the straighten readout.
Item {
    id: bar
    objectName: "cropToolbar"
    property bool compact: false
    property int presetIndex: 5                       // Free until a preset is chosen
    signal aspectChosen(real ratio)
    signal resetRequested()
    signal doneRequested()
    readonly property var presets: [
        { label: "Original", r: -1 }, { label: "3:2", r: 1.5 }, { label: "4:5", r: 0.8 },
        { label: "1:1", r: 1 }, { label: "16:9", r: 1.7777778 }, { label: "Free", r: 0 }
    ]
    readonly property real straighten: Number(engine.filmControls.straighten_deg)
    width: row.implicitWidth + 24
    height: 44
    onVisibleChanged: if (visible) presetIndex = 5

    Rectangle {                                       // shadow
        anchors.fill: parent
        anchors.topMargin: 8
        anchors.bottomMargin: -10
        radius: 14
        color: "#59000000"
    }
    Rectangle {
        anchors.fill: parent
        radius: 12
        color: "#eb242426"
        border.width: 1
        border.color: "#14ffffff"
    }
    Row {
        id: row
        anchors.centerIn: parent
        spacing: 10
        FlButton {
            id: aspectButton
            objectName: "aspectButton"
            kind: "quiet"
            text: bar.presets[bar.presetIndex].label
            anchors.verticalCenter: parent.verticalCenter
            onClicked: aspectPicker.open()
            FlListPopup {
                id: aspectPicker
                objectName: "aspectPicker"
                rowPrefix: "aspect_"
                width: 150
                y: -height - 6
                currentId: String(bar.presetIndex)
                rows: bar.presets.map((p, i) => ({ id: String(i), name: p.label, group: "", detail: "", art: "" }))
                onPicked: (id) => { bar.presetIndex = Number(id); bar.aspectChosen(bar.presets[bar.presetIndex].r); }
            }
        }
        Rectangle { width: 1; height: 22; color: "#1affffff"; anchors.verticalCenter: parent.verticalCenter }
        FlSlider {
            id: straightenSlider
            objectName: "straightenSlider"
            width: 96
            anchors.verticalCenter: parent.verticalCenter
            from: -45; to: 45; stepSize: 0.1
            bipolar: true; neutral: 0
            onValueMoved: (v) => engine.setFilmControl("straighten_deg", v)
            Binding { target: straightenSlider; property: "value"; value: bar.straighten }
            FlTip { visible: straightenHover.hovered; text: "Straighten — level a tilted horizon. Double-click resets." }
            HoverHandler { id: straightenHover }
        }
        Text {
            visible: !bar.compact
            width: 44
            anchors.verticalCenter: parent.verticalCenter
            text: (bar.straighten > 0 ? "+" : "") + bar.straighten.toFixed(1) + "°"
            color: Math.abs(bar.straighten) > 0.001 ? Theme.text : Theme.textCaption
            font.pixelSize: Theme.fontLabel
            font.features: { "tnum": 1 }
        }
        Rectangle { width: 1; height: 22; color: "#1affffff"; anchors.verticalCenter: parent.verticalCenter }
        FlIconButton {
            objectName: "rotateLeftButton"
            anchors.verticalCenter: parent.verticalCenter
            iconName: "arrow-counter-clockwise"
            tip: "Rotate left 90°"
            onClicked: engine.rotateQuadrant(-1)
        }
        FlIconButton {
            objectName: "rotateRightButton"
            anchors.verticalCenter: parent.verticalCenter
            iconName: "arrow-clockwise"
            tip: "Rotate right 90°"
            onClicked: engine.rotateQuadrant(1)
        }
        FlIconButton {
            objectName: "flipButton"
            anchors.verticalCenter: parent.verticalCenter
            iconName: "flip-horizontal"
            tip: "Flip horizontal"
            active: engine.filmControls.flip_h === true
            onClicked: engine.setFilmControl("flip_h", engine.filmControls.flip_h !== true)
        }
        FlIconButton {
            objectName: "flipVerticalButton"
            anchors.verticalCenter: parent.verticalCenter
            iconName: "flip-vertical"
            tip: "Flip vertical"
            active: engine.filmControls.flip_v === true
            onClicked: engine.setFilmControl("flip_v", engine.filmControls.flip_v !== true)
        }
        FlButton {
            objectName: "cropResetButton"
            kind: "text"
            text: "Reset"
            anchors.verticalCenter: parent.verticalCenter
            onClicked: bar.resetRequested()
        }
        FlButton {
            objectName: "cropDoneButton"
            kind: "accent"
            text: "Done"
            anchors.verticalCenter: parent.verticalCenter
            onClicked: bar.doneRequested()
        }
    }
}
