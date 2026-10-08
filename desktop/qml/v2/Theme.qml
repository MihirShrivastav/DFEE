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
        return null;
    }
}
