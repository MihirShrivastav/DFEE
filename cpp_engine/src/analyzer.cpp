#include "dfee/analyzer.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
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

[[nodiscard]] std::array<float, 3> rgb_to_oklch_pixel(const float r, const float g, const float b) {
    const float rs = std::max(r, 1.0e-12F);
    const float gs = std::max(g, 1.0e-12F);
    const float bs = std::max(b, 1.0e-12F);

    const float l = 0.4122214708F * rs + 0.5363325363F * gs + 0.0514459929F * bs;
    const float m = 0.2119034982F * rs + 0.6806995451F * gs + 0.1073969566F * bs;
    const float s = 0.0883024619F * rs + 0.2817188376F * gs + 0.6299787005F * bs;

    const float lp = std::cbrt(l);
    const float mp = std::cbrt(m);
    const float sp = std::cbrt(s);

    const float a = 1.9779984951F * lp - 2.4285922050F * mp + 0.4505937099F * sp;
    const float b_lab = 0.0259040371F * lp + 0.7827717612F * mp - 0.8086757983F * sp;
    float hue = std::atan2(b_lab, a);
    if (hue < 0.0F) {
        hue += 2.0F * std::numbers::pi_v<float>;
    }

    return {
        0.2104542553F * lp + 0.7936177850F * mp - 0.0040720468F * sp,
        std::sqrt(a * a + b_lab * b_lab),
        hue,
    };
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

ColorAnalysis ImageStateAnalyzer::analyze_color(const Image& rgb_linear, const ZoneMasks& zone_masks) const {
    if (rgb_linear.channels != 3) {
        throw std::invalid_argument("analyze_color expects a 3-channel RGB image");
    }
    for (const auto& zone : zone_masks.zones) {
        if (zone.width != rgb_linear.width || zone.height != rgb_linear.height) {
            throw std::invalid_argument("analyze_color expects zone masks to match the RGB image dimensions");
        }
    }

    std::vector<float> chroma_values;
    chroma_values.reserve(rgb_linear.pixel_count());

    float chroma_sum = 0.0F;
    float shadow_weight_sum = 0.0F;
    float mid_weight_sum = 0.0F;
    float highlight_weight_sum = 0.0F;
    float shadow_chroma_sum = 0.0F;
    float mid_chroma_sum = 0.0F;
    float highlight_chroma_sum = 0.0F;
    std::size_t neon_pixels = 0;
    std::array<float, 12> hue_counts{};

    for (std::size_t i = 0; i < rgb_linear.pixel_count(); ++i) {
        const auto oklch = rgb_to_oklch_pixel(
            rgb_linear.pixels[i * 3 + 0],
            rgb_linear.pixels[i * 3 + 1],
            rgb_linear.pixels[i * 3 + 2]);

        const float chroma = oklch[1];
        chroma_values.push_back(chroma);
        chroma_sum += chroma;
        neon_pixels += chroma > 0.18F ? 1U : 0U;

        const float shadow_weight = zone_masks.zones[1].values[i];
        const float mid_weight = zone_masks.zones[3].values[i];
        const float highlight_weight = zone_masks.zones[5].values[i];
        shadow_weight_sum += shadow_weight;
        mid_weight_sum += mid_weight;
        highlight_weight_sum += highlight_weight;
        shadow_chroma_sum += chroma * shadow_weight;
        mid_chroma_sum += chroma * mid_weight;
        highlight_chroma_sum += chroma * highlight_weight;

        const float hue_deg = oklch[2] * (180.0F / std::numbers::pi_v<float>);
        int bin = static_cast<int>(std::floor(hue_deg / 30.0F));
        if (bin < 0) {
            bin = 0;
        } else if (bin > 11) {
            bin = 11;
        }
        hue_counts[static_cast<std::size_t>(bin)] += 1.0F;
    }

    ColorAnalysis result;
    result.mean_chroma = rgb_linear.pixel_count() > 0
        ? chroma_sum / static_cast<float>(rgb_linear.pixel_count())
        : 0.0F;
    result.sat_p95 = percentile(chroma_values, 95.0F);
    result.sat_shadow_mean = shadow_chroma_sum / std::max(shadow_weight_sum, 1.0e-5F);
    result.sat_mid_mean = mid_chroma_sum / std::max(mid_weight_sum, 1.0e-5F);
    result.sat_highlight_mean = highlight_chroma_sum / std::max(highlight_weight_sum, 1.0e-5F);
    result.neon_risk = rgb_linear.pixel_count() > 0
        ? static_cast<float>(neon_pixels) / static_cast<float>(rgb_linear.pixel_count())
        : 0.0F;

    constexpr std::array<const char*, 12> kHueNames{
        "Red", "Orange", "Yellow", "Yellow-Green", "Green", "Green-Cyan",
        "Cyan", "Cyan-Blue", "Blue", "Blue-Violet", "Violet", "Magenta",
    };
    std::array<float, 12> hue_dist{};
    float hue_sum = 0.0F;
    for (const float value : hue_counts) {
        hue_sum += value;
    }
    const float hue_norm = std::max(hue_sum, 1.0e-6F);
    for (std::size_t i = 0; i < hue_counts.size(); ++i) {
        hue_dist[i] = hue_counts[i] / hue_norm;
    }

    std::array<std::size_t, 12> indices{};
    for (std::size_t i = 0; i < indices.size(); ++i) {
        indices[i] = i;
    }
    std::ranges::sort(indices, [&](const std::size_t left, const std::size_t right) {
        return hue_dist[left] > hue_dist[right];
    });
    for (std::size_t i = 0; i < result.dominant_hue_bins.size(); ++i) {
        result.dominant_hue_bins[i] = kHueNames[indices[i]];
    }

    float entropy = 0.0F;
    for (const float p : hue_dist) {
        entropy += -p * std::log2(p + 1.0e-6F);
    }
    result.hue_entropy = entropy;

    result.red_orange_density = hue_dist[0] + hue_dist[1] + hue_dist[11];
    result.green_yellow_density = hue_dist[2] + hue_dist[3] + hue_dist[4];
    const float warm_sum = hue_dist[0] + hue_dist[1] + hue_dist[2] + hue_dist[10] + hue_dist[11];
    const float cool_sum = hue_dist[5] + hue_dist[6] + hue_dist[7] + hue_dist[8];
    result.warm_cool_ratio = warm_sum / std::max(cool_sum, 1.0e-5F);
    result.cyan_blue_ratio = hue_dist[6] + hue_dist[7] + hue_dist[8];
    return result;
}

}  // namespace dfee
