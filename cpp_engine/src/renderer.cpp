#include "dfee/renderer.hpp"
#include "dfee/color_spaces.hpp"

#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>
#include <random>
#include <stdexcept>
#include <vector>

#include <opencv2/imgproc.hpp>

namespace dfee {
namespace {

[[nodiscard]] float clampf(const float value, const float low, const float high) {
    return std::clamp(value, low, high);
}

struct OklabPixel {
    float l = 0.0F;
    float a = 0.0F;
    float b = 0.0F;
};

struct OklchPixel {
    float l = 0.0F;
    float c = 0.0F;
    float h = 0.0F;
};

[[nodiscard]] OklabPixel rgb_to_oklab_pixel(const float r, const float g, const float b) {
    const float rs = std::max(r, 1.0e-12F);
    const float gs = std::max(g, 1.0e-12F);
    const float bs = std::max(b, 1.0e-12F);

    const float l = 0.4122214708F * rs + 0.5363325363F * gs + 0.0514459929F * bs;
    const float m = 0.2119034982F * rs + 0.6806995451F * gs + 0.1073969566F * bs;
    const float s = 0.0883024619F * rs + 0.2817188376F * gs + 0.6299787005F * bs;

    const float lp = std::cbrt(l);
    const float mp = std::cbrt(m);
    const float sp = std::cbrt(s);

    return {
        0.2104542553F * lp + 0.7936177850F * mp - 0.0040720468F * sp,
        1.9779984951F * lp - 2.4285922050F * mp + 0.4505937099F * sp,
        0.0259040371F * lp + 0.7827717612F * mp - 0.8086757983F * sp,
    };
}

[[nodiscard]] std::array<float, 3> oklab_to_rgb_pixel(const OklabPixel& oklab) {
    const float lp = oklab.l + 0.3963377774F * oklab.a + 0.2158017574F * oklab.b;
    const float mp = oklab.l - 0.1055613458F * oklab.a - 0.0638541728F * oklab.b;
    const float sp = oklab.l - 0.0894841775F * oklab.a - 1.2914855480F * oklab.b;

    const float l = lp * lp * lp;
    const float m = mp * mp * mp;
    const float s = sp * sp * sp;

    return {
        clamp01(4.0767416621F * l - 3.3077115913F * m + 0.2309699292F * s),
        clamp01(-1.2684380046F * l + 2.6097574011F * m - 0.3413193965F * s),
        clamp01(-0.0041960863F * l - 0.7034186147F * m + 1.7076147010F * s),
    };
}

[[nodiscard]] OklchPixel oklab_to_oklch_pixel(const OklabPixel& oklab) {
    float hue = std::atan2(oklab.b, oklab.a);
    if (hue < 0.0F) {
        hue += 2.0F * std::numbers::pi_v<float>;
    }
    return {
        oklab.l,
        std::sqrt(oklab.a * oklab.a + oklab.b * oklab.b),
        hue,
    };
}

[[nodiscard]] OklabPixel oklch_to_oklab_pixel(const OklchPixel& oklch) {
    return {
        oklch.l,
        oklch.c * std::cos(oklch.h),
        oklch.c * std::sin(oklch.h),
    };
}

[[nodiscard]] float wrap_angle_positive(const float angle) {
    float out = std::fmod(angle, 2.0F * std::numbers::pi_v<float>);
    if (out < 0.0F) {
        out += 2.0F * std::numbers::pi_v<float>;
    }
    return out;
}

[[nodiscard]] cv::Mat rgb_image_to_mat(const Image& image, const bool clamp_values = false) {
    cv::Mat out(image.height, image.width, CV_32FC3);
    for (int y = 0; y < image.height; ++y) {
        for (int x = 0; x < image.width; ++x) {
            auto& pixel = out.at<cv::Vec3f>(y, x);
            pixel[0] = clamp_values ? clamp01(image.at(x, y, 0)) : image.at(x, y, 0);
            pixel[1] = clamp_values ? clamp01(image.at(x, y, 1)) : image.at(x, y, 1);
            pixel[2] = clamp_values ? clamp01(image.at(x, y, 2)) : image.at(x, y, 2);
        }
    }
    return out;
}

[[nodiscard]] Image mat_to_rgb_image(const cv::Mat& mat) {
    Image out(mat.cols, mat.rows, 3);
    for (int y = 0; y < mat.rows; ++y) {
        for (int x = 0; x < mat.cols; ++x) {
            const auto& pixel = mat.at<cv::Vec3f>(y, x);
            out.at(x, y, 0) = clamp01(pixel[0]);
            out.at(x, y, 1) = clamp01(pixel[1]);
            out.at(x, y, 2) = clamp01(pixel[2]);
        }
    }
    return out;
}

[[nodiscard]] cv::Mat gamma_encode_mat(const Image& image) {
    cv::Mat out(image.height, image.width, CV_32FC3);
    constexpr float kGamma = 1.0F / 2.2F;
    for (int y = 0; y < image.height; ++y) {
        for (int x = 0; x < image.width; ++x) {
            auto& pixel = out.at<cv::Vec3f>(y, x);
            pixel[0] = std::pow(clamp01(image.at(x, y, 0)), kGamma);
            pixel[1] = std::pow(clamp01(image.at(x, y, 1)), kGamma);
            pixel[2] = std::pow(clamp01(image.at(x, y, 2)), kGamma);
        }
    }
    return out;
}

[[nodiscard]] Image gamma_decode_mat(const cv::Mat& mat) {
    Image out(mat.cols, mat.rows, 3);
    constexpr float kGamma = 2.2F;
    for (int y = 0; y < mat.rows; ++y) {
        for (int x = 0; x < mat.cols; ++x) {
            const auto& pixel = mat.at<cv::Vec3f>(y, x);
            out.at(x, y, 0) = std::pow(clamp01(pixel[0]), kGamma);
            out.at(x, y, 1) = std::pow(clamp01(pixel[1]), kGamma);
            out.at(x, y, 2) = std::pow(clamp01(pixel[2]), kGamma);
        }
    }
    return out;
}

[[nodiscard]] cv::Mat compute_luminance_mat(const cv::Mat& rgb) {
    cv::Mat luminance(rgb.rows, rgb.cols, CV_32F);
    for (int y = 0; y < rgb.rows; ++y) {
        for (int x = 0; x < rgb.cols; ++x) {
            const auto& pixel = rgb.at<cv::Vec3f>(y, x);
            luminance.at<float>(y, x) = 0.2126F * pixel[0] + 0.7152F * pixel[1] + 0.0722F * pixel[2];
        }
    }
    return luminance;
}

[[nodiscard]] cv::Mat luminance_image_to_mat(const LuminanceImage& image) {
    cv::Mat out(image.height, image.width, CV_32F);
    for (int y = 0; y < image.height; ++y) {
        for (int x = 0; x < image.width; ++x) {
            out.at<float>(y, x) = image.at(x, y);
        }
    }
    return out;
}

[[nodiscard]] int odd_kernel_size(const int candidate, const int min_size, const int max_extent) {
    int size = std::max(min_size, candidate);
    const int max_odd = (max_extent % 2 == 0) ? std::max(3, max_extent - 1) : max_extent;
    size = std::min(size, max_odd);
    if ((size % 2) == 0) {
        size = std::max(min_size, size - 1);
    }
    return std::max(3, size);
}

[[nodiscard]] cv::Mat gaussian_blur_downsampled_rgb(
    const cv::Mat& source,
    const int kernel_size,
    const int max_working_edge) {
    const int current_max = std::max(source.cols, source.rows);
    if (current_max <= max_working_edge) {
        cv::Mat blurred;
        cv::GaussianBlur(source, blurred, cv::Size(kernel_size, kernel_size), 0.0);
        return blurred;
    }

    const float scale = static_cast<float>(max_working_edge) / static_cast<float>(current_max);
    const int target_width = std::max(1, static_cast<int>(std::lround(source.cols * scale)));
    const int target_height = std::max(1, static_cast<int>(std::lround(source.rows * scale)));

    cv::Mat reduced;
    cv::resize(source, reduced, cv::Size(target_width, target_height), 0.0, 0.0, cv::INTER_AREA);

    const int scaled_kernel = odd_kernel_size(
        std::max(3, static_cast<int>(std::lround(kernel_size * scale))),
        3,
        std::min(target_width, target_height));
    cv::Mat reduced_blur;
    cv::GaussianBlur(reduced, reduced_blur, cv::Size(scaled_kernel, scaled_kernel), 0.0);

    cv::Mat restored;
    cv::resize(reduced_blur, restored, cv::Size(source.cols, source.rows), 0.0, 0.0, cv::INTER_LINEAR);
    return restored;
}

[[nodiscard]] cv::Mat roll_mat(const cv::Mat& source, const int shift_y, const int shift_x) {
    cv::Mat out(source.rows, source.cols, source.type());
    const int rows = source.rows;
    const int cols = source.cols;
    const int sy = ((shift_y % rows) + rows) % rows;
    const int sx = ((shift_x % cols) + cols) % cols;
    for (int y = 0; y < rows; ++y) {
        const int src_y = (y - sy + rows) % rows;
        for (int x = 0; x < cols; ++x) {
            const int src_x = (x - sx + cols) % cols;
            out.at<float>(y, x) = source.at<float>(src_y, src_x);
        }
    }
    return out;
}

[[nodiscard]] std::uint32_t compute_grain_seed(const Image& rgb_linear) {
    if (rgb_linear.empty()) {
        return 0U;
    }
    const int h = rgb_linear.height;
    const int w = rgb_linear.width;
    const float first = rgb_linear.at(0, 0, 0);
    const float center = rgb_linear.at(w / 2, h / 2, std::min(1, rgb_linear.channels - 1));
    const float last = rgb_linear.at(w - 1, h - 1, std::min(2, rgb_linear.channels - 1));
    const double combined = std::fabs(first * 1.0e6 + center * 1.0e4 + last * 1.0e2);
    const double capped = std::fmod(combined, static_cast<double>(std::numeric_limits<std::uint32_t>::max()));
    return static_cast<std::uint32_t>(capped);
}

struct GrainNoiseCacheKey {
    int width = 0;
    int height = 0;
    std::uint32_t seed = 0U;
    std::int32_t grain_size_q = 0;
    std::int32_t grain_roughness_q = 0;
    std::int32_t grain_chroma_q = 0;

