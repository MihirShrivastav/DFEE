#include "dfee/raw_development.hpp"

#include "dfee/parallel.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace dfee {
namespace {

constexpr float kMidGrey = 0.18F;
// Calibrated against 20 edit-free Lightroom Adobe Standard TIFF references. 1.28
// improves RAW tone agreement while retaining highlight headroom; see M6-010.
constexpr float kMidtonePower = 1.28F;
constexpr float kEpsilon = 1.0e-6F;

[[nodiscard]] float resolved_midtone_power() noexcept {
    const char* const raw_value = std::getenv("DFEE_RAW_BASELINE_POWER");
    if (raw_value == nullptr) {
        return kMidtonePower;
    }

    char* end = nullptr;
    const float parsed = std::strtof(raw_value, &end);
    if (end == raw_value || *end != '\0' || !std::isfinite(parsed)) {
        return kMidtonePower;
    }
    return std::clamp(parsed, 0.90F, 1.40F);
}

[[nodiscard]] float baseline_luminance(const float luminance, const float midtone_power) noexcept {
    const float nonnegative = std::max(luminance, 0.0F);
    return kMidGrey * std::pow(nonnegative / kMidGrey, midtone_power);
}

}  // namespace

float raw_baseline_midtone_power() noexcept {
    return resolved_midtone_power();
}

Image apply_raw_baseline_develop(const Image& rgb_linear) {
    if (rgb_linear.channels != 3) {
        return rgb_linear;
    }

    const float midtone_power = resolved_midtone_power();
    Image out(rgb_linear.width, rgb_linear.height, rgb_linear.channels);
    parallel_for_rows(rgb_linear.height, [&](const int y) {
        for (int x = 0; x < rgb_linear.width; ++x) {
            const float red = std::max(rgb_linear.at(x, y, 0), 0.0F);
            const float green = std::max(rgb_linear.at(x, y, 1), 0.0F);
            const float blue = std::max(rgb_linear.at(x, y, 2), 0.0F);
            const float luminance = 0.2126F * red + 0.7152F * green + 0.0722F * blue;
            const float target_luminance = baseline_luminance(luminance, midtone_power);

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
