#include "dfee/analyzer.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <stdexcept>
#include <vector>

#if DFEE_HAS_OPENCV
#include <opencv2/imgproc.hpp>
#endif

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

[[nodiscard]] LuminanceImage resize_luminance_linear(
    const LuminanceImage& source,
    const int target_width,
    const int target_height) {
    if (target_width <= 0 || target_height <= 0) {
        throw std::invalid_argument("resize_luminance_linear expects positive target dimensions");
    }
    if (source.empty() || (source.width == target_width && source.height == target_height)) {
        return source;
    }

#if DFEE_HAS_OPENCV
    cv::Mat source_mat(source.height, source.width, CV_32F, const_cast<float*>(source.values.data()));
    cv::Mat resized_mat;
    cv::resize(source_mat, resized_mat, cv::Size(target_width, target_height), 0.0, 0.0, cv::INTER_LINEAR);

    LuminanceImage resized(target_width, target_height);
    for (int y = 0; y < target_height; ++y) {
        for (int x = 0; x < target_width; ++x) {
            resized.at(x, y) = resized_mat.at<float>(y, x);
        }
    }
    return resized;
#else
    LuminanceImage resized(target_width, target_height);
    const float x_scale = static_cast<float>(source.width) / static_cast<float>(target_width);
    const float y_scale = static_cast<float>(source.height) / static_cast<float>(target_height);
    for (int y = 0; y < target_height; ++y) {
        const float src_y = (static_cast<float>(y) + 0.5F) * y_scale - 0.5F;
        const int y0 = std::clamp(static_cast<int>(std::floor(src_y)), 0, source.height - 1);
        const int y1 = std::clamp(y0 + 1, 0, source.height - 1);
        const float fy = src_y - static_cast<float>(y0);
        for (int x = 0; x < target_width; ++x) {
            const float src_x = (static_cast<float>(x) + 0.5F) * x_scale - 0.5F;
            const int x0 = std::clamp(static_cast<int>(std::floor(src_x)), 0, source.width - 1);
            const int x1 = std::clamp(x0 + 1, 0, source.width - 1);
            const float fx = src_x - static_cast<float>(x0);
            const float top = source.at(x0, y0) * (1.0F - fx) + source.at(x1, y0) * fx;
            const float bottom = source.at(x0, y1) * (1.0F - fx) + source.at(x1, y1) * fx;
            resized.at(x, y) = top * (1.0F - fy) + bottom * fy;
        }
    }
    return resized;
#endif
}

[[nodiscard]] LuminanceImage box_filter_square(const LuminanceImage& source, const int kernel_size) {
    if (source.empty()) {
        return source;
    }
    const int size = std::max(1, kernel_size | 1);

#if DFEE_HAS_OPENCV
    cv::Mat source_mat(source.height, source.width, CV_32F, const_cast<float*>(source.values.data()));
    cv::Mat filtered_mat;
    cv::boxFilter(source_mat, filtered_mat, -1, cv::Size(size, size));

    LuminanceImage filtered(source.width, source.height);
    for (int y = 0; y < source.height; ++y) {
        for (int x = 0; x < source.width; ++x) {
            filtered.at(x, y) = filtered_mat.at<float>(y, x);
        }
    }
    return filtered;
#else
    const int radius = size / 2;
    const int stride = source.width + 1;
    std::vector<float> integral(static_cast<std::size_t>(source.height + 1) * stride, 0.0F);
    for (int y = 0; y < source.height; ++y) {
        float row_sum = 0.0F;
        for (int x = 0; x < source.width; ++x) {
            row_sum += source.at(x, y);
            integral[static_cast<std::size_t>(y + 1) * stride + static_cast<std::size_t>(x + 1)] =
                integral[static_cast<std::size_t>(y) * stride + static_cast<std::size_t>(x + 1)] + row_sum;
        }
    }

    LuminanceImage filtered(source.width, source.height);
    for (int y = 0; y < source.height; ++y) {
        const int y0 = std::max(0, y - radius);
        const int y1 = std::min(source.height - 1, y + radius);
        for (int x = 0; x < source.width; ++x) {
            const int x0 = std::max(0, x - radius);
            const int x1 = std::min(source.width - 1, x + radius);
            const std::size_t top_left = static_cast<std::size_t>(y0) * stride + static_cast<std::size_t>(x0);
            const std::size_t top_right = static_cast<std::size_t>(y0) * stride + static_cast<std::size_t>(x1 + 1);
            const std::size_t bottom_left = static_cast<std::size_t>(y1 + 1) * stride + static_cast<std::size_t>(x0);
            const std::size_t bottom_right = static_cast<std::size_t>(y1 + 1) * stride + static_cast<std::size_t>(x1 + 1);
            const float sum = integral[bottom_right] - integral[top_right] - integral[bottom_left] + integral[top_left];
            const float area = static_cast<float>((x1 - x0 + 1) * (y1 - y0 + 1));
            filtered.at(x, y) = sum / area;
        }
    }
    return filtered;
#endif
}

