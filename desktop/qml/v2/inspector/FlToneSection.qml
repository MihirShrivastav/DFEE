import QtQuick
import DFEE

// Tone: the film's tone curve — rolloff, contrast, shadow lift — and how much of the
// photo's developed tone it keeps.
FlInspectorSection {
    title: "Tone"
    group: "tone"
    FlFilmSlider {
        controlKey: "highlight_rolloff"; label: "Highlight rolloff"
        from: 0; to: 200; neutral: 100
        tip: "How gently the brightest tones roll off instead of clipping — higher for softer, glowier film highlights."
    }
    FlFilmSlider {
        controlKey: "film_contrast"; label: "Film contrast"
        from: 0; to: 200; neutral: 100
        tip: "The punch of the film's tone curve — higher for a deeper, more contrasty look; lower for flatter."
    }
    FlFilmSlider {
        controlKey: "shadow_lift"; label: "Shadow lift"
        from: -100; to: 100; bipolar: true
        tip: "Base-fog fade in the deepest shadows, the way negative film never quite reaches pure black. Forward lifts shadows into a soft matte; back deepens them toward true black."
    }
    FlFilmSlider {
        controlKey: "rendered_input"; label: "Preserve rendered tone"
        from: 0; to: 100; neutral: 80
        tip: "Higher keeps the photo's developed exposure and tone and applies the film look gently, protecting skies and bright highlights from being pushed again. Lower lets the film's full tone curve through."
    }
    FlSwitch {
        objectName: "adaptiveSwitch"
        label: "Adaptive scene tone"
        checked: engine.filmControls.adaptive === true
        onToggled: engine.setFilmControl("adaptive", !checked)
        tip: "Lets the film read the scene and auto-adjust its tone for flat, high-dynamic-range files. Turn off for a fixed, predictable response."
    }
}