    [[nodiscard]] bool matches(const GrainNoiseCacheKey& other) const noexcept {
        return width == other.width &&
            height == other.height &&
            seed == other.seed &&
            grain_size_q == other.grain_size_q &&
            grain_roughness_q == other.grain_roughness_q &&
            grain_chroma_q == other.grain_chroma_q;
    }
};

struct GrainNoiseCacheEntry {
    GrainNoiseCacheKey key;
    cv::Mat noise_r;
    cv::Mat noise_g;
    cv::Mat noise_b;
};

[[nodiscard]] std::int32_t quantize_grain_param(const float value) {
    return static_cast<std::int32_t>(std::lround(value * 1000.0F));
}

[[nodiscard]] cv::Mat make_sparse_master(const int height, const int width, std::mt19937_64& rng) {
    cv::Mat out(height, width, CV_32F);
    std::bernoulli_distribution keep(0.08);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            out.at<float>(y, x) = keep(rng) ? 1.0F : 0.0F;
        }
    }
    return out;
}

[[nodiscard]] cv::Mat make_standard_normal_mat(const int height, const int width, std::mt19937_64& rng) {
    cv::Mat out(height, width, CV_32F);
    std::normal_distribution<float> normal(0.0F, 1.0F);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            out.at<float>(y, x) = normal(rng);
        }
    }
    return out;
}

[[nodiscard]] cv::Mat make_standard_normal_mat_fixed_seed(const int height, const int width, const std::uint64_t seed) {
    std::mt19937_64 rng(seed);
    return make_standard_normal_mat(height, width, rng);
}

void normalize_zero_mean_unit_variance(cv::Mat& mat) {
    cv::Scalar mean;
    cv::Scalar stddev;
    cv::meanStdDev(mat, mean, stddev);
    if (stddev[0] > 1.0e-8) {
        mat = (mat - mean[0]) / stddev[0];
    }
}

[[nodiscard]] cv::Mat generate_grain_noise_channel(
    const cv::Mat& sparse_in,
    const cv::Mat& grit_in,
    const float grain_size,
    const float size_multiplier,
    const float scale_factor,
    const float roughness) {
    const int h = sparse_in.rows;
    const int w = sparse_in.cols;
    const float channel_grain_size = std::max(0.05F, grain_size * size_multiplier * scale_factor);

    int kernel_size = static_cast<int>(3.0F + channel_grain_size * 2.5F);
    if ((kernel_size % 2) == 0) {
        ++kernel_size;
    }
    kernel_size = std::max(3, kernel_size);
    kernel_size = odd_kernel_size(kernel_size, 3, std::min(h, w));

    cv::Mat binary_disk = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(kernel_size, kernel_size));
    binary_disk.convertTo(binary_disk, CV_32F);
    binary_disk /= static_cast<float>(cv::sum(binary_disk)[0] + 1.0e-8);

    const float blur_sigma = 0.35F + std::min(channel_grain_size * 0.15F, 0.25F);
    cv::Mat gaussian_disk;
    cv::GaussianBlur(binary_disk, gaussian_disk, cv::Size(kernel_size, kernel_size), blur_sigma);
    gaussian_disk /= static_cast<float>(cv::sum(gaussian_disk)[0] + 1.0e-8);

    cv::Mat kernel = roughness * binary_disk + (1.0F - roughness) * gaussian_disk;
    kernel /= static_cast<float>(cv::sum(kernel)[0] + 1.0e-8);

    cv::Mat noise;
    cv::filter2D(sparse_in, noise, -1, kernel, cv::Point(-1, -1), 0.0, cv::BORDER_REFLECT_101);
    normalize_zero_mean_unit_variance(noise);

    const float contrast_factor = 6.0F + roughness * 8.0F;
    for (int y = 0; y < noise.rows; ++y) {
        for (int x = 0; x < noise.cols; ++x) {
            noise.at<float>(y, x) = std::tanh(noise.at<float>(y, x) * contrast_factor);
        }
    }
    normalize_zero_mean_unit_variance(noise);

    const float grit_blend = 0.10F + roughness * 0.25F;
    noise = (1.0F - grit_blend) * noise + grit_blend * grit_in;

    if (roughness > 0.0F) {
        const float sharp_factor = roughness;
        cv::Mat kernel_sharp = (cv::Mat_<float>(3, 3) <<
            0.0F, -sharp_factor, 0.0F,
            -sharp_factor, 1.0F + 4.0F * sharp_factor, -sharp_factor,
            0.0F, -sharp_factor, 0.0F);
        cv::filter2D(noise, noise, -1, kernel_sharp, cv::Point(-1, -1), 0.0, cv::BORDER_REFLECT_101);
    }

    normalize_zero_mean_unit_variance(noise);
    return noise;
}

[[nodiscard]] Image apply_gamma_local_contrast(
    const Image& rgb_linear,
    const float amount,
    const int radius_divisor,
    const int min_radius,
    const float sigma_divisor,
    const float detail_scale,
    const bool midtone_mask) {
    if (rgb_linear.channels != 3) {
        throw std::invalid_argument("apply_gamma_local_contrast expects a 3-channel RGB image");
    }
    if (amount == 0.0F) {
        return rgb_linear;
    }

    const int short_edge = std::min(rgb_linear.width, rgb_linear.height);
    const int radius = odd_kernel_size(short_edge / radius_divisor, min_radius, short_edge);
    const float strength = clampf(amount / 100.0F, -1.0F, 1.0F);

    cv::Mat gamma = gamma_encode_mat(rgb_linear);
    cv::Mat blurred;
    cv::GaussianBlur(gamma, blurred, cv::Size(radius, radius), radius / sigma_divisor);
    cv::Mat luminance = compute_luminance_mat(gamma);

    cv::Mat result(gamma.rows, gamma.cols, CV_32FC3);
    for (int y = 0; y < gamma.rows; ++y) {
        for (int x = 0; x < gamma.cols; ++x) {
            const auto& src = gamma.at<cv::Vec3f>(y, x);
            const auto& blur = blurred.at<cv::Vec3f>(y, x);
            const float luma = luminance.at<float>(y, x);
            const float mask = midtone_mask
                ? 4.0F * luma * (1.0F - luma)
                : clamp01(luma * 5.0F) * clamp01((1.0F - luma) * 5.0F);
            auto& dst = result.at<cv::Vec3f>(y, x);
            for (int channel = 0; channel < 3; ++channel) {
                const float detail = src[channel] - blur[channel];
                dst[channel] = clamp01(src[channel] + detail * strength * detail_scale * mask);
            }
        }
    }

    return gamma_decode_mat(result);
}

