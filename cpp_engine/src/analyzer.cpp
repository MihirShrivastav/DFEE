#include "dfee/analyzer.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace dfee {
namespace {

[[nodiscard]] float percentile(std::vector<float> values, const float pct) {
    if (values.empty()) {
        return 0.0F;
    }
    std::ranges::sort(values);
    const float position = (pct / 100.0F) * static_cast<float>(values.size() - 1);
    const auto lower = static_cast<size_t>(std::floor(position));
    const auto upper = static_cast<size_t>(std::ceil(position));
    const float fraction = position - static_cast<float>(lower);
    return values[lower] * (1.0F - fraction) + values[upper] * fraction;
}

[[nodiscard]] float max_clip_ratio(const std::unordered_map<std::string, float>& ratios) {
    float max_value = 0.0F;
    for (const auto& [_, value] : ratios) {
        max_value = std::max(max_value, value);
    }
    return max_value;
}

}  // namespace

TonalDistribution ImageStateAnalyzer::analyze_tonal(
    const LuminanceImage& luminance,
    const std::unordered_map<std::string, float>& clipping_ratios) const {
    const std::vector<float> values(luminance.values.begin(), luminance.values.end());

    TonalDistribution result;
    result.luma_p01 = percentile(values, 1.0F);
    result.luma_p05 = percentile(values, 5.0F);
    result.luma_p25 = percentile(values, 25.0F);
    result.luma_p50 = percentile(values, 50.0F);
    result.luma_p75 = percentile(values, 75.0F);
    result.luma_p95 = percentile(values, 95.0F);
    result.luma_p99 = percentile(values, 99.0F);
    result.luma_p995 = percentile(values, 99.5F);

    const float p01_safe = std::max(result.luma_p01, 1.0e-6F);
    result.dynamic_range_stops = std::log2(std::max(result.luma_p99, p01_safe) / p01_safe);
    result.highlight_headroom = 1.0F - result.luma_p95;
    result.shadow_depth = result.luma_p05;
    result.midtone_anchor = std::max(result.luma_p50, 0.01F);
    result.contrast_index = result.luma_p75 - result.luma_p25;
    result.black_point_actual = result.luma_p01;
    result.white_point_actual = result.luma_p995;

    if (result.midtone_anchor < 0.05F) {
        result.tonal_skew = "low_key";
    } else if (result.midtone_anchor > 0.45F) {
        result.tonal_skew = "high_key";
    } else if (result.dynamic_range_stops > 11.5F) {
        result.tonal_skew = "harsh_contrast";
    } else if (result.dynamic_range_stops < 5.0F) {
        result.tonal_skew = "flat";
    }
    if (max_clip_ratio(clipping_ratios) > 0.01F) {
        result.tonal_skew = "highlight_stressed";
    }
    return result;
}

ZoneMasks ImageStateAnalyzer::generate_zone_masks(const LuminanceImage& luminance, const float midtone_anchor) const {
    constexpr std::array<float, 7> centers{-4.5F, -3.0F, -1.5F, 0.0F, 1.5F, 3.0F, 4.5F};
    constexpr float sigma = 0.85F;
    constexpr float sigma2 = 2.0F * sigma * sigma;

    ZoneMasks masks;
    for (auto& mask : masks.zones) {
        mask = LuminanceImage(luminance.width, luminance.height);
    }

    const float anchor = std::max(midtone_anchor, 1.0e-10F);
    for (size_t i = 0; i < luminance.values.size(); ++i) {
        const float y = std::max(luminance.values[i], 1.0e-10F);
        const float ev = std::log2(y / anchor);
        std::array<float, 7> weights{};
        float sum = 0.0F;
        for (size_t zone = 0; zone < centers.size(); ++zone) {
            const float delta = ev - centers[zone];
            float weight = std::exp(-(delta * delta) / sigma2);
            if (zone == 0 && ev <= centers[zone]) {
                weight = 1.0F;
            } else if (zone == centers.size() - 1 && ev >= centers[zone]) {
                weight = 1.0F;
            }
            weights[zone] = weight;
            sum += weight;
        }
        sum = std::max(sum, 1.0e-12F);
        for (size_t zone = 0; zone < centers.size(); ++zone) {
            masks.zones[zone].values[i] = weights[zone] / sum;
        }
    }
    return masks;
}

}  // namespace dfee
