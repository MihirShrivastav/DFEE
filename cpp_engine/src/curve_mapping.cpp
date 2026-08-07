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
    const float lift = std::clamp(shadow_lift / 100.0f, -1.0f, 1.0f);
    c.d_min = std::clamp(a.d_min + lift * 0.06f, 0.0f, 0.30f);               // Shadow Lift raises/lowers the black floor
    return c;
}
float map_exposure_shift(float film_exposure_ev, bool is_reversal) {
    const float bias = is_reversal ? 0.0f : 0.20f; // gentle expose-to-right for negatives; larger blows highlights
    return film_exposure_ev + bias;
}
}
