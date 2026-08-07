#pragma once
#include "dfee/characteristic_curve.hpp"
namespace dfee {
CharacteristicCurve map_characteristic_curve(const CharacteristicCurve& authored,
                                             float film_contrast, float highlight_rolloff, float shadow_lift);
float map_exposure_shift(float film_exposure_ev, bool is_reversal);
}
