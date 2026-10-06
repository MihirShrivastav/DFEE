import QtQuick
import DFEE

// Color: the film's color character (dimmed on B&W stocks), split toning, then white
// balance and saturation.
FlInspectorSection {
    id: sec
    title: "Color"
    group: "color"
    readonly property bool mono: engine.currentStockMonochrome
    summary: mono ? "B&W film" : ""

    FlFilmSlider {
        controlKey: "film_color_density"; label: "Color density"
        from: 0; to: 200; neutral: 100; available: !sec.mono
        tip: "How dense and cohesive the film's colors are — forward for richer, deeper, more film-like color; back for a thinner, more digital look."
    }
    FlFilmSlider {
        controlKey: "emulsion_color_density"; label: "Color boost"
        from: -100; to: 100; bipolar: true; available: !sec.mono
        tip: "Overall saturation of the stock's color dyes — forward for punchier color, back for a muted look."
    }
    FlFilmSlider {
        controlKey: "crossover"; label: "Crossover"
        from: 0; to: 200; neutral: 100; available: !sec.mono
        tip: "Strength of the film's natural color crossover — the way its dye layers render cool shadows and warm highlights (and shift greens/blues). 100 is the stock's authentic amount; higher exaggerates it, 0 removes it."
    }
    FlFilmSlider {
        controlKey: "highlight_color_hold"; label: "Highlight saturation"
        from: -100; to: 100; bipolar: true; available: !sec.mono
        tip: "How much color survives in the highlights — back bleaches bright areas toward clean white (rescues blown, over-warm highlights)."
    }
    FlFilmSlider {
        controlKey: "shadow_color_retention"; label: "Shadow saturation"
        from: -100; to: 100; bipolar: true; available: !sec.mono
        tip: "How much color survives in the shadows — forward keeps darks colorful, back mutes them toward neutral."
    }
    FlFilmSlider {
        controlKey: "cg_crossbalance"; label: "Split toning"
        from: -100; to: 100; bipolar: true; available: !sec.mono
        tip: "Adds your own split-tone on top of the film — forward for teal shadows and warm highlights, back for the inverse."
    }
    Row {
        width: parent.width
        spacing: 12
        opacity: sec.mono ? 0.4 : 1.0
        enabled: !sec.mono
        FlToneSwatch { objectName: "swatch_shadow"; zone: "shadow"; label: "Shadow tint" }
        FlToneSwatch { objectName: "swatch_highlight"; zone: "highlight"; label: "Highlight tint" }
    }
    FlGroupLabel { text: "White balance" }
    FlFilmSlider {
        controlKey: "temp"; label: "Temperature"
        from: -100; to: 100; bipolar: true
        tip: "White balance warmth — forward warms (more amber), back cools (more blue)."
    }
    FlFilmSlider {
        controlKey: "tint"; label: "Tint"
        from: -100; to: 100; bipolar: true
        tip: "White balance green/magenta — forward toward magenta, back toward green."
    }
    FlFilmSlider {
        controlKey: "vibrance"; label: "Vibrance"
        from: -100; to: 100; bipolar: true
        tip: "Smart saturation that protects skin tones and already-saturated colors."
    }
    FlFilmSlider {
        controlKey: "saturation"; label: "Saturation"
        from: -100; to: 100; bipolar: true
        tip: "Overall color intensity, applied evenly to all hues."
    }
}