[[nodiscard]] float percentile_approx(std::vector<float> values, const float fraction) {
    if (values.empty()) {
        return 0.0F;
    }
    const float clamped_fraction = clampf(fraction, 0.0F, 1.0F);
    const std::size_t index = static_cast<std::size_t>(clamped_fraction * static_cast<float>(values.size() - 1));
    std::nth_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(index), values.end());
    return values[index];
}

[[nodiscard]] Image apply_dye_contamination(
    const Image& rgb_linear,
    const FilmResponsePlan& response) {
    if (rgb_linear.channels != 3) {
        throw std::invalid_argument("apply_dye_contamination expects a 3-channel RGB image");
    }

    const auto get_value = [&](const char* key) -> float {
        const auto it = response.dye_contamination.find(key);
        return it != response.dye_contamination.end() ? it->second : 0.0F;
    };

    const float film_color_scale = clampf(response.film_color / 100.0F, 0.0F, 2.0F);
    const float r_to_g = get_value("r_to_g") * film_color_scale;
    const float g_to_r = get_value("g_to_r") * film_color_scale;
    const float b_to_g = get_value("b_to_g") * film_color_scale;
    const float b_to_r = get_value("b_to_r") * film_color_scale;
    const float r_to_b = get_value("r_to_b") * film_color_scale;
    const float g_to_b = get_value("g_to_b") * film_color_scale;

    if (r_to_g == 0.0F && g_to_r == 0.0F &&
        b_to_g == 0.0F && b_to_r == 0.0F &&
        r_to_b == 0.0F && g_to_b == 0.0F) {
        return rgb_linear;
    }

    Image contaminated(rgb_linear.width, rgb_linear.height, 3);
    for (std::size_t i = 0; i < rgb_linear.pixel_count(); ++i) {
        const float r = rgb_linear.pixels[i * 3 + 0];
        const float g = rgb_linear.pixels[i * 3 + 1];
        const float b = rgb_linear.pixels[i * 3 + 2];

        contaminated.pixels[i * 3 + 0] = clamp01(r + g * g_to_r + b * b_to_r);
        contaminated.pixels[i * 3 + 1] = clamp01(g + r * r_to_g + b * b_to_g);
        contaminated.pixels[i * 3 + 2] = clamp01(b + r * r_to_b + g * g_to_b);
    }
    return contaminated;
}

[[nodiscard]] Image apply_color_pipeline(
    const Image& rgb_linear,
    const ZoneMasks* zone_masks,
    const FilmResponsePlan& response,
    const bool apply_color_response_stage,
    const bool apply_luminance_coupling_stage) {
    if (rgb_linear.channels != 3) {
        throw std::invalid_argument("apply_color_pipeline expects a 3-channel RGB image");
    }
    if (apply_color_response_stage) {
        if (zone_masks == nullptr) {
            throw std::invalid_argument("apply_color_pipeline requires zone masks for color response");
        }
        for (const auto& zone : zone_masks->zones) {
            if (zone.width != rgb_linear.width || zone.height != rgb_linear.height) {
                throw std::invalid_argument("apply_color_pipeline expects zone masks to match the RGB image dimensions");
            }
        }
    }

    const float fc = response.film_color / 100.0F;
    constexpr float kBiasScale = 0.004F;
    const float chroma_boost = 1.0F + (response.chroma_boost - 1.0F) * fc;
    const float red_comp = response.red_orange_compression * fc;
    const float blue_comp = response.blue_cyan_compression * fc;
    const float neon_comp = response.neon_compression * fc;
    const float highlight_desat = response.highlight_desaturation * fc;

    float hi_start = 0.75F;
    float hi_rate = 1.8F;
    float hi_comp = 0.50F * fc;
    float sh_start = 0.18F;
    float sh_comp = 0.45F * fc;
    float hi_hue_target = 0.28F;
    float hi_hue_strength = 0.18F * fc;

    if (response.stock_type == "color_reversal") {
        hi_start = 0.70F;
        hi_rate = 3.0F;
        hi_comp = 0.70F * fc;
        sh_start = 0.16F;
        sh_comp = 0.55F * fc;
        hi_hue_target = 0.15F;
        hi_hue_strength = 0.12F * fc;
    }

    auto get_cc = [&](const char* key, const float fallback) {
        const auto it = response.chroma_coupling.find(key);
        return it != response.chroma_coupling.end() ? it->second : fallback;
    };
    hi_start = get_cc("hi_rolloff_start", hi_start);
    hi_rate = get_cc("hi_rolloff_rate", hi_rate);
    hi_comp = get_cc("hi_compression", hi_comp / std::max(fc, 1.0e-6F)) * fc;
    sh_start = get_cc("sh_rolloff_start", sh_start);
    sh_comp = get_cc("sh_compression", sh_comp / std::max(fc, 1.0e-6F)) * fc;
    hi_hue_target = get_cc("hi_hue_conv_rad", hi_hue_target);
    hi_hue_strength = get_cc("hi_hue_conv_str", hi_hue_strength / std::max(fc, 1.0e-6F)) * fc;

    const float shadow_a_scale = response.shadow_bias_lab[1] * kBiasScale * fc;
    const float shadow_b_scale = response.shadow_bias_lab[2] * kBiasScale * fc;
    const float mid_a_scale = response.midtone_bias_lab[1] * kBiasScale * fc;
    const float mid_b_scale = response.midtone_bias_lab[2] * kBiasScale * fc;
    const float hi_a_scale = response.highlight_bias_lab[1] * kBiasScale * fc;
    const float hi_b_scale = response.highlight_bias_lab[2] * kBiasScale * fc;

    Image out(rgb_linear.width, rgb_linear.height, 3);
    for (std::size_t i = 0; i < rgb_linear.pixel_count(); ++i) {
        float r = rgb_linear.pixels[i * 3 + 0];
        float g = rgb_linear.pixels[i * 3 + 1];
        float b = rgb_linear.pixels[i * 3 + 2];

        if (apply_color_response_stage) {
            const OklabPixel base_oklab = rgb_to_oklab_pixel(r, g, b);
            OklchPixel lch = oklab_to_oklch_pixel(base_oklab);
            float chroma = lch.c;
            const float hue = lch.h;

            if (chroma_boost != 1.0F) {
                chroma *= chroma_boost;
            }

            const float z1 = zone_masks->zones[1].values[i];
            const float z2 = zone_masks->zones[2].values[i];
            const float z3 = zone_masks->zones[3].values[i];
            const float z4 = zone_masks->zones[4].values[i];
            const float z5 = zone_masks->zones[5].values[i];

            const float shadow_zone = z1 + z2 * 0.5F;
            const float mid_zone = z2 * 0.5F + z3 + z4 * 0.5F;
            const float hi_zone = z4 * 0.5F + z5;

            const float a_bias = shadow_zone * shadow_a_scale + mid_zone * mid_a_scale + hi_zone * hi_a_scale;
            const float b_bias = shadow_zone * shadow_b_scale + mid_zone * mid_b_scale + hi_zone * hi_b_scale;

            const float weight_red_orange = std::pow(clampf(std::cos(hue - 0.6F), 0.0F, 1.0F), 2.0F);
            const float weight_blue_cyan = std::pow(clampf(std::cos(hue - 4.0F), 0.0F, 1.0F), 2.0F);
            float c_new = chroma * (1.0F - red_comp * weight_red_orange * z3);
            c_new *= 1.0F - blue_comp * weight_blue_cyan * z5;

            if (neon_comp > 0.0F) {
                constexpr float kKneeStart = 0.15F;
                constexpr float kKneeEnd = 0.35F;
                constexpr float kKneeRange = kKneeEnd - kKneeStart;
                const float knee_w = std::pow(clampf((c_new - kKneeStart) / kKneeRange, 0.0F, 1.0F), 2.0F);
                const float c_neon = kKneeStart + (c_new - kKneeStart) * (1.0F - neon_comp * knee_w);
                c_new = c_new * (1.0F - knee_w) + c_neon * knee_w;
            }

            const float c_final = c_new * (1.0F - highlight_desat * z5);
            OklabPixel adjusted = oklch_to_oklab_pixel({base_oklab.l, std::max(c_final, 0.0F), hue});
            adjusted.a += a_bias;
            adjusted.b += b_bias;
            const auto adjusted_rgb = oklab_to_rgb_pixel(adjusted);
            r = adjusted_rgb[0];
            g = adjusted_rgb[1];
            b = adjusted_rgb[2];
        }

        if (apply_luminance_coupling_stage) {
            const OklabPixel oklab = rgb_to_oklab_pixel(clamp01(r), clamp01(g), clamp01(b));
            OklchPixel lch = oklab_to_oklch_pixel(oklab);

            const float t_hi = clampf((lch.l - hi_start) / std::max(1.0F - hi_start, 1.0e-6F), 0.0F, 1.0F);
            const float hi_mask = std::pow(t_hi, hi_rate);
            const float c_hi = lch.c * (1.0F - hi_mask * hi_comp);

            const float t_sh = clampf(1.0F - lch.l / std::max(sh_start, 1.0e-6F), 0.0F, 1.0F);
            const float sh_mask = std::pow(t_sh, 1.5F);
            const float c_new = c_hi * (1.0F - sh_mask * sh_comp);

            const float d_h = std::fmod((hi_hue_target - lch.h) + std::numbers::pi_v<float>, 2.0F * std::numbers::pi_v<float>) -
                std::numbers::pi_v<float>;
            const float h_new = wrap_angle_positive(lch.h + d_h * hi_mask * hi_hue_strength);

            const OklabPixel adjusted = oklch_to_oklab_pixel({
                lch.l,
                std::max(c_new, 0.0F),
                h_new,
            });
            const auto adjusted_rgb = oklab_to_rgb_pixel(adjusted);
            r = adjusted_rgb[0];
            g = adjusted_rgb[1];
            b = adjusted_rgb[2];
        }

        out.pixels[i * 3 + 0] = r;
        out.pixels[i * 3 + 1] = g;
        out.pixels[i * 3 + 2] = b;
    }
    return out;
}

