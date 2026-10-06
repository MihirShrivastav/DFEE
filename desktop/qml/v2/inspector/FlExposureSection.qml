import QtQuick
import DFEE

// Exposure: where the scene sits before the film (As shot / Auto balanced) and the
// film's own exposure.
FlInspectorSection {
    id: sec
    title: "Exposure"
    group: "exposure"
    readonly property bool asShot: engine.filmControls.exposure_placement === "as_shot"
    summary: asShot ? "As shot" : "Auto balanced"

    Item {
        width: parent.width
        height: Theme.segmentHeight + 4
        Text {
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            text: "Scene"
            color: Theme.textSecondary
            font.pixelSize: Theme.fontLabel
        }
        FlSegmented {
            objectName: "placementSegmented"
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            model: ["As shot", "Auto balanced"]
            currentIndex: sec.asShot ? 0 : 1
            onActivated: (i) => engine.setFilmControl("exposure_placement", i === 0 ? "as_shot" : "auto_balanced")
        }
        HoverHandler { id: sceneHover }
        FlTip {
            visible: sceneHover.hovered
            text: "Auto balanced sets a stock-aware starting exposure for the scene. As shot preserves the RAW's own exposure placement before the film response."
        }
    }
    FlFilmSlider {
        controlKey: "film_exposure_ev"
        label: "Film exposure"
        from: -3; to: 3; stepSize: 0.05; decimals: 2; bipolar: true
        suffix: " EV"
        tip: "Virtual exposure (in stops) reaching the film before its tone and color response — like rating the stock faster or slower."
    }
}
