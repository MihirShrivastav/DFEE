#pragma once
namespace dfee {
struct CharacteristicCurve {
    float gamma = 1.0f;             // rendered contrast index (mid-slope)
    float latitude_stops = 8.0f;    // reference span; sets stops-per-output via kStopsRef
    float toe_onset = 2.0f;         // stops below mid where the toe join begins
    float toe_hardness = 1.0f;      // knee tightness (1 = C1 continuous)
    float shoulder_onset = 2.0f;    // stops above mid where the shoulder join begins
    float shoulder_hardness = 1.0f;
    float d_min = 0.0f;             // rendered black floor (perceptual)
    float d_max = 1.0f;             // rendered white ceiling (perceptual)
};
// Perceptual tone in [0,1] for scene log-exposure `logE` (stops from mid-grey).
float curve_eval(const CharacteristicCurve& c, float logE);
// Scene log-exposure of a linear value relative to the metered mid-grey anchor, plus shift.
float scene_logE(float linear_value, float midtone_anchor, float exposure_shift_stops);
}
