pragma Singleton
import QtQuick

// Film Lab design language v2 (desktop/DESIGN.md). Every v2 color, size and radius
// comes from here; no hex literals in v2 components.
QtObject {
    // Surfaces
    readonly property color window: "#1c1c1e"
    readonly property color panel: "#202022"
    readonly property color toolbar: "#242426"
    readonly property color canvas: "#131314"
    readonly property color inset: "#161618"
    readonly property color control: "#3a3a3c"
    readonly property color controlHover: "#444447"
    readonly property color selected: "#4a4a4e"
    readonly property color rowSelected: "#14ffffff"   // rgba(255,255,255,0.08)
    readonly property color rowHover: "#0bffffff"      // rgba(255,255,255,0.045)
    readonly property color hairline: "#12ffffff"      // rgba(255,255,255,0.07)
    readonly property color card: "#2a2a2d"
    readonly property color popover: "#2a2a2d"
    readonly property color scrollThumb: "#2c2c2f"        // a shade above `panel`
    readonly property color scrollThumbHover: "#3a3a3c"
    // Accent + text
    readonly property color accent: "#0a84ff"
    readonly property color accentHover: "#409cff"
    readonly property color text: "#d4d4d8"
    readonly property color textBody: "#b8b8bd"
    readonly property color textSecondary: "#949499"
    readonly property color textCaption: "#8e8e93"
    readonly property color textTertiary: "#6e6e73"
    readonly property color textOnAccent: "#ffffff"
    readonly property color danger: "#e0655b"
    // Sliders
    readonly property color sliderTrack: "#3a3a3c"
    readonly property color sliderFill: "#636368"
    readonly property color knob: "#cfcfd4"
    readonly property color sliderKnob: "#a6a6ab"            // quieter than `knob`; the track carries the color
    // Type
    readonly property string fontFamily: "Geist"
    readonly property int fontTitle: 13
    readonly property int fontBody: 13
    readonly property int fontLabel: 12
    readonly property int fontCaption: 11
    // Geometry
    readonly property int toolbarHeight: 52
    readonly property int sidebarWidth: 232
    readonly property int inspectorWidth: 300
    readonly property int trayHeight: 184
    readonly property int controlHeight: 26
    readonly property int segmentHeight: 24
    readonly property int sectionRow: 40
    readonly property int radiusControl: 6
    readonly property int radiusTrack: 7
    readonly property int radiusSegment: 5
    readonly property int radiusTile: 6
    readonly property int radiusCard: 8
    readonly property int radiusPopover: 10
    // Motion
    readonly property int motionFast: 120
    readonly property int motionNormal: 160

    // Informational color tracks (temperature, tint, print head, HSL), muted. Each end
    // shows what moving that way does: positive temperature warms (OKLab +b) and
    // positive tint adds magenta (OKLab +a), as in Lightroom.
    function colorTrackPalette(key) {
        if (key === "temp") return { start: "#4d94b2", middle: "#6e6e6e", end: "#bd842f" };
        if (key === "tint") return { start: "#2e9c68", middle: "#6e6e6e", end: "#ad4b9b" };
        if (key === "print_c") return { start: "#b45d4b", middle: "#6e6e6e", end: "#348fa7" };
        if (key === "print_m") return { start: "#459466", middle: "#6e6e6e", end: "#b35295" };
        if (key === "print_y") return { start: "#4b78a9", middle: "#6e6e6e", end: "#c7ad39" };
        const hsl = /^hsl_([a-z]+)_([hsl])$/.exec(key || "");
        if (hsl && hslBandHue[hsl[1]] !== undefined) return hslTrack(hsl[1], hsl[2]);
        return null;
    }

    // Color mixer bands on the ordinary hue wheel, as in Lightroom and Resolve (and the
    // engine: cpp_engine/src/session.cpp apply_hsl). Order matters: neighbours are adjacent.
    readonly property var hslBands: ["red", "orange", "yellow", "green", "aqua", "blue", "purple", "magenta"]
    readonly property var hslBandHue: ({ red: 0, orange: 30, yellow: 60, green: 120,
                                         aqua: 180, blue: 240, purple: 270, magenta: 300 })
    // A color mixer track whose ends show what that slider end does: Hue reaches
    // half-way to the neighbouring band on each side; Saturation runs grey → vivid;
    // Luminance runs dark → light.
    function hslTrack(band, channel) {
        const i = hslBands.indexOf(band);
        const hue = hslBandHue[band];
        const prev = hslBandHue[hslBands[(i + 7) % 8]];
        const next = hslBandHue[hslBands[(i + 1) % 8]];
        const back = ((hue - prev) + 360) % 360 * 0.5;
        const fwd = ((next - hue) + 360) % 360 * 0.5;
        const c = (h, sat, val) => Qt.hsva((((h % 360) + 360) % 360) / 360, sat, val, 1);
        const S = 0.62, V = 0.82;
        if (channel === "h")
            return { start: c(hue - back, S, V), q1: c(hue - back / 2, S, V), middle: c(hue, S, V),
                     q3: c(hue + fwd / 2, S, V), end: c(hue + fwd, S, V) };
        if (channel === "s")
            return { start: c(hue, 0, V), q1: c(hue, S * 0.5, V), middle: c(hue, S, V),
                     q3: c(hue, Math.min(1, S * 1.3), V), end: c(hue, Math.min(1, S * 1.55), V) };
        return { start: c(hue, S, 0.38), q1: c(hue, S, 0.6), middle: c(hue, S, V),
                 q3: c(hue, S * 0.7, 0.92), end: c(hue, S * 0.4, 1.0) };
    }
}
