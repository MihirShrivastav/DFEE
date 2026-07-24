#pragma once

#include "dfee/image.hpp"

namespace dfee {

// Perceptual 3-way + global colour grading (see documentation/planning/color-grading-spec.md).
// Per-zone hue is degrees (0..360), sat 0..100, lum -100..100. balance -100..100 shifts the
// shadow<->highlight midpoint; blending 0..100 widens the zone overlap; crossbalance
// -100..100 is the film split-tone shortcut (+ teal shadows / warm highlights).
// All zeros = neutral (no-op).
struct ColorGradeParams {
    float shadow_hue = 0.0F;
    float shadow_sat = 0.0F;
    float shadow_lum = 0.0F;
    float midtone_hue = 0.0F;
    float midtone_sat = 0.0F;
    float midtone_lum = 0.0F;
    float highlight_hue = 0.0F;
    float highlight_sat = 0.0F;
    float highlight_lum = 0.0F;
    float global_hue = 0.0F;
    float global_sat = 0.0F;
    float global_lum = 0.0F;
    float balance = 0.0F;
    float blending = 0.0F;
    float crossbalance = 0.0F;
};

// Applies the grade in place on linear-ish rendered RGB. Colour shifts are done in OKLab
// (luminance-preserving); neutral params leave the image byte-identical.
void apply_color_grading(Image& rendered, const ColorGradeParams& params);

}  // namespace dfee
