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
        if (hsl && hslBandHue[hsl[1]] !== undefined) return hslTrack(hslBandHue[hsl[1]], hsl[2]);
        return null;
    }

    // Color mixer bands: the OKLCh hue each engine band is centred on
    // (cpp_engine/src/session.cpp apply_hsl).
    readonly property var hslBandHue: ({ red: 0, orange: 30, yellow: 60, green: 120,
                                         aqua: 180, blue: 240, purple: 285, magenta: 330 })
    // A color mixer track drawn from the engine's own maths, so each end shows what
    // that slider end does to its band: Hue rotates OKLCh hue by up to ±50°,
    // Saturation scales chroma ×0…×2, Luminance moves OKLab L by ±0.22.
    function hslTrack(hue, channel) {
        const L = 0.68, C = 0.12;
        if (channel === "h")
            return { start: oklch(L, C, hue - 50), q1: oklch(L, C, hue - 25), middle: oklch(L, C, hue),
                     q3: oklch(L, C, hue + 25), end: oklch(L, C, hue + 50) };
        if (channel === "s")
            return { start: oklch(L, 0, hue), q1: oklch(L, C * 0.5, hue), middle: oklch(L, C, hue),
                     q3: oklch(L, C * 1.5, hue), end: oklch(L, C * 2, hue) };
        return { start: oklch(L - 0.22, C, hue), q1: oklch(L - 0.11, C, hue), middle: oklch(L, C, hue),
                 q3: oklch(L + 0.11, C, hue), end: oklch(L + 0.22, C, hue) };
    }
    // OKLCh (L 0…1, chroma, hue in degrees) → sRGB color; chroma is reduced until the
    // color fits the sRGB gamut so the track never shows clipped, wrong-hue colors.
    function oklch(L, C, hueDeg) {
        const h = hueDeg * Math.PI / 180;
        for (let c = C; c >= 0; c -= 0.005) {
            const a = c * Math.cos(h), b = c * Math.sin(h);
            const l_ = L + 0.3963377774 * a + 0.2158037573 * b;
            const m_ = L - 0.1055613458 * a - 0.0638541728 * b;
            const s_ = L - 0.0894841775 * a - 1.2914855480 * b;
            const l = l_ * l_ * l_, m = m_ * m_ * m_, s = s_ * s_ * s_;
            const rgb = [4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s,
                         -1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s,
                         -0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s];
            if (rgb.every(v => v >= -0.0005 && v <= 1.0005) || c <= 0) {
                const enc = rgb.map(v => {
                    const x = Math.max(0, Math.min(1, v));
                    return x <= 0.0031308 ? 12.92 * x : 1.055 * Math.pow(x, 1 / 2.4) - 0.055;
                });
                return Qt.rgba(enc[0], enc[1], enc[2], 1);
            }
        }
        return Qt.rgba(L, L, L, 1);
    }
}
