#include "dfee/raw_development.hpp"

#include "dfee/parallel.hpp"

#include <algorithm>
#include <cmath>

namespace dfee {
namespace {

constexpr float kMidGrey = 0.18F;
constexpr float kMidtonePower = 1.12F;
constexpr float kEpsilon = 1.0e-6F;

[[nodiscard]] float baseline_luminance(const float luminance) noexcept {
    const float nonnegative = std::max(luminance, 0.0F);
    return kMidGrey * std::pow(nonnegative / kMidGrey, kMidtonePower);
}

}  // namespace

Image apply_raw_baseline_develop(const Image& rgb_linear) {
    if (rgb_linear.channels != 3) {
        return rgb_linear;
    }

    Image out(rgb_linear.width, rgb_linear.height, rgb_linear.channels);
    parallel_for_rows(rgb_linear.height, [&](const int y) {
        for (int x = 0; x < rgb_linear.width; ++x) {
            const float red = std::max(rgb_linear.at(x, y, 0), 0.0F);
            const float green = std::max(rgb_linear.at(x, y, 1), 0.0F);
            const float blue = std::max(rgb_linear.at(x, y, 2), 0.0F);
            const float luminance = 0.2126F * red + 0.7152F * green + 0.0722F * blue;
            const float target_luminance = baseline_luminance(luminance);

            // Scale RGB together so a neutral RAW base does not alter hue or
            // saturation before the stock's colour response. Restrict the scale
            // to the source gamut instead of clipping individual channels.
            const float max_channel = std::max({red, green, blue});
            const float requested_scale = target_luminance / std::max(luminance, kEpsilon);
            const float gamut_safe_scale = max_channel > kEpsilon ? 1.0F / max_channel : requested_scale;
            const float scale = std::min(requested_scale, gamut_safe_scale);

            out.at(x, y, 0) = red * scale;
            out.at(x, y, 1) = green * scale;
            out.at(x, y, 2) = blue * scale;
        }
    });
    return out;
}

}  // namespace dfee
