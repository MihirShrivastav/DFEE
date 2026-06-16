#pragma once

#include <array>
#include <string>
#include <unordered_map>
#include <utility>

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

struct ColorAnalysis {
    float sat_shadow_mean = 0.0F;
    float sat_mid_mean = 0.0F;
    float sat_highlight_mean = 0.0F;
    float sat_p95 = 0.0F;
    float neon_risk = 0.0F;
    std::array<std::string, 3> dominant_hue_bins{"Red", "Orange", "Yellow"};
    float hue_entropy = 0.0F;
    float red_orange_density = 0.0F;
    float green_yellow_density = 0.0F;
    float warm_cool_ratio = 0.0F;
    float cyan_blue_ratio = 0.0F;
    float mean_chroma = 0.0F;
};

struct SpatialAnalysis {
    float texture_density = 0.0F;
    float smooth_area_ratio = 0.0F;
    float edge_density = 0.0F;
    float digital_sharpness_score = 0.0F;
    float specular_point_ratio = 0.0F;
    float large_highlight_area_ratio = 0.0F;
};

struct SpatialMasks {
    LuminanceImage grain_receptivity_mask;
    LuminanceImage halation_source_mask;
    LuminanceImage halation_receiver_mask;
};

class ImageStateAnalyzer {
public:
    [[nodiscard]] TonalDistribution analyze_tonal(
        const LuminanceImage& luminance,
        const std::unordered_map<std::string, float>& clipping_ratios) const;

    [[nodiscard]] ZoneMasks generate_zone_masks(
        const LuminanceImage& luminance,
        float midtone_anchor) const;

    [[nodiscard]] ColorAnalysis analyze_color(
        const Image& rgb_linear,
        const ZoneMasks& zone_masks) const;

    [[nodiscard]] std::pair<SpatialAnalysis, SpatialMasks> analyze_spatial(
        const LuminanceImage& luminance) const;
};

}  // namespace dfee
