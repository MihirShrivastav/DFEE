#pragma once

#include "dfee/image.hpp"

namespace dfee {

// Converts a scene-linear RAW working image into DFEE's neutral baseline before
// the chosen stock is applied. It intentionally changes luminance only: film
// profiles own the hue and chroma signature.
[[nodiscard]] Image apply_raw_baseline_develop(const Image& rgb_linear);

// The production default is 1.28. A bounded DFEE_RAW_BASELINE_POWER override is
// intentionally available for the offline calibration harness only; it is not a
// user-facing editing control.
[[nodiscard]] float raw_baseline_midtone_power() noexcept;

}  // namespace dfee
