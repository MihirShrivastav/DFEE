import QtQuick
import DFEE

// Grain & light: grain (matched to film speed, or by hand), halation and bloom.
FlInspectorSection {
    id: sec
    title: "Grain & light"
    group: "grain_light"
    readonly property bool autoGrain: engine.filmControls.grain_auto === true
    summary: autoGrain ? "Auto grain" : ""

    FlGroupLabel { text: "Grain" }
    FlSwitch {
        objectName: "grainAutoSwitch"
        label: engine.grainResolving ? "Resolving stock grain…" : "Match grain to film speed"
        checked: sec.autoGrain
        enabled: !engine.grainResolving
        onToggled: engine.setAutoGrain(!checked)
        tip: "Automatically matches grain to the film speed (ISO) and stock. Turn off to seed and edit Strength, Size and Roughness manually."
    }
    FlFilmSlider {
        controlKey: "grain_strength"; label: "Strength"
        from: 0; to: 2; stepSize: 0.05; decimals: 2
        available: !sec.autoGrain; autoValue: sec.autoGrain
        tip: "How visible the grain is — the apparent film speed."
    }
    FlFilmSlider {
        controlKey: "grain_size"; label: "Size"
        from: 0.1; to: 2; stepSize: 0.05; decimals: 2; neutral: 0.6
        available: !sec.autoGrain; autoValue: sec.autoGrain
        tip: "Particle size — larger reads as a coarser, higher-ISO stock."
    }
    FlFilmSlider {
        controlKey: "grain_roughness"; label: "Roughness"
        from: 0; to: 1; stepSize: 0.05; decimals: 2; neutral: 0.5
        available: !sec.autoGrain; autoValue: sec.autoGrain
        tip: "Irregularity of the grain clumping — higher is grittier and more organic, lower is finer and more even."
    }
    FlGroupLabel { text: "Halation" }
    FlFilmSlider {
        controlKey: "halation_strength"; label: "Strength"
        from: 0; to: 200; neutral: 100
        tip: "Strength of the warm red-orange glow that bleeds around bright edges against dark backgrounds."
    }
    FlFilmSlider {
        controlKey: "halation_threshold"; label: "Threshold"
        from: 0; to: 100; neutral: 50
        tip: "How bright an area must be before it starts to halate — higher restricts the glow to the brightest highlights."
    }
    FlGroupLabel { text: "Bloom" }
    FlFilmSlider {
        controlKey: "bloom"; label: "Amount"
        from: 0; to: 100
        tip: "Soft optical glow spreading from the highlights, like light diffusing in the lens."
    }
}
