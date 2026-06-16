#pragma once

#include "dfee/image.hpp"

namespace dfee {

[[nodiscard]] Image rgb_to_oklab(const Image& rgb);
[[nodiscard]] Image oklab_to_rgb(const Image& oklab);
[[nodiscard]] Image oklab_to_oklch(const Image& oklab);
[[nodiscard]] Image oklch_to_oklab(const Image& oklch);

}  // namespace dfee