[[nodiscard]] Image apply_color_response_and_coupling_pipeline(
    const Image& rgb_linear,
    const ZoneMasks& zone_masks,
    const FilmResponsePlan& response) {
    if (rgb_linear.channels != 3) {
        throw std::invalid_argument("apply_color_response_and_coupling_pipeline expects a 3-channel RGB image");
    }
    for (const auto& zone : zone_masks.zones) {
        if (zone.width != rgb_linear.width || zone.height != rgb_linear.height) {
            throw std::invalid_argument("apply_color_response_and_coupling_pipeline expects zone masks to match the RGB image dimensions");
        }
    }

    const auto& zone_1 = zone_masks.zones[1].values;
    const auto& zone_2 = zone_masks.zones[2].values;
    const auto& zone_3 = zone_masks.zones[3].values;
    const auto& zone_4 = zone_masks.zones[4].values;
    const auto& zone_5 = zone_masks.zones[5].values;

    const float fc = response.film_color / 100.0F;
    constexpr float kBiasScale = 0.004F;
    const float chroma_boost = 1.0F + (response.chroma_boost - 1.0F) * fc;
    const float red_comp = response.red_orange_compression * fc;
    const float blue_comp = response.blue_cyan_compression * fc;
    const float neon_comp = response.neon_compression * fc;
    const float highlight_desat = response.highlight_desaturation * fc;

    float hi_start = 0.75F;
    float hi_rate = 1.8F;
    float hi_comp = 0.50F * fc;
    float sh_start = 0.18F;
    float sh_comp = 0.45F * fc;
    float hi_hue_target = 0.28F;
    float hi_hue_strength = 0.18F * fc;

    if (response.stock_type == "color_reversal") {
        hi_start = 0.70F;
        hi_rate = 3.0F;
        hi_comp = 0.70F * fc;
        sh_start = 0.16F;
        sh_comp = 0.55F * fc;
        hi_hue_target = 0.15F;
        hi_hue_strength = 0.12F * fc;
    }

    auto get_cc = [&](const char* key, const float fallback) {
        const auto it = response.chroma_coupling.find(key);
        return it != response.chroma_coupling.end() ? it->second : fallback;
    };
    hi_start = get_cc("hi_rolloff_start", hi_start);
    hi_rate = get_cc("hi_rolloff_rate", hi_rate);
    hi_comp = get_cc("hi_compression", hi_comp / std::max(fc, 1.0e-6F)) * fc;
    sh_start = get_cc("sh_rolloff_start", sh_start);
    sh_comp = get_cc("sh_compression", sh_comp / std::max(fc, 1.0e-6F)) * fc;
    hi_hue_target = get_cc("hi_hue_conv_rad", hi_hue_target);
    hi_hue_strength = get_cc("hi_hue_conv_str", hi_hue_strength / std::max(fc, 1.0e-6F)) * fc;

    const float shadow_a_scale = response.shadow_bias_lab[1] * kBiasScale * fc;
    const float shadow_b_scale = response.shadow_bias_lab[2] * kBiasScale * fc;
    const float mid_a_scale = response.midtone_bias_lab[1] * kBiasScale * fc;
    const float mid_b_scale = response.midtone_bias_lab[2] * kBiasScale * fc;
    const float hi_a_scale = response.highlight_bias_lab[1] * kBiasScale * fc;
    const float hi_b_scale = response.highlight_bias_lab[2] * kBiasScale * fc;

    Image out(rgb_linear.width, rgb_linear.height, 3);
    for (std::size_t i = 0; i < rgb_linear.pixel_count(); ++i) {
        const OklabPixel base_oklab = rgb_to_oklab_pixel(
            rgb_linear.pixels[i * 3 + 0],
            rgb_linear.pixels[i * 3 + 1],
            rgb_linear.pixels[i * 3 + 2]);
        OklchPixel lch = oklab_to_oklch_pixel(base_oklab);
        float chroma = lch.c;
        const float hue = lch.h;

        if (chroma_boost != 1.0F) {
            chroma *= chroma_boost;
        }

        const float z1 = zone_1[i];
        const float z2 = zone_2[i];
        const float z3 = zone_3[i];
        const float z4 = zone_4[i];
        const float z5 = zone_5[i];

        const float shadow_zone = z1 + z2 * 0.5F;
        const float mid_zone = z2 * 0.5F + z3 + z4 * 0.5F;
        const float hi_zone = z4 * 0.5F + z5;

        const float a_bias = shadow_zone * shadow_a_scale + mid_zone * mid_a_scale + hi_zone * hi_a_scale;
        const float b_bias = shadow_zone * shadow_b_scale + mid_zone * mid_b_scale + hi_zone * hi_b_scale;

        const float red_cos = clampf(std::cos(hue - 0.6F), 0.0F, 1.0F);
        const float blue_cos = clampf(std::cos(hue - 4.0F), 0.0F, 1.0F);
        const float weight_red_orange = red_cos * red_cos;
        const float weight_blue_cyan = blue_cos * blue_cos;
        float c_new = chroma * (1.0F - red_comp * weight_red_orange * z3);
        c_new *= 1.0F - blue_comp * weight_blue_cyan * z5;

        if (neon_comp > 0.0F) {
            constexpr float kKneeStart = 0.15F;
            constexpr float kKneeEnd = 0.35F;
            constexpr float kKneeRange = kKneeEnd - kKneeStart;
            const float knee_t = clampf((c_new - kKneeStart) / kKneeRange, 0.0F, 1.0F);
            const float knee_w = knee_t * knee_t;
            const float c_neon = kKneeStart + (c_new - kKneeStart) * (1.0F - neon_comp * knee_w);
            c_new = c_new * (1.0F - knee_w) + c_neon * knee_w;
        }

        const float c_final = c_new * (1.0F - highlight_desat * z5);
        OklabPixel adjusted = oklch_to_oklab_pixel({base_oklab.l, std::max(c_final, 0.0F), hue});
        adjusted.a += a_bias;
        adjusted.b += b_bias;
        const auto adjusted_rgb = oklab_to_rgb_pixel(adjusted);

        const OklabPixel coupled_oklab = rgb_to_oklab_pixel(
            clamp01(adjusted_rgb[0]),
            clamp01(adjusted_rgb[1]),
            clamp01(adjusted_rgb[2]));
        lch = oklab_to_oklch_pixel(coupled_oklab);

        const float t_hi = clampf((lch.l - hi_start) / std::max(1.0F - hi_start, 1.0e-6F), 0.0F, 1.0F);
        const float hi_mask = std::pow(t_hi, hi_rate);
        const float c_hi = lch.c * (1.0F - hi_mask * hi_comp);

        const float t_sh = clampf(1.0F - lch.l / std::max(sh_start, 1.0e-6F), 0.0F, 1.0F);
        const float sh_mask = t_sh * std::sqrt(t_sh);
        c_new = c_hi * (1.0F - sh_mask * sh_comp);

        const float d_h = std::fmod((hi_hue_target - lch.h) + std::numbers::pi_v<float>, 2.0F * std::numbers::pi_v<float>) -
            std::numbers::pi_v<float>;
        const float h_new = wrap_angle_positive(lch.h + d_h * hi_mask * hi_hue_strength);

        adjusted = oklch_to_oklab_pixel({
            lch.l,
            std::max(c_new, 0.0F),
            h_new,
        });
        const auto rgb = oklab_to_rgb_pixel(adjusted);
        out.pixels[i * 3 + 0] = rgb[0];
        out.pixels[i * 3 + 1] = rgb[1];
        out.pixels[i * 3 + 2] = rgb[2];
    }
    return out;
}

}  // namespace

