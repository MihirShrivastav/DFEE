#include "dfee/characteristic_curve.hpp"
#include <algorithm>
#include <cmath>

namespace dfee {
namespace {
constexpr float kMidOut = 0.4586f;   // pow(0.18, 1/2.2): mid-grey perceptual output
}

float curve_eval(const CharacteristicCurve& c, float logE) {
    // Gamma is the contrast index while latitude defines the useful straight-line
    // span. Treating every stock as an eight-stop curve made the authored latitude
    // field inert and could place a long negative-film toe below d_min, where the
    // renderer had no option but to flatten it.
    const float latitude = std::max(c.latitude_stops, 1.0f);
    const float s = c.gamma / latitude;                  // straight-line slope (output per stop)
    const float d_min = std::clamp(c.d_min, 0.0f, 0.9f);
    const float d_max = std::clamp(c.d_max, d_min + 0.05f, 1.0f);
    const float lin = kMidOut + s * logE;                // straight-line output
    // The C1 exponential toe joins the straight line at height (kMidOut - s*toe_onset)
    // and rolls down to d_min with curvature k = (s/foot)*toe_hardness, where
    // foot = joinHeight - d_min. If the authored toe_onset places the join right on
    // the black floor (foot -> 0) the curvature explodes and the toe collapses into a
    // FLAT crush at d_min — every shadow past the join slams to black with no
    // gradation. Guaranteeing a foot keeps the toe long and gentle so shadow detail
    // is retained; the floor (d_min) is unchanged, so blacks stay deep (no milkiness)
    // — only the gradation above them is recovered.
    //
    // The foot is stock-specific, scaled from the authored latitude so identity is
    // preserved: wide-latitude negatives (Portra, latitude ~10) get a long open toe
    // with lots of shadow gradation; short-latitude reversal stocks (Velvia, ~5.4)
    // keep a short toe that blocks up — the real difference between the two.
    const float toe_foot = std::clamp(0.035f * latitude - 0.12f, 0.05f, 0.22f);
    const float toe_limit = std::max(
        0.05f, (kMidOut - d_min - toe_foot) / std::max(s, 1.0e-4f));
    const float toe_onset = std::clamp(c.toe_onset, 0.05f, toe_limit);
    float y;
    if (logE < -toe_onset) {
        // C1 exponential toe: matches value & slope s at the join, asymptotes to d_min.
        const float y0 = kMidOut - s * toe_onset;        // straight-line value at the join
        const float foot = std::max(y0 - d_min, 1e-4f);
        const float k = (s / foot) * std::max(c.toe_hardness, 0.05f);
        y = d_min + foot * std::exp(k * (logE + toe_onset));
    } else if (logE > c.shoulder_onset) {
        // C1 exponential shoulder: asymptotes to d_max.
        const float y1 = kMidOut + s * c.shoulder_onset;
        const float head = std::max(d_max - y1, 1e-4f);
        const float k = (s / head) * std::max(c.shoulder_hardness, 0.05f);
        y = d_max - head * std::exp(-k * (logE - c.shoulder_onset));
    } else {
        y = lin;
    }
    return std::clamp(y, 0.0f, 1.0f);
}

float scene_logE(float linear_value, float midtone_anchor, float exposure_shift_stops) {
    const float v = std::max(linear_value, 1e-6f);
    const float a = std::max(midtone_anchor, 1e-6f);
    return std::log2(v / a) + exposure_shift_stops;
}
}
