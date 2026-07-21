#pragma once

#include "dfee/image.hpp"

namespace dfee {

// Scene-referred EV-masked tone stage (filmic_v3). Region controls (Shadows/Midtones/
// Highlights) are EV dodge/burn through smooth luminance-zone masks applied as a uniform
// RGB multiply (hue/chroma-ratio preserving). Whites/Blacks set the endpoints; Contrast
// is a pivot power around mid-grey. Operates in place on linear (scene-referred) RGB.
// Control values are the raw slider units (-100..+100; 0 = no-op).
// See documentation/planning/tone-controls-rebuild-spec.md.
void apply_scene_referred_tone(
    Image& adjusted,
    float contrast_value,
    float highlights_value,
    float shadows_value,
    float whites_value,
    float blacks_value,
    float midtones_value);

}  // namespace dfee
