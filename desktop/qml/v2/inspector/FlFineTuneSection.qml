import QtQuick
import DFEE

// Fine-tune (collapsed by default): the generic grade on top of the film — basic
// tone, detail, the HSL color mixer and the color grading wheels.
FlInspectorSection {
    id: sec
    title: "Fine-tune"
    group: "fine_tune"
    property int hslIndex: 0                 // 0 hue, 1 saturation, 2 luminance
    readonly property string hslSuffix: ["h", "s", "l"][hslIndex]

    FlGroupLabel { text: "Basic tone" }
    FlFilmSlider {
        controlKey: "exposure"; label: "Exposure"
        from: -3; to: 3; stepSize: 0.05; decimals: 2; bipolar: true; suffix: " EV"
        tip: "Overall brightness of the finished image, in stops — a grade applied after the film response. For the film's own exposure (which drives its tone and rolloff), use Film exposure in the Exposure section."
    }
    FlFilmSlider { controlKey: "contrast"; label: "Contrast"; from: -100; to: 100; bipolar: true; tip: "Global contrast — spreads or compresses the tonal range around the midtones." }
    FlFilmSlider { controlKey: "highlights"; label: "Highlights"; from: -100; to: 100; bipolar: true; tip: "Recovers or brightens the brighter tones without moving whites." }
    FlFilmSlider { controlKey: "shadows"; label: "Shadows"; from: -100; to: 100; bipolar: true; tip: "Opens or deepens the darker tones without moving blacks." }
    FlFilmSlider { controlKey: "whites"; label: "Whites"; from: -100; to: 100; bipolar: true; tip: "Sets the white clipping point — how bright the brightest tones become." }
    FlFilmSlider { controlKey: "blacks"; label: "Blacks"; from: -100; to: 100; bipolar: true; tip: "Sets the black clipping point — how deep the darkest tones become." }
    FlFilmSlider { controlKey: "midtones"; label: "Midtones"; from: -100; to: 100; bipolar: true; tip: "Brightness of the mid-tones, leaving the extremes anchored." }

    FlGroupLabel { text: "Detail" }
    FlFilmSlider { controlKey: "texture"; label: "Texture"; from: -100; to: 100; bipolar: true; tip: "Medium-scale detail like skin and foliage — forward enhances, back smooths." }
    FlFilmSlider { controlKey: "clarity"; label: "Clarity"; from: -100; to: 100; bipolar: true; tip: "Midtone local contrast — forward adds punch and presence, back softens." }
    FlFilmSlider { controlKey: "dehaze"; label: "Dehaze"; from: -100; to: 100; bipolar: true; tip: "Cuts or adds atmospheric haze and low-contrast veiling." }
    FlFilmSlider { controlKey: "sharpness"; label: "Sharpening"; from: 0; to: 2; stepSize: 0.05; decimals: 2; tip: "Edge sharpening amount." }
    FlFilmSlider { controlKey: "sharpness_mask"; label: "Sharpening mask"; from: 0; to: 1; stepSize: 0.05; decimals: 2; neutral: 0.5; tip: "Limits sharpening to edges, protecting smooth areas (like skies) from being sharpened into noise." }

    FlGroupLabel { text: "Color mixer" }
    FlSegmented {
        objectName: "hslSegmented"
        model: ["Hue", "Saturation", "Luminance"]
        currentIndex: sec.hslIndex
        onActivated: (i) => sec.hslIndex = i
    }
    Repeater {
        model: [
            { key: "red", label: "Red" }, { key: "orange", label: "Orange" },
            { key: "yellow", label: "Yellow" }, { key: "green", label: "Green" },
            { key: "aqua", label: "Aqua" }, { key: "blue", label: "Blue" },
            { key: "purple", label: "Purple" }, { key: "magenta", label: "Magenta" }
        ]
        delegate: FlFilmSlider {
            width: parent.width
            controlKey: "hsl_" + modelData.key + "_" + sec.hslSuffix
            label: modelData.label
            from: -100; to: 100; bipolar: true
        }
    }

    FlGroupLabel { text: "Color grading" }
    Grid {
        anchors.horizontalCenter: parent.horizontalCenter
        columns: 2
        columnSpacing: 28
        rowSpacing: 12
        FlColorWheel { objectName: "wheel_shadow"; zone: "shadow"; label: "Shadows"; diameter: 104 }
        FlColorWheel { objectName: "wheel_midtone"; zone: "midtone"; label: "Midtones"; diameter: 104 }
        FlColorWheel { objectName: "wheel_highlight"; zone: "highlight"; label: "Highlights"; diameter: 104 }
        FlColorWheel { objectName: "wheel_global"; zone: "global"; label: "Global"; diameter: 104 }
    }
    FlFilmSlider { controlKey: "cg_shadow_lum"; label: "Shadow luminance"; from: -100; to: 100; bipolar: true; tip: "Brightness of the shadow zone only." }
    FlFilmSlider { controlKey: "cg_midtone_lum"; label: "Midtone luminance"; from: -100; to: 100; bipolar: true; tip: "Brightness of the midtone zone only." }
    FlFilmSlider { controlKey: "cg_highlight_lum"; label: "Highlight luminance"; from: -100; to: 100; bipolar: true; tip: "Brightness of the highlight zone only." }
    FlFilmSlider { controlKey: "cg_global_lum"; label: "Global luminance"; from: -100; to: 100; bipolar: true; tip: "Overall brightness applied by the grade." }
    FlFilmSlider { controlKey: "cg_balance"; label: "Balance"; from: -100; to: 100; bipolar: true; tip: "Shifts where shadows end and highlights begin, weighting the grade toward darks or lights." }
    FlFilmSlider { controlKey: "cg_blending"; label: "Blending"; from: 0; to: 100; tip: "How softly the shadow, midtone and highlight zones overlap." }
}
