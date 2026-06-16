#pragma once

#include <array>
#include <string>
#include <unordered_map>

#include "dfee/image.hpp"

namespace dfee {

struct TonalDistribution {
    float luma_p01 = 0.0F;
    float luma_p05 = 0.0F;
    float luma_p25 = 0.0F;
    float luma_p50 = 0.0F;
    float luma_p75 = 0.0F;
    float luma_p95 = 0.0F;
    float luma_p99 = 0.0F;
    float luma_p995 = 0.0F;
    float dynamic_range_stops = 0.0F;
    float midtone_anchor = 0.18F;
    float highlight_headroom = 0.0F;
    float shadow_depth = 0.0F;
    std::string tonal_skew = "normal";
    float contrast_index = 0.0F;
    float black_point_actual = 0.0F;
    float white_point_actual = 0.0F;
};

struct ZoneMasks {
    std::array<LuminanceImage, 7> zones;
};

class ImageStateAnalyzer {
public:
    [[nodiscard]] TonalDistribution analyze_tonal(
        const LuminanceImage& luminance,
        const std::unordered_map<std::string, float>& clipping_ratios) const;

    [[nodiscard]] ZoneMasks generate_zone_masks(
        const LuminanceImage& luminance,
        float midtone_anchor) const;
};

}  // namespace dfee