[[nodiscard]] LuminanceImage gaussian_blur_square(const LuminanceImage& source, const int kernel_size) {
    if (source.empty()) {
        return source;
    }
    const int size = std::max(1, kernel_size | 1);

#if DFEE_HAS_OPENCV
    cv::Mat source_mat(source.height, source.width, CV_32F, const_cast<float*>(source.values.data()));
    cv::Mat blurred_mat;
    cv::GaussianBlur(source_mat, blurred_mat, cv::Size(size, size), 0.0);

    LuminanceImage blurred(source.width, source.height);
    for (int y = 0; y < source.height; ++y) {
        for (int x = 0; x < source.width; ++x) {
            blurred.at(x, y) = blurred_mat.at<float>(y, x);
        }
    }
    return blurred;
#else
    const int radius = size / 2;
    const float sigma = std::max(static_cast<float>(radius) / 2.0F, 1.0F);
    std::vector<float> kernel(static_cast<std::size_t>(size), 0.0F);
    float kernel_sum = 0.0F;
    for (int i = -radius; i <= radius; ++i) {
        const float value = std::exp(-(static_cast<float>(i * i)) / (2.0F * sigma * sigma));
        kernel[static_cast<std::size_t>(i + radius)] = value;
        kernel_sum += value;
    }
    for (float& value : kernel) {
        value /= kernel_sum;
    }

    LuminanceImage horizontal(source.width, source.height);
    for (int y = 0; y < source.height; ++y) {
        for (int x = 0; x < source.width; ++x) {
            float accum = 0.0F;
            for (int k = -radius; k <= radius; ++k) {
                const int sample_x = std::clamp(x + k, 0, source.width - 1);
                accum += source.at(sample_x, y) * kernel[static_cast<std::size_t>(k + radius)];
            }
            horizontal.at(x, y) = accum;
        }
    }

    LuminanceImage blurred(source.width, source.height);
    for (int y = 0; y < source.height; ++y) {
        for (int x = 0; x < source.width; ++x) {
            float accum = 0.0F;
            for (int k = -radius; k <= radius; ++k) {
                const int sample_y = std::clamp(y + k, 0, source.height - 1);
                accum += horizontal.at(x, sample_y) * kernel[static_cast<std::size_t>(k + radius)];
            }
            blurred.at(x, y) = accum;
        }
    }
    return blurred;
#endif
}

[[nodiscard]] float mean_sobel_gradient(const LuminanceImage& source) {
    if (source.empty()) {
        return 0.0F;
    }

#if DFEE_HAS_OPENCV
    cv::Mat source_mat(source.height, source.width, CV_32F, const_cast<float*>(source.values.data()));
    cv::Mat sobel_x;
    cv::Mat sobel_y;
    cv::Sobel(source_mat, sobel_x, CV_32F, 1, 0, 3);
    cv::Sobel(source_mat, sobel_y, CV_32F, 0, 1, 3);

    double gradient_sum = 0.0;
    for (int y = 0; y < source.height; ++y) {
        for (int x = 0; x < source.width; ++x) {
            const float gx = sobel_x.at<float>(y, x);
            const float gy = sobel_y.at<float>(y, x);
            gradient_sum += std::sqrt(gx * gx + gy * gy);
        }
    }
    return static_cast<float>(gradient_sum / static_cast<double>(source.values.size()));
#else
    if (source.width < 3 || source.height < 3) {
        return 0.0F;
    }
    double gradient_sum = 0.0;
    for (int y = 1; y < source.height - 1; ++y) {
        for (int x = 1; x < source.width - 1; ++x) {
            const float gx =
                -source.at(x - 1, y - 1) + source.at(x + 1, y - 1) +
                -2.0F * source.at(x - 1, y) + 2.0F * source.at(x + 1, y) +
                -source.at(x - 1, y + 1) + source.at(x + 1, y + 1);
            const float gy =
                -source.at(x - 1, y - 1) - 2.0F * source.at(x, y - 1) - source.at(x + 1, y - 1) +
                source.at(x - 1, y + 1) + 2.0F * source.at(x, y + 1) + source.at(x + 1, y + 1);
            gradient_sum += std::sqrt(gx * gx + gy * gy);
        }
    }
    return static_cast<float>(gradient_sum / static_cast<double>(source.values.size()));
#endif
}

[[nodiscard]] LuminanceImage dilate_mask_ellipse(const LuminanceImage& source, const int kernel_size) {
    if (source.empty()) {
        return source;
    }
    const int size = std::max(1, kernel_size | 1);

#if DFEE_HAS_OPENCV
    cv::Mat source_mat(source.height, source.width, CV_32F, const_cast<float*>(source.values.data()));
    cv::Mat dilated_mat;
    const cv::Mat kernel = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(size, size));
    cv::dilate(source_mat, dilated_mat, kernel);

    LuminanceImage dilated(source.width, source.height);
    for (int y = 0; y < source.height; ++y) {
        for (int x = 0; x < source.width; ++x) {
            dilated.at(x, y) = dilated_mat.at<float>(y, x);
        }
    }
    return dilated;
