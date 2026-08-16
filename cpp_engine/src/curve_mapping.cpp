#include "dfee/curve_mapping.hpp"
#include <algorithm>
namespace dfee {
CharacteristicCurve map_characteristic_curve(const CharacteristicCurve& a,
                                             float film_contrast, float highlight_rolloff, float shadow_lift) {
    // The display-referred film tone (renderer) uses gamma (luma contrast), d_min (black floor)
    // and d_max (soft white ceiling / highlight shoulder). Map the creative controls onto those.
    CharacteristicCurve c = a;
    const float con = std::clamp(film_contrast / 100.0f, 0.0f, 2.0f);        // 1.0 = neutral
    c.gamma = std::clamp(a.gamma * (0.7f + 0.3f * con), 0.3f, 3.0f);         // Film Contrast scales luma contrast
    const float roll = std::clamp(highlight_rolloff / 100.0f, 0.0f, 2.0f);   // 1.0 = neutral
    c.d_max = std::clamp(a.d_max - (roll - 1.0f) * 0.14f, 0.72f, 1.0f);      // Highlight Rolloff lowers the white ceiling
    // Shadow Lift is intentionally NOT mapped onto d_min: raising the black floor
    // lifts true black uniformly and reads as a milky fog. It is applied instead as
    // a black-preserving shadow toe in the renderer (see shadow_lift_norm). d_min
    // stays at the stock's authored fog level. (shadow_lift consumed downstream.)
    (void)shadow_lift;
    return c;
}
float map_exposure_shift(float film_exposure_ev, bool is_reversal) {
    const float bias = is_reversal ? 0.0f : 0.20f; // gentle expose-to-right for negatives; larger blows highlights
    return film_exposure_ev + bias;
}
}
