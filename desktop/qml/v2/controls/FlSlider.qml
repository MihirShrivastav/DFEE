import QtQuick
import QtQuick.Controls
import DFEE

// v2 slider. Ports v1 InspectorSlider behaviour: click/drag sets, modifier keys give
// precision drag, double-click resets to `neutral`, Left/Right nudge. Restyled per
// DESIGN.md v2: 4px track, tempered fill (from a centre tick when bipolar), 14px knob,
// accent focus ring.
Slider {
    id: control
    // MainV2.arrowKeysFree: a focused slider keeps Left/Right for nudging.
    readonly property bool keepsArrowKeys: true
    // The accent ring is for keyboard focus only (Tab), never after a mouse click.
    readonly property bool showsFocusRing: visualFocus
    property real neutral: 0
    property bool bipolar: false
    property var trackPalette: null          // {start, middle, end} for informational tracks
    // Not `moved`: Slider already declares a parameterless moved() signal.
    signal valueMoved(real value)
    implicitHeight: 18
    padding: 0

    function clampValue(v) { return Math.max(from, Math.min(to, v)); }
    function modifierScale(m) {
        if ((m & Qt.ControlModifier) && (m & Qt.ShiftModifier)) return 0.01;
        if (m & Qt.AltModifier) return 0.05;
        if (m & Qt.ShiftModifier) return 0.10;
        if (m & Qt.ControlModifier) return 0.25;
        return 1.0;
    }
    readonly property real step: stepSize > 0 ? stepSize : 1
    function commit(v) {
        const next = clampValue(Math.round(v / step) * step);
        value = next;
        valueMoved(next);
    }
    function fromMouse(mouse, precise, startX, startValue) {
        const span = to - from;
        commit(precise
            ? startValue + ((mouse.x - startX) / Math.max(1, width)) * span * modifierScale(mouse.modifiers)
            : from + Math.max(0, Math.min(1, mouse.x / Math.max(1, width))) * span);
    }
    readonly property real neutralPos: Math.max(0, Math.min(1, (neutral - from) / Math.max(0.0001, to - from)))

    background: Item {
        x: 0
        width: control.width
        height: control.height
        Rectangle {                                   // track
            id: track
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width
            height: 4
            radius: 2
            color: Theme.sliderTrack
            Rectangle {                               // informational color track
                anchors.fill: parent
                radius: 2
                visible: control.trackPalette !== null
                opacity: 0.6
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0; color: control.trackPalette ? control.trackPalette.start : "transparent" }
                    GradientStop { position: 0.5; color: control.trackPalette ? control.trackPalette.middle : "transparent" }
                    GradientStop { position: 1; color: control.trackPalette ? control.trackPalette.end : "transparent" }
                }
            }
            Rectangle {                               // amount
                readonly property real a: control.bipolar ? Math.min(control.visualPosition, control.neutralPos) : 0
                readonly property real b: control.bipolar ? Math.max(control.visualPosition, control.neutralPos) : control.visualPosition
                visible: control.trackPalette === null
                x: a * parent.width
                width: Math.max(0, (b - a) * parent.width)
                height: parent.height
                radius: 2
                color: Theme.sliderFill
            }
        }
        Rectangle {                                   // centre tick (bipolar)
            visible: control.bipolar
            x: control.neutralPos * parent.width
            anchors.verticalCenter: parent.verticalCenter
            width: 1
            height: 10
            color: "#40ffffff"
        }
    }

    handle: Rectangle {
        x: control.visualPosition * (control.width - width)
        anchors.verticalCenter: parent.verticalCenter
        width: 14
        height: 14
        radius: 7
        color: Theme.knob
        border.width: control.showsFocusRing ? 3 : 0
        border.color: "#730a84ff"                      // accent focus ring at ~45%
        Rectangle { z: -1; anchors.fill: parent; anchors.topMargin: 1; anchors.bottomMargin: -1; radius: 7; color: "#80000000" }
    }

    MouseArea {
        anchors.fill: parent
        anchors.topMargin: -6
        anchors.bottomMargin: -6
        preventStealing: true
        cursorShape: Qt.PointingHandCursor
        property real startX: 0
        property real startValue: 0
        onPressed: (mouse) => {
            control.forceActiveFocus();
            startX = mouse.x;
            startValue = control.value;
            control.fromMouse(mouse, mouse.modifiers !== Qt.NoModifier, startX, startValue);
        }
        onPositionChanged: (mouse) => {
            if (pressed) control.fromMouse(mouse, mouse.modifiers !== Qt.NoModifier, startX, startValue);
        }
        onDoubleClicked: control.commit(control.neutral)
    }
    Keys.onLeftPressed: (e) => { control.commit(control.value - control.step * control.modifierScale(e.modifiers)); e.accepted = true; }
    Keys.onRightPressed: (e) => { control.commit(control.value + control.step * control.modifierScale(e.modifiers)); e.accepted = true; }
    // Esc gives the arrow keys back to photo navigation.
    Keys.onEscapePressed: (e) => {
        const w = control.Window.window;
        if (w && w.returnFocus) w.returnFocus();
        e.accepted = true;
    }
}