Image FilmRenderer::render(
    const Image& rgb_linear,
    const ZoneMasks& zone_masks,
    const SpatialMasks& spatial_masks,
    const RenderPlan& render_plan) const {
    Image rendered = apply_pre_film_normalization(rgb_linear, zone_masks, render_plan.pre_film_normalization);
    if (render_plan.stock_type == "monochrome") {
        rendered = apply_panchromatic_conversion(rendered, render_plan.film_response);
    }

    rendered = apply_film_tone_response(rendered, render_plan.film_response);
    rendered = apply_dye_contamination(rendered, render_plan.film_response);

    if (render_plan.stock_type != "monochrome") {
        rendered = apply_color_response_and_coupling(rendered, zone_masks, render_plan.film_response);
    }

    rendered = apply_acutance_shaping(rendered, render_plan.material_effects);
    rendered = apply_halation_bloom(rendered, zone_masks, spatial_masks, render_plan.material_effects);
    rendered = apply_film_grain(rendered, spatial_masks, render_plan.material_effects);

    if (render_plan.print_finish.has_value()) {
        rendered = apply_print_finish(rendered, *render_plan.print_finish);
    }
    return rendered;
}

Image FilmRenderer::apply_pre_film_normalization(
    const Image& rgb_linear,
    const ZoneMasks& zone_masks,
    const PreFilmNormalization& pre_film) const {
    if (rgb_linear.channels != 3) {
        throw std::invalid_argument("apply_pre_film_normalization expects a 3-channel RGB image");
    }
    for (const auto& zone : zone_masks.zones) {
        if (zone.width != rgb_linear.width || zone.height != rgb_linear.height) {
            throw std::invalid_argument("apply_pre_film_normalization expects zone masks to match the RGB image dimensions");
        }
    }

    Image normalized(rgb_linear.width, rgb_linear.height, 3);
    const float exp_factor = std::pow(2.0F, pre_film.exposure_compensation_stops);

    for (std::size_t i = 0; i < rgb_linear.pixel_count(); ++i) {
        float r = rgb_linear.pixels[i * 3 + 0] * exp_factor;
        float g = rgb_linear.pixels[i * 3 + 1] * exp_factor;
        float b = rgb_linear.pixels[i * 3 + 2] * exp_factor;

        const float y = 0.2126F * r + 0.7152F * g + 0.0722F * b;
        if (y > 0.85F) {
            const float blend = clamp01((y - 0.85F) / 0.15F);
            r = (1.0F - blend) * r + blend * y;
            g = (1.0F - blend) * g + blend * y;
            b = (1.0F - blend) * b + blend * y;
        }

        OklabPixel oklab = rgb_to_oklab_pixel(r, g, b);
        oklab.b += zone_masks.zones[1].values[i] * pre_film.shadow_blue_normalization;
        oklab.a -= zone_masks.zones[3].values[i] * pre_film.green_magenta_stabilization *
            (oklab.a < 0.0F ? -1.0F : (oklab.a > 0.0F ? 1.0F : 0.0F));

        const auto rgb = oklab_to_rgb_pixel(oklab);
        normalized.pixels[i * 3 + 0] = rgb[0];
        normalized.pixels[i * 3 + 1] = rgb[1];
        normalized.pixels[i * 3 + 2] = rgb[2];
    }

    return normalized;
}

Image FilmRenderer::apply_panchromatic_conversion(
    const Image& rgb_linear,
    const FilmResponsePlan& response) const {
    if (rgb_linear.channels != 3) {
        throw std::invalid_argument("apply_panchromatic_conversion expects a 3-channel RGB image");
    }

    Image monochrome(rgb_linear.width, rgb_linear.height, 3);
    for (std::size_t i = 0; i < rgb_linear.pixel_count(); ++i) {
        const float y_pan =
            response.pan_weight_r * rgb_linear.pixels[i * 3 + 0] +
            response.pan_weight_g * rgb_linear.pixels[i * 3 + 1] +
            response.pan_weight_b * rgb_linear.pixels[i * 3 + 2];
        monochrome.pixels[i * 3 + 0] = y_pan;
        monochrome.pixels[i * 3 + 1] = y_pan;
        monochrome.pixels[i * 3 + 2] = y_pan;
    }
    return monochrome;
}

Image FilmRenderer::apply_film_tone_response(
    const Image& rgb_linear,
    const FilmResponsePlan& response) const {
    if (rgb_linear.channels != 3) {
        throw std::invalid_argument("apply_film_tone_response expects a 3-channel RGB image");
    }

    Image toned(rgb_linear.width, rgb_linear.height, 3);
    constexpr std::size_t kToneLutSize = 16384U;
    std::array<float, 3> alpha{};
    std::array<float, 3> beta{};
    std::array<float, 3> mid{};
    std::array<float, 3> shoulder_k{};
    std::array<std::vector<float>, 3> tone_luts{};
    for (int channel = 0; channel < 3; ++channel) {
        const std::size_t index = static_cast<std::size_t>(channel);
        alpha[index] =
            1.0F + response.toe_strength * response.channel_toe_mult[static_cast<std::size_t>(channel)];
        beta[index] =
            1.0F + response.shoulder_strength * response.channel_shoulder_mult[static_cast<std::size_t>(channel)];
        mid[index] =
            response.midtone_density * response.channel_midtone_mult[static_cast<std::size_t>(channel)];
        shoulder_k[index] =
            2.0F + response.shoulder_strength * response.channel_shoulder_mult[static_cast<std::size_t>(channel)];

        auto& lut = tone_luts[index];
        lut.resize(kToneLutSize);
        for (std::size_t sample_index = 0; sample_index < kToneLutSize; ++sample_index) {
            const float ch_clamp = static_cast<float>(sample_index) / static_cast<float>(kToneLutSize - 1U);
            const float ch_safe = clampf(ch_clamp, 1.0e-12F, 1.0F - 1.0e-12F);
            const float ch_pow_alpha = std::pow(ch_safe, alpha[index]);
            float s_curve = ch_pow_alpha /
                (ch_pow_alpha + std::pow(1.0F - ch_safe, beta[index]));

            const float mid_value = mid[index];
            if (mid_value != 1.0F) {
                const float delta = s_curve - 0.5F;
                const float mid_weight = std::exp(-(delta * delta) / (2.0F * 0.12F * 0.12F));
                const float s_curve_gamma = std::pow(s_curve, 1.0F / mid_value);
                s_curve = s_curve * (1.0F - mid_weight) + s_curve_gamma * mid_weight;
            }

            const float toe_fade = clampf(s_curve / 0.25F, 0.0F, 1.0F);
            const float shadow_weight = (1.0F - toe_fade) * (1.0F - toe_fade);
            lut[sample_index] = clamp01(s_curve + response.black_density_floor * shadow_weight);
        }
    }

    const auto apply_tone_curve = [&](const float ch, const int channel) {
        const std::size_t index = static_cast<std::size_t>(channel);
        const float ch_safe = ch > 1.0F
            ? 1.0F - 1.0F / std::max(1.0e-6F, 1.0F + shoulder_k[index] * (ch - 1.0F))
            : ch;
        const float ch_clamp = clampf(ch_safe, 0.0F, 1.0F);
        const float scaled = ch_clamp * static_cast<float>(kToneLutSize - 1U);
        const std::size_t lower = static_cast<std::size_t>(scaled);
        const std::size_t upper = std::min(lower + 1U, kToneLutSize - 1U);
        const float mix = scaled - static_cast<float>(lower);
        const auto& lut = tone_luts[index];
        return lut[lower] * (1.0F - mix) + lut[upper] * mix;
    };

    for (std::size_t pixel_index = 0; pixel_index < rgb_linear.pixel_count(); ++pixel_index) {
        const std::size_t base = pixel_index * 3U;
        toned.pixels[base + 0U] = apply_tone_curve(rgb_linear.pixels[base + 0U], 0);
        toned.pixels[base + 1U] = apply_tone_curve(rgb_linear.pixels[base + 1U], 1);
        toned.pixels[base + 2U] = apply_tone_curve(rgb_linear.pixels[base + 2U], 2);
    }

    return toned;
}

