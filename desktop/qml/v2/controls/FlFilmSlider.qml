import QtQuick
import DFEE

// One engine control as a slider row: shows engine.filmControls[controlKey] and
// writes drags through engine.setFilmControl. Row objectName "ctl_<key>", slider
// "slider_<key>". autoValue (Auto grain) shows "Auto" with the knob at neutral.
FlSliderRow {
    id: fs
    property string controlKey: ""
    property bool autoValue: false
    objectName: "ctl_" + controlKey
    slider.objectName: "slider_" + controlKey
    readonly property real engineValue: Number(engine.filmControls[controlKey])
    value: autoValue ? neutral : engineValue
    autoText: autoValue ? "Auto" : ""
    trackPalette: Theme.colorTrackPalette(controlKey)
    onMoved: (v) => engine.setFilmControl(fs.controlKey, v)
}
