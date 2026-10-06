import QtQuick
import DFEE

// Label left, value right (bright only when it differs from `neutral`), slider below.
Column {
    id: row
    property string label: ""
    property real value: 0
    property real from: 0
    property real to: 100
    property real stepSize: 1
    property real neutral: 0
    property bool bipolar: false
    property int decimals: 0
    property string suffix: ""
    property bool available: true
    property string autoText: ""             // e.g. "Auto": shown instead of the number
    property var trackPalette: null
    property alias slider: s
    signal moved(real value)
    width: parent ? parent.width : 240
    spacing: 7
    opacity: available ? 1.0 : 0.4
    readonly property bool edited: Math.abs(value - neutral) > 0.0001
    Item {
        width: parent.width
        height: 15
        Text {
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            text: row.label
            color: Theme.textSecondary
            font.pixelSize: Theme.fontLabel
        }
        Text {
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            text: row.autoText.length ? row.autoText
                 : ((row.bipolar && row.value > 0 ? "+" : "") + row.value.toFixed(row.decimals) + row.suffix)
            color: row.edited && !row.autoText.length ? Theme.text : Theme.textCaption
            font.pixelSize: Theme.fontLabel
            font.features: { "tnum": 1 }
        }
    }
    FlSlider {
        id: s
        width: parent.width
        enabled: row.available
        from: row.from
        to: row.to
        stepSize: row.stepSize
        neutral: row.neutral
        bipolar: row.bipolar
        trackPalette: row.trackPalette
        onValueMoved: (v) => row.moved(v)
    }
    // A drag assigns the slider's value directly, which would break a plain
    // `value: row.value` binding; Binding re-applies on every outside change
    // (undo, reset, another stock), so the knob always follows the row.
    Binding {
        target: s
        property: "value"
        value: row.value
    }
}