Image FilmRenderer::apply_color_response(
    const Image& rgb_linear,
    const ZoneMasks& zone_masks,
    const FilmResponsePlan& response) const {
    return apply_color_pipeline(rgb_linear, &zone_masks, response, true, false);
}

Image FilmRenderer::apply_dye_contamination(
    const Image& rgb_linear,
    const FilmResponsePlan& response) const {
    return dfee::apply_dye_contamination(rgb_linear, response);
}

Image FilmRenderer::apply_color_response_and_coupling(
    const Image& rgb_linear,
    const ZoneMasks& zone_masks,
    const FilmResponsePlan& response) const {
    return apply_color_response_and_coupling_pipeline(rgb_linear, zone_masks, response);
}

Image FilmRenderer::apply_luminance_chroma_coupling(
    const Image& rgb_linear,
    const FilmResponsePlan& response) const {
    return apply_color_pipeline(rgb_linear, nullptr, response, false, true);
}

Image FilmRenderer::apply_acutance_shaping(
    const Image& rgb_linear,
    const MaterialEffectsPlan& effects) const {
    if (rgb_linear.channels != 3) {
        throw std::invalid_argument("apply_acutance_shaping expects a 3-channel RGB image");
    }

    Image oklab = rgb_to_oklab(rgb_linear);
    cv::Mat lightness(oklab.height, oklab.width, CV_32F);
    for (int y = 0; y < oklab.height; ++y) {
        for (int x = 0; x < oklab.width; ++x) {
            lightness.at<float>(y, x) = oklab.at(x, y, 0);
        }
    }

    const int short_edge = std::min(oklab.width, oklab.height);
    const int k_low = odd_kernel_size(19, 3, short_edge);
    cv::Mat low_blur;
    cv::GaussianBlur(lightness, low_blur, cv::Size(k_low, k_low), 0.0);

    cv::Mat mid_blur;
    cv::GaussianBlur(lightness, mid_blur, cv::Size(5, 5), 0.0);

    cv::Mat processed(lightness.rows, lightness.cols, CV_32F);
    for (int y = 0; y < lightness.rows; ++y) {
        for (int x = 0; x < lightness.cols; ++x) {
            const float l = lightness.at<float>(y, x);
            const float l_low = low_blur.at<float>(y, x);
            const float l_mid = mid_blur.at<float>(y, x) - l_low;
            const float l_high = l - mid_blur.at<float>(y, x);
            processed.at<float>(y, x) = clamp01(l_low + l_mid * 1.05F + l_high * (1.0F - effects.edge_softening));
        }
    }

    if (effects.sharpness > 0.0F) {
        cv::Mat padded;
        cv::copyMakeBorder(processed, padded, 1, 1, 1, 1, cv::BORDER_REFLECT_101);

        cv::Mat sharpened = processed.clone();
        const float sharp_val = clampf(effects.sharpness, 0.0F, 1.0F);
        const float peak = 8.0F - 3.0F * sharp_val;

        for (int y = 0; y < processed.rows; ++y) {
            for (int x = 0; x < processed.cols; ++x) {
                const int py = y + 1;
                const int px = x + 1;
                const float a = padded.at<float>(py - 1, px - 1);
                const float b = padded.at<float>(py - 1, px);
                const float c = padded.at<float>(py - 1, px + 1);
                const float d = padded.at<float>(py, px - 1);
                const float e = padded.at<float>(py, px);
                const float f = padded.at<float>(py, px + 1);
                const float g = padded.at<float>(py + 1, px - 1);
                const float h = padded.at<float>(py + 1, px);
                const float i = padded.at<float>(py + 1, px + 1);

                float mn = std::min({d, e, f, b, h});
                const float mn2 = std::min(mn, std::min({a, c, g, i}));
                mn += mn2;

                float mx = std::max({d, e, f, b, h});
                const float mx2 = std::max(mx, std::max({a, c, g, i}));
                mx += mx2;

                const float amp = std::sqrt(clampf(std::min(mn, 2.0F - mx) / std::max(mx, 1.0e-5F), 0.0F, 1.0F));
                const float weight = -amp / peak;
                const float rcp_weight = 1.0F / (1.0F + 4.0F * weight);
                const float window = (b + d) + (f + h);
                const float l_sharp = clamp01((window * weight + e) * rcp_weight);
                const float luma_mask = 1.0F - effects.sharpness_mask *
                    (1.0F - std::pow(std::sin(std::numbers::pi_v<float> * e), 2.0F));
                sharpened.at<float>(y, x) = clamp01(e + (l_sharp - e) * luma_mask * effects.sharpness);
            }
        }
        processed = std::move(sharpened);
    }

    for (int y = 0; y < oklab.height; ++y) {
        for (int x = 0; x < oklab.width; ++x) {
            oklab.at(x, y, 0) = processed.at<float>(y, x);
        }
    }
    return oklab_to_rgb(oklab);
}

Image FilmRenderer::apply_clarity(
    const Image& rgb_linear,
    const float amount) const {
    return apply_gamma_local_contrast(rgb_linear, amount, 16, 5, 3.0F, 0.65F, true);
}

Image FilmRenderer::apply_texture(
    const Image& rgb_linear,
    const float amount) const {
    return apply_gamma_local_contrast(rgb_linear, amount, 64, 3, 2.0F, 0.55F, false);
}

Image FilmRenderer::apply_dehaze(
    const Image& rgb_linear,
    const float amount) const {
    if (rgb_linear.channels != 3) {
        throw std::invalid_argument("apply_dehaze expects a 3-channel RGB image");
    }
    if (amount == 0.0F) {
        return rgb_linear;
    }

    const bool add_haze = amount < 0.0F;
    const float strength = std::fabs(amount) / 100.0F;
    const int scale = 4;

    cv::Mat gamma = gamma_encode_mat(rgb_linear);
    const int small_w = std::max(1, rgb_linear.width / scale);
    const int small_h = std::max(1, rgb_linear.height / scale);
    cv::Mat small;
    cv::resize(gamma, small, cv::Size(small_w, small_h), 0.0, 0.0, cv::INTER_AREA);

    cv::Mat dark_small(small.rows, small.cols, CV_32F);
    for (int y = 0; y < small.rows; ++y) {
        for (int x = 0; x < small.cols; ++x) {
            const auto& pixel = small.at<cv::Vec3f>(y, x);
            dark_small.at<float>(y, x) = std::min({pixel[0], pixel[1], pixel[2]});
        }
    }

    int patch = std::max(3, 15 / scale);
    if ((patch % 2) == 0) {
        ++patch;
    }
    const cv::Mat kernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(patch, patch));
    cv::erode(dark_small, dark_small, kernel);

    cv::Mat dark;
    cv::resize(dark_small, dark, cv::Size(rgb_linear.width, rgb_linear.height), 0.0, 0.0, cv::INTER_LINEAR);
    cv::GaussianBlur(dark, dark, cv::Size(0, 0), std::max(rgb_linear.width, rgb_linear.height) * 0.006);

    std::vector<float> dark_values;
    dark_values.reserve(static_cast<std::size_t>(dark.rows) * static_cast<std::size_t>(dark.cols));
    for (int y = 0; y < dark.rows; ++y) {
        for (int x = 0; x < dark.cols; ++x) {
            dark_values.push_back(dark.at<float>(y, x));
        }
    }
    const float threshold = percentile_approx(std::move(dark_values), 0.999F);

    cv::Vec3f atmosphere(0.9F, 0.9F, 0.9F);
    double accum_r = 0.0;
    double accum_g = 0.0;
    double accum_b = 0.0;
    std::size_t atm_count = 0;
    for (int y = 0; y < gamma.rows; ++y) {
        for (int x = 0; x < gamma.cols; ++x) {
            if (dark.at<float>(y, x) >= threshold) {
                const auto& pixel = gamma.at<cv::Vec3f>(y, x);
                accum_r += pixel[0];
                accum_g += pixel[1];
                accum_b += pixel[2];
                ++atm_count;
            }
        }
    }
    if (atm_count > 0) {
        atmosphere[0] = clampf(static_cast<float>(accum_r / static_cast<double>(atm_count)), 0.5F, 1.0F);
        atmosphere[1] = clampf(static_cast<float>(accum_g / static_cast<double>(atm_count)), 0.5F, 1.0F);
        atmosphere[2] = clampf(static_cast<float>(accum_b / static_cast<double>(atm_count)), 0.5F, 1.0F);
    }

    const float omega = 0.95F * strength;
    const float a_max = std::max({atmosphere[0], atmosphere[1], atmosphere[2], 1.0e-8F});
    cv::Mat result(gamma.rows, gamma.cols, CV_32FC3);
    for (int y = 0; y < gamma.rows; ++y) {
        for (int x = 0; x < gamma.cols; ++x) {
            const auto& src = gamma.at<cv::Vec3f>(y, x);
            const float transmission = clampf(1.0F - omega * dark.at<float>(y, x) / a_max, 0.1F, 1.0F);
            auto& dst = result.at<cv::Vec3f>(y, x);
            for (int channel = 0; channel < 3; ++channel) {
                dst[channel] = add_haze
                    ? clamp01(src[channel] * transmission + atmosphere[channel] * (1.0F - transmission))
                    : clamp01((src[channel] - atmosphere[channel]) / transmission + atmosphere[channel]);
            }
        }
    }

    return gamma_decode_mat(result);
}

