import QtQuick
import DFEE

// Hue/saturation wheel for one grading zone (cg_<zone>_hue / _sat), written as one
// step through engine.setGradeColor. Drag from the centre to tint; double-click
// clears. Ported from v1 Main.qml ColorWheel.
Item {
    id: wheel
    property string zone: ""
    property string label: ""
    property int diameter: 96
    // Implicit size too, so a Popup that hosts the wheel (FlToneSwatch) sizes to it.
    implicitWidth: diameter
    implicitHeight: diameter + 20
    width: diameter
    height: diameter + 20
    readonly property real hueVal: Number(engine.filmControls["cg_" + zone + "_hue"])
    readonly property real satVal: Number(engine.filmControls["cg_" + zone + "_sat"])

    function pick(mx, my) {
        const c = wheel.diameter / 2;
        const dx = mx - c, dy = my - c;
        let ang = Math.atan2(-dy, dx) * 180 / Math.PI;
        if (ang < 0) ang += 360;
        const rad = Math.min(Math.sqrt(dx * dx + dy * dy) / c, 1.0);
        engine.setGradeColor(wheel.zone, Math.round(ang), Math.round(rad * 100));
    }

    Rectangle {
        id: disc
        objectName: wheel.objectName.length > 0 ? wheel.objectName + "Disc" : ""
        width: wheel.diameter
        height: wheel.diameter
        radius: wheel.diameter / 2
        anchors.horizontalCenter: parent.horizontalCenter
        color: Theme.inset
        border.width: 1
        border.color: Theme.hairline
        clip: true
        Canvas {
            anchors.fill: parent
            onPaint: {
                const ctx = getContext("2d");
                ctx.reset();
                const w = width, cx = w / 2, cy = w / 2, r = w / 2;
                for (let a = 0; a < 360; a += 3) {
                    ctx.beginPath();
                    ctx.moveTo(cx, cy);
                    ctx.arc(cx, cy, r, (-(a + 3)) * Math.PI / 180, (-a) * Math.PI / 180, false);
                    ctx.closePath();
                    ctx.fillStyle = "hsl(" + a + ",60%,50%)";
                    ctx.fill();
                }
                const g = ctx.createRadialGradient(cx, cy, 0, cx, cy, r);
                g.addColorStop(0.0, "rgba(22,22,24,1.0)");
                g.addColorStop(0.35, "rgba(22,22,24,0.4)");
                g.addColorStop(1.0, "rgba(22,22,24,0.0)");
                ctx.fillStyle = g;
                ctx.beginPath();
                ctx.arc(cx, cy, r, 0, 2 * Math.PI);
                ctx.fill();
            }
        }
        Rectangle {
            width: 13
            height: 13
            radius: 7
            border.width: 2
            border.color: Theme.knob
            x: disc.width / 2 + Math.cos(wheel.hueVal * Math.PI / 180) * (wheel.satVal / 100) * (disc.width / 2 - 9) - width / 2
            y: disc.height / 2 - Math.sin(wheel.hueVal * Math.PI / 180) * (wheel.satVal / 100) * (disc.height / 2 - 9) - height / 2
            color: wheel.satVal > 0
                ? Qt.hsla((((wheel.hueVal % 360) + 360) % 360) / 360, Math.min(wheel.satVal / 100, 1.0), 0.55, 1.0)
                : Theme.control
        }
        MouseArea {
            id: drag
            anchors.fill: parent
            preventStealing: true
            cursorShape: Qt.CrossCursor
            onPressed: (mouse) => wheel.pick(mouse.x, mouse.y)
            onPositionChanged: (mouse) => { if (pressed) wheel.pick(mouse.x, mouse.y); }
            onDoubleClicked: engine.setGradeColor(wheel.zone, 0, 0)
        }
        HoverHandler { id: wheelHover }
        FlTip {
            visible: wheelHover.hovered && !drag.pressed
            text: "Tints the " + wheel.label.toLowerCase() + " — drag from the centre to add color, double-click to reset."
        }
    }
    Text {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        text: wheel.label
        color: Theme.textSecondary
        font.pixelSize: Theme.fontCaption
    }
}
