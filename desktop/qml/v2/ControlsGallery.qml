import QtQuick
import QtQuick.Controls
import DFEE

// Dev-only gallery of v2 controls (FILMLAB_UI=gallery) for script checks and screenshots.
ApplicationWindow {
    id: root
    objectName: "v2Root"
    width: 640
    height: 520
    visible: true
    color: Theme.panel
    font.family: Theme.fontFamily
    function returnFocus() { keySink.forceActiveFocus(); }
    Item { id: keySink }
    property bool galleryHeaderOpen: true
    property bool gallerySwitchOn: false

    Column {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 18
        Row {
            spacing: 10
            FlButton { kind: "accent"; text: "Export…" }
            FlButton { kind: "quiet"; text: "Copy look" }
            // Changes the row from outside, as undo or a stock change would (review I2).
            FlButton { objectName: "galleryExternalReset"; kind: "text"; text: "Reset"; onClicked: contrastRow.value = 50 }
            FlButton { objectName: "galleryOpenHeader"; kind: "quiet"; text: "Open"; onClicked: root.galleryHeaderOpen = true }
            FlIconButton { iconName: "crop"; tip: "Crop (C)" }
        }
        Row {
            spacing: 12
            Repeater {
                model: ["crop", "sidebar-simple", "question", "folder-simple", "caret-down"]
                delegate: FlIcon { name: modelData; color: Theme.textSecondary }
            }
        }
        FlSegmented { objectName: "gallerySegmented"; model: ["Roll", "Films", "Looks"]; currentIndex: 1; onActivated: (i) => currentIndex = i }
        Column {
            width: 300
            spacing: 0
            FlSectionHeader { objectName: "galleryHeader"; title: "Tone"; edited: true; summary: "Off"; open: root.galleryHeaderOpen; onToggled: root.galleryHeaderOpen = !root.galleryHeaderOpen }
            Item { width: 1; height: 10 }
            Column {
                x: 16
                width: 268
                spacing: 14
                FlSliderRow {
                    id: contrastRow
                    width: parent.width
                    label: "Film contrast"
                    from: 0; to: 100; neutral: 50
                    value: 50
                    slider.objectName: "gallerySlider"
                    onMoved: (v) => value = v
                }
                FlSliderRow { width: parent.width; label: "Shadow lift"; from: -100; to: 100; neutral: 0; bipolar: true; value: 24 }
                FlSliderRow { width: parent.width; label: "Temperature"; from: -100; to: 100; neutral: 0; bipolar: true; value: -10; trackPalette: Theme.colorTrackPalette("temp") }
                FlSwitch { objectName: "gallerySwitch"; label: "Adaptive scene tone"; checked: root.gallerySwitchOn; onToggled: root.gallerySwitchOn = !root.gallerySwitchOn }
                FlFilmSlider { controlKey: "film_contrast"; label: "Film contrast (engine)"; from: 0; to: 200; neutral: 100; tip: "Bound to engine.filmControls.film_contrast." }
            }
        }
    }
}
