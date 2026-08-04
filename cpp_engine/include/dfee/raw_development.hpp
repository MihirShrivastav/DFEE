#pragma once

#include "dfee/image.hpp"

namespace dfee {

// Converts a scene-linear RAW working image into DFEE's neutral baseline before
// the chosen stock is applied. It intentionally changes luminance only: film
// profiles own the hue and chroma signature.
[[nodiscard]] Image apply_raw_baseline_develop(const Image& rgb_linear);

}  // namespace dfee