Image FilmRenderer::apply_halation_bloom(
    const Image& rgb_linear,
    const ZoneMasks& zone_masks,
    const SpatialMasks& spatial_masks,
    const MaterialEffectsPlan& effects) const {
    if (rgb_linear.channels != 3) {
        throw std::invalid_argument("apply_halation_bloom expects a 3-channel RGB image");
    }
    for (const auto& zone : zone_masks.zones) {
        if (zone.width != rgb_linear.width || zone.height != rgb_linear.height) {
            throw std::invalid_argument("apply_halation_bloom expects zone masks to match the RGB image dimensions");
        }
    }
    if (spatial_masks.halation_source_mask.width != rgb_linear.width ||
        spatial_masks.halation_source_mask.height != rgb_linear.height ||
        spatial_masks.halation_receiver_mask.width != rgb_linear.width ||
        spatial_masks.halation_receiver_mask.height != rgb_linear.height) {
        throw std::invalid_argument("apply_halation_bloom expects spatial masks to match the RGB image dimensions");
    }
    if (effects.halation_strength <= 0.0F && effects.bloom_strength <= 0.0F) {
        return rgb_linear;
    }

    cv::Mat rgb = rgb_image_to_mat(rgb_linear, false);

    if (effects.halation_strength > 0.0F) {
        cv::Mat source = luminance_image_to_mat(spatial_masks.halation_source_mask);
        cv::Mat receiver = luminance_image_to_mat(spatial_masks.halation_receiver_mask);
        const int halation_kernel = odd_kernel_size(21, 5, std::min(rgb_linear.width, rgb_linear.height));
        cv::Mat halation_blur;
        cv::GaussianBlur(source, halation_blur, cv::Size(halation_kernel, halation_kernel), 0.0);

        for (int y = 0; y < rgb.rows; ++y) {
            for (int x = 0; x < rgb.cols; ++x) {
                const float bleed = halation_blur.at<float>(y, x) * receiver.at<float>(y, x) * effects.halation_strength;
                auto& pixel = rgb.at<cv::Vec3f>(y, x);
                pixel[0] += bleed * 1.00F;
                pixel[1] += bleed * 0.22F;
                pixel[2] += bleed * 0.08F;
            }
        }
    }

    if (effects.bloom_strength > 0.0F) {
        const int bloom_kernel = odd_kernel_size(51, 15, std::min(rgb_linear.width, rgb_linear.height));
        cv::Mat bloom_blur = gaussian_blur_downsampled_rgb(rgb, bloom_kernel, 512);
        cv::Mat z5 = luminance_image_to_mat(zone_masks.zones[5]);
        const float bloom_mix = effects.bloom_strength * 0.12F;
        for (int y = 0; y < rgb.rows; ++y) {
            for (int x = 0; x < rgb.cols; ++x) {
                const float weight = bloom_mix * z5.at<float>(y, x);
                auto& pixel = rgb.at<cv::Vec3f>(y, x);
                const auto& blur = bloom_blur.at<cv::Vec3f>(y, x);
                pixel[0] = (1.0F - weight) * pixel[0] + weight * blur[0];
                pixel[1] = (1.0F - weight) * pixel[1] + weight * blur[1];
                pixel[2] = (1.0F - weight) * pixel[2] + weight * blur[2];
            }
        }
    }

    return mat_to_rgb_image(rgb);
}

Image FilmRenderer::apply_film_grain(
    const Image& rgb_linear,
    const SpatialMasks& spatial_masks,
    const MaterialEffectsPlan& effects) const {
    if (rgb_linear.channels != 3) {
        throw std::invalid_argument("apply_film_grain expects a 3-channel RGB image");
    }
    if (spatial_masks.grain_receptivity_mask.width != rgb_linear.width ||
        spatial_masks.grain_receptivity_mask.height != rgb_linear.height) {
        throw std::invalid_argument("apply_film_grain expects grain receptivity mask to match the RGB image dimensions");
    }
    if (effects.grain_strength == 0.0F) {
        return rgb_linear;
    }

    const int h = rgb_linear.height;
    const int w = rgb_linear.width;
    const float scale_factor = static_cast<float>(w) / 2048.0F;
    const bool is_mono = effects.grain_chroma_strength <= 0.0F;
    const std::uint32_t grain_seed = effects.grain_seed != 0U ? effects.grain_seed : compute_grain_seed(rgb_linear);
    const GrainNoiseCacheKey cache_key{
        .width = w,
        .height = h,
        .seed = grain_seed,
        .grain_size_q = quantize_grain_param(effects.grain_size),
        .grain_roughness_q = quantize_grain_param(effects.grain_roughness),
        .grain_chroma_q = quantize_grain_param(effects.grain_chroma_strength),
    };
    static thread_local std::optional<GrainNoiseCacheEntry> grain_noise_cache;

    cv::Mat noise_r;
    cv::Mat noise_g;
    cv::Mat noise_b;

    if (grain_noise_cache.has_value() && grain_noise_cache->key.matches(cache_key)) {
        noise_r = grain_noise_cache->noise_r;
        noise_g = grain_noise_cache->noise_g;
        noise_b = grain_noise_cache->noise_b;
    } else {
        std::mt19937_64 rng(grain_seed);
        cv::Mat sparse_master = make_sparse_master(h, w, rng);
        cv::Mat grit_master = make_standard_normal_mat(h, w, rng);
        cv::Mat grit_blur;
        cv::GaussianBlur(grit_master, grit_blur, cv::Size(3, 3), 0.5);
        grit_master -= grit_blur;
        normalize_zero_mean_unit_variance(grit_master);

        if (is_mono) {
            noise_g = generate_grain_noise_channel(
                sparse_master,
                grit_master,
                effects.grain_size,
                1.00F,
                scale_factor,
                effects.grain_roughness);
            noise_r = noise_g;
            noise_b = noise_g;
        } else {
            const cv::Mat sparse_r = sparse_master;
            const cv::Mat sparse_g = roll_mat(sparse_master, 13, 0);
            const cv::Mat sparse_b = roll_mat(sparse_master, 0, 23);
            const cv::Mat grit_r = grit_master;
            const cv::Mat grit_g = roll_mat(grit_master, 13, 0);
            const cv::Mat grit_b = roll_mat(grit_master, 0, 23);

            const cv::Mat noise_r_ind = generate_grain_noise_channel(
                sparse_r, grit_r, effects.grain_size, 0.80F, scale_factor, effects.grain_roughness);
            noise_g = generate_grain_noise_channel(
                sparse_g, grit_g, effects.grain_size, 1.00F, scale_factor, effects.grain_roughness);
            const cv::Mat noise_b_ind = generate_grain_noise_channel(
                sparse_b, grit_b, effects.grain_size, 1.25F, scale_factor, effects.grain_roughness);

            const float chroma_mix = clampf(effects.grain_chroma_strength * 4.0F, 0.0F, 1.0F);
            noise_r = (1.0F - chroma_mix) * noise_g + chroma_mix * noise_r_ind;
            noise_b = (1.0F - chroma_mix) * noise_g + chroma_mix * noise_b_ind;
            normalize_zero_mean_unit_variance(noise_r);
            normalize_zero_mean_unit_variance(noise_g);
            normalize_zero_mean_unit_variance(noise_b);
        }

        grain_noise_cache = GrainNoiseCacheEntry{
            .key = cache_key,
            .noise_r = noise_r,
            .noise_g = noise_g,
            .noise_b = noise_b,
        };
    }

    cv::Mat grain_receptivity = luminance_image_to_mat(spatial_masks.grain_receptivity_mask);
    cv::Mat gamma = gamma_encode_mat(rgb_linear);
    Image out(rgb_linear.width, rgb_linear.height, 3);
    constexpr std::array<float, 3> kStrengthMults{0.75F, 0.95F, 1.35F};

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const float smooth_mod = grain_receptivity.at<float>(y, x);
            const float noise_values[3] = {
                noise_r.at<float>(y, x),
                noise_g.at<float>(y, x),
                noise_b.at<float>(y, x),
            };
            auto& dst0 = out.at(x, y, 0);
            auto& dst1 = out.at(x, y, 1);
            auto& dst2 = out.at(x, y, 2);
            float* dst_channels[3] = {&dst0, &dst1, &dst2};
            for (int channel = 0; channel < 3; ++channel) {
                const float ch = gamma.at<cv::Vec3f>(y, x)[channel];
                const float mod = ((std::pow(ch, 0.6F) * std::pow(1.0F - ch, 0.9F)) / 0.364F) * smooth_mod;
                const float ch_strength = effects.grain_strength * 0.038F * kStrengthMults[static_cast<std::size_t>(channel)];
                const float gamma_out = clamp01(ch + noise_values[channel] * ch_strength * mod);
                *dst_channels[channel] = std::pow(gamma_out, 2.2F);
            }
        }
    }

    return out;
}