#else
    const int radius = size / 2;
    std::vector<std::pair<int, int>> offsets;
    offsets.reserve(static_cast<std::size_t>(size * size));
    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
            const float nx = static_cast<float>(dx) / static_cast<float>(radius == 0 ? 1 : radius);
            const float ny = static_cast<float>(dy) / static_cast<float>(radius == 0 ? 1 : radius);
            if ((nx * nx + ny * ny) <= 1.0F) {
                offsets.emplace_back(dx, dy);
            }
        }
    }

    LuminanceImage dilated(source.width, source.height);
    for (int y = 0; y < source.height; ++y) {
        for (int x = 0; x < source.width; ++x) {
            float max_value = 0.0F;
            for (const auto& [dx, dy] : offsets) {
                const int sample_x = std::clamp(x + dx, 0, source.width - 1);
                const int sample_y = std::clamp(y + dy, 0, source.height - 1);
                max_value = std::max(max_value, source.at(sample_x, sample_y));
            }
            dilated.at(x, y) = max_value;
        }
    }
    return dilated;
#endif
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

std::pair<SpatialAnalysis, SpatialMasks> ImageStateAnalyzer::analyze_spatial(const LuminanceImage& luminance) const {
    if (luminance.empty()) {
        throw std::invalid_argument("analyze_spatial expects a non-empty luminance image");
    }

    const int width = luminance.width;
    const int height = luminance.height;
    float scale = 1.0F;
    LuminanceImage downsampled = luminance;
    if (height > 1000 || width > 1000) {
        scale = 1000.0F / static_cast<float>(std::max(height, width));
        const int target_width = std::max(1, static_cast<int>(std::round(static_cast<float>(width) * scale)));
        const int target_height = std::max(1, static_cast<int>(std::round(static_cast<float>(height) * scale)));
        downsampled = resize_luminance_linear(luminance, target_width, target_height);
    }

    const LuminanceImage mean_y = box_filter_square(downsampled, 15);
    LuminanceImage squared(downsampled.width, downsampled.height);
    for (std::size_t i = 0; i < downsampled.values.size(); ++i) {
        squared.values[i] = downsampled.values[i] * downsampled.values[i];
    }
    const LuminanceImage mean_y_sq = box_filter_square(squared, 15);

    LuminanceImage local_var_small(downsampled.width, downsampled.height);
    for (std::size_t i = 0; i < local_var_small.values.size(); ++i) {
        local_var_small.values[i] = std::max(mean_y_sq.values[i] - mean_y.values[i] * mean_y.values[i], 0.0F);
    }

    const LuminanceImage local_var_full =
        scale != 1.0F ? resize_luminance_linear(local_var_small, width, height) : local_var_small;

    double texture_sum = 0.0;
    for (const float value : local_var_full.values) {
        texture_sum += value;
    }

    const float max_var = std::max(percentile(local_var_full.values, 95.0F), 1.0e-5F);
    SpatialMasks masks;
    masks.grain_receptivity_mask = LuminanceImage(width, height);
    std::size_t smooth_pixels = 0;
    for (std::size_t i = 0; i < local_var_full.values.size(); ++i) {
        const float norm_var = clamp01(local_var_full.values[i] / max_var);
        masks.grain_receptivity_mask.values[i] = 1.0F - norm_var;
        smooth_pixels += norm_var < 0.1F ? 1U : 0U;
    }

    const float edge_density = mean_sobel_gradient(downsampled);

    const LuminanceImage blurred = gaussian_blur_square(luminance, 25);
    masks.halation_source_mask = LuminanceImage(width, height);
    masks.halation_receiver_mask = LuminanceImage(width, height);
    std::size_t specular_pixels = 0;
    std::size_t large_highlight_pixels = 0;
    for (std::size_t i = 0; i < luminance.values.size(); ++i) {
        const float y = luminance.values[i];
        const float local_delta = y - blurred.values[i];
        const bool specular = y > 0.8F && local_delta > 0.1F;
        const bool large_highlight = y > 0.7F && local_delta <= 0.1F;
        masks.halation_source_mask.values[i] = specular ? 1.0F : 0.0F;
        specular_pixels += specular ? 1U : 0U;
        large_highlight_pixels += large_highlight ? 1U : 0U;
    }

    const LuminanceImage dilated_sources = dilate_mask_ellipse(masks.halation_source_mask, 15);
    for (std::size_t i = 0; i < luminance.values.size(); ++i) {
        masks.halation_receiver_mask.values[i] = dilated_sources.values[i] * (1.0F - luminance.values[i]);
    }

    SpatialAnalysis result;
    result.texture_density = static_cast<float>(texture_sum / static_cast<double>(local_var_full.values.size()));
    result.smooth_area_ratio =
        static_cast<float>(smooth_pixels) / static_cast<float>(luminance.values.size());
    result.edge_density = edge_density;
    result.digital_sharpness_score = edge_density / std::max(result.texture_density * 100.0F, 1.0e-4F);
    result.specular_point_ratio =
        static_cast<float>(specular_pixels) / static_cast<float>(luminance.values.size());
    result.large_highlight_area_ratio =
        static_cast<float>(large_highlight_pixels) / static_cast<float>(luminance.values.size());
    return {result, masks};
}

}  // namespace dfee
