#include "dfee/curve_mapping.hpp"
#include <algorithm>
namespace dfee {
CharacteristicCurve map_characteristic_curve(const CharacteristicCurve& a,
                                             float film_contrast, float highlight_rolloff, float shadow_lift) {
    CharacteristicCurve c = a;
    const float con = std::clamp(film_contrast / 100.0f, 0.0f, 2.0f);       // 1.0 = neutral
    c.gamma = std::clamp(a.gamma * (0.6f + 0.4f * con), 0.3f, 3.5f);        // +-40% around authored at ends
    const float roll = std::clamp(highlight_rolloff / 100.0f, 0.0f, 2.0f);  // 1.0 = neutral
    c.shoulder_onset = std::clamp(a.shoulder_onset - (roll - 1.0f) * 0.8f, 0.4f, 6.0f); // more roll = earlier shoulder
    const float lift = std::clamp(shadow_lift / 100.0f, -1.0f, 1.0f);
    if (lift > 0.0f) { c.d_min = std::clamp(a.d_min + lift * 0.10f, 0.0f, 0.30f); }        // matte lift
    else             { c.toe_onset = std::clamp(a.toe_onset + (-lift) * 0.8f, 0.4f, 6.0f); } // crush deeper
    return c;
}
float map_exposure_shift(float film_exposure_ev, bool is_reversal) {
    const float bias = is_reversal ? 0.0f : 0.66f; // negatives designed to over-expose ~2/3 stop
    return film_exposure_ev + bias;
}
}