Image FilmRenderer::apply_print_finish(
    const Image& rgb_linear,
    const PrintFinishPlan& print_finish) const {
    if (rgb_linear.channels != 3) {
        throw std::invalid_argument("apply_print_finish expects a 3-channel RGB image");
    }
    const float strength = print_finish.strength;
    if (strength <= 0.0F) {
        return rgb_linear;
    }

    Image rgb = rgb_linear;
    const float print_c = print_finish.print_c / 100.0F;
    const float print_m = print_finish.print_m / 100.0F;
    const float print_y = print_finish.print_y / 100.0F;

    for (std::size_t i = 0; i < rgb.pixel_count(); ++i) {
        if (print_c != 0.0F) {
            rgb.pixels[i * 3 + 0] = clamp01(rgb.pixels[i * 3 + 0] * (1.0F - print_c * 0.5F));
        }
        if (print_m != 0.0F) {
            rgb.pixels[i * 3 + 1] = clamp01(rgb.pixels[i * 3 + 1] * (1.0F - print_m * 0.5F));
        }
        if (print_y != 0.0F) {
            rgb.pixels[i * 3 + 2] = clamp01(rgb.pixels[i * 3 + 2] * (1.0F - print_y * 0.5F));
        }
    }

    const float print_bp = print_finish.print_black_point / 100.0F;
    const float shadow_lift = clampf(print_finish.shadow_lift * strength + print_bp * 0.05F, 0.0F, 0.2F);
    for (float& value : rgb.pixels) {
        value = clamp01(value + shadow_lift * (1.0F - value));
    }

    const float contrast_boost = print_finish.contrast_boost + (print_finish.print_contrast / 100.0F) * 0.5F;
    const float cb = ((contrast_boost - 1.0F) * strength) + 1.0F;
    if (std::fabs(cb - 1.0F) > 0.005F) {
        const float k = clampf((cb - 1.0F) * 3.0F, -2.5F, 2.5F);
        const float denom = std::tanh(k * 0.5F);
        if (std::fabs(denom) > 1.0e-6F) {
            for (float& value : rgb.pixels) {
                value = clamp01(0.5F + std::tanh(k * (value - 0.5F)) / (2.0F * denom));
            }
        }
    }

    const auto luminance = compute_luminance(rgb);
    for (int y = 0; y < rgb.height; ++y) {
        for (int x = 0; x < rgb.width; ++x) {
            const float y_value = luminance.at(x, y);
            if (y_value > print_finish.highlight_rolloff) {
                const float above = clampf(
                    (y_value - print_finish.highlight_rolloff) / (1.0F - print_finish.highlight_rolloff + 1.0e-6F),
                    0.0F,
                    1.0F);
                const float rolloff = clampf(
                    1.0F - std::pow(above, print_finish.highlight_rolloff_rate) *
                    (1.0F - print_finish.highlight_rolloff) * strength,
                    print_finish.highlight_rolloff,
                    1.0F);
                const float scale = rolloff / std::max(y_value, 1.0e-6F);
                for (int channel = 0; channel < 3; ++channel) {
                    rgb.at(x, y, channel) = clamp01(rgb.at(x, y, channel) * scale);
                }
            }
        }
    }

    Image oklab = rgb_to_oklab(rgb);
    for (int y = 0; y < oklab.height; ++y) {
        for (int x = 0; x < oklab.width; ++x) {
            const float l = oklab.at(x, y, 0);
            const float w_shadow = std::pow(clampf(1.0F - l / 0.35F, 0.0F, 1.0F), 1.5F);
            const float w_highlight = std::pow(clampf((l - 0.60F) / 0.40F, 0.0F, 1.0F), 1.5F);
            const float w_midtone = clampf(1.0F - w_shadow - w_highlight, 0.0F, 1.0F);
            const float bias_scale = strength * 0.01F;
            for (int channel = 0; channel < 3; ++channel) {
                const float delta = bias_scale * (
                    print_finish.shadow_bias_lab[static_cast<std::size_t>(channel)] * w_shadow +
                    print_finish.midtone_bias_lab[static_cast<std::size_t>(channel)] * w_midtone +
                    print_finish.highlight_bias_lab[static_cast<std::size_t>(channel)] * w_highlight);
                oklab.at(x, y, channel) += delta;
            }
        }
    }

    Image rgb2 = oklab_to_rgb(oklab);
    const float red_boost = print_finish.red_boost * strength;
    const float blue_suppression = print_finish.blue_suppression * strength;
    const float green_shift = print_finish.green_shift * strength;
    for (std::size_t i = 0; i < rgb2.pixel_count(); ++i) {
        rgb2.pixels[i * 3 + 0] = clamp01(rgb2.pixels[i * 3 + 0] * (1.0F + red_boost * 0.3F));
        rgb2.pixels[i * 3 + 2] = clamp01(rgb2.pixels[i * 3 + 2] * (1.0F - blue_suppression * 0.3F));
        rgb2.pixels[i * 3 + 1] = clamp01(rgb2.pixels[i * 3 + 1] * (1.0F + green_shift * 0.15F));
    }

    if (std::fabs(print_finish.saturation_scale - 1.0F) > 0.005F) {
        Image oklab2 = rgb_to_oklab(rgb2);
        const float effective_sat = 1.0F + (print_finish.saturation_scale - 1.0F) * strength;
        for (std::size_t i = 0; i < oklab2.pixel_count(); ++i) {
            oklab2.pixels[i * 3 + 1] *= effective_sat;
            oklab2.pixels[i * 3 + 2] *= effective_sat;
        }
        rgb2 = oklab_to_rgb(oklab2);
    }

    const float grain_strength = print_finish.grain_strength * strength;
    if (grain_strength > 0.005F) {
        const int h = rgb2.height;
        const int w = rgb2.width;
        const int downsample_divisor = std::max(1, static_cast<int>(1.0F / (print_finish.grain_size + 0.01F)));
        const int small_h = std::max(h / downsample_divisor, 1);
        const int small_w = std::max(w / downsample_divisor, 1);
        cv::Mat noise_small = make_standard_normal_mat_fixed_seed(small_h, small_w, 42U);
        cv::Mat noise;
        cv::resize(noise_small, noise, cv::Size(w, h), 0.0, 0.0, cv::INTER_LINEAR);

        const auto y2 = compute_luminance(rgb2);
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                const float grain_mask = 4.0F * y2.at(x, y) * (1.0F - y2.at(x, y));
                const float delta = noise.at<float>(y, x) * grain_mask * grain_strength * 0.04F;
                for (int channel = 0; channel < 3; ++channel) {
                    rgb2.at(x, y, channel) = clamp01(rgb2.at(x, y, channel) + delta);
                }
            }
        }
    }

    return rgb2;
}

}  // namespace dfee
