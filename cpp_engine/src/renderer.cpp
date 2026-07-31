#include "dfee/renderer.hpp"
#include "dfee/color_spaces.hpp"
#include "dfee/mask_sample.hpp"
#include "dfee/parallel.hpp"

#include <array>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <limits>
#include <numbers>
#include <random>
#include <stdexcept>
#include <string_view>
#include <vector>

#include <opencv2/imgproc.hpp>

namespace dfee {
namespace {

constexpr float kHoldGainHi = 0.6F;       // Highlight Saturation -> highlight chroma coupling
constexpr float kHiSatDesatGain = 0.45F;  // Highlight Saturation -> highlight desaturation (additive authority)
constexpr float kHiSatBiasCool = 0.80F;   // Highlight Saturation (bleach dir) cools the warm highlight bias
constexpr float kShSatGain = 0.45F;       // Shadow Saturation -> shadow chroma (additive authority)
constexpr float kEmulsionDensityGain = 0.5F;

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

[[nodiscard]] std::array<float, 3> oklab_to_rgb_pixel_unclamped(const OklabPixel& oklab) {
    const float lp = oklab.l + 0.3963377774F * oklab.a + 0.2158017574F * oklab.b;
    const float mp = oklab.l - 0.1055613458F * oklab.a - 0.0638541728F * oklab.b;
    const float sp = oklab.l - 0.0894841775F * oklab.a - 1.2914855480F * oklab.b;

    const float l = lp * lp * lp;
    const float m = mp * mp * mp;
    const float s = sp * sp * sp;

    return {
        4.0767416621F * l - 3.3077115913F * m + 0.2309699292F * s,
        -1.2684380046F * l + 2.6097574011F * m - 0.3413193965F * s,
        -0.0041960863F * l - 0.7034186147F * m + 1.7076147010F * s,
    };
}

[[nodiscard]] std::array<float, 3> oklab_to_rgb_pixel(const OklabPixel& oklab) {
    auto rgb = oklab_to_rgb_pixel_unclamped(oklab);
    rgb[0] = clamp01(rgb[0]);
    rgb[1] = clamp01(rgb[1]);
    rgb[2] = clamp01(rgb[2]);
    return rgb;
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

void append_timing_metric(
    NativeEngineMetadata* metadata,
    const char* timing_prefix,
    const char* suffix,
    const double milliseconds) {
    if (metadata == nullptr || timing_prefix == nullptr || timing_prefix[0] == '\0') {
        return;
    }
    metadata->timings.push_back({
        .stage = std::string(timing_prefix) + suffix,
        .milliseconds = milliseconds,
    });
}

[[nodiscard]] bool should_trace_gamut_reentry(
    NativeEngineMetadata* metadata,
    const char* timing_prefix) {
    if (metadata == nullptr || timing_prefix == nullptr || timing_prefix[0] == '\0') {
        return false;
    }
    const char* value = std::getenv("DFEE_TRACE_GAMUT_REENTRY");
    return value != nullptr && std::string_view(value) == "1";
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

// Convert a (possibly lower-resolution proxy) luminance mask to a cv::Mat at the target
// size. When the mask is smaller it is upsampled with cv::resize INTER_LINEAR — byte-
// identical to the old path (which pre-upsampled the mask to full-res the same way), but
// materialized transiently and only for the few channels a stage actually needs, instead
// of holding all mask channels at full-res for the whole render.
[[nodiscard]] cv::Mat luminance_image_to_mat_scaled(const LuminanceImage& image, const int dst_w, const int dst_h) {
    cv::Mat src = luminance_image_to_mat(image);
    if (image.width == dst_w && image.height == dst_h) {
        return src;
    }
    cv::Mat out;
    cv::resize(src, out, cv::Size(dst_w, dst_h), 0.0, 0.0, cv::INTER_LINEAR);
    return out;
}

// Grain reads its receptivity mask per pixel as a flat vector. Return the mask's values
// directly when it already matches (bit-identical), else upsample once via cv::resize
// INTER_LINEAR into `scratch` (byte-identical to the old pre-upsampled mask).
[[nodiscard]] const std::vector<float>& scaled_receptivity_values(
    const LuminanceImage& mask, const int dst_w, const int dst_h, std::vector<float>& scratch) {
    if (mask.width == dst_w && mask.height == dst_h) {
        return mask.values;
    }
    const cv::Mat resized = luminance_image_to_mat_scaled(mask, dst_w, dst_h);
    scratch.resize(static_cast<std::size_t>(dst_w) * static_cast<std::size_t>(dst_h));
    for (int y = 0; y < dst_h; ++y) {
        const float* row = resized.ptr<float>(y);
        for (int x = 0; x < dst_w; ++x) {
            scratch[static_cast<std::size_t>(y) * static_cast<std::size_t>(dst_w) + static_cast<std::size_t>(x)] = row[x];
        }
    }
    return scratch;
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

[[nodiscard]] cv::Mat gaussian_blur_downsampled_gray(
    const cv::Mat& source,
    const int radius,
    const int max_working_edge) {
    const int kernel = std::max(3, radius * 2 + 1);
    const int current_max = std::max(source.cols, source.rows);
    if (current_max <= max_working_edge) {
        cv::Mat blurred;
        cv::GaussianBlur(source, blurred, cv::Size(kernel, kernel), std::max(0.5, static_cast<double>(radius) / 2.5));
        return blurred;
    }

    const float scale = static_cast<float>(max_working_edge) / static_cast<float>(current_max);
    const int target_width = std::max(1, static_cast<int>(std::lround(source.cols * scale)));
    const int target_height = std::max(1, static_cast<int>(std::lround(source.rows * scale)));

    cv::Mat reduced;
    cv::resize(source, reduced, cv::Size(target_width, target_height), 0.0, 0.0, cv::INTER_AREA);

    const int scaled_radius = std::max(1, static_cast<int>(std::lround(radius * scale)));
    int scaled_kernel = std::max(3, scaled_radius * 2 + 1);
    if ((scaled_kernel % 2) == 0) {
        ++scaled_kernel;
    }

    cv::Mat reduced_blur;
    cv::GaussianBlur(
        reduced,
        reduced_blur,
        cv::Size(scaled_kernel, scaled_kernel),
        std::max(0.5, static_cast<double>(scaled_radius) / 2.5));

    cv::Mat restored;
    cv::resize(reduced_blur, restored, cv::Size(source.cols, source.rows), 0.0, 0.0, cv::INTER_LINEAR);
    return restored;
}

[[nodiscard]] float smoothstep01(const float value) {
    const float t = clamp01(value);
    return t * t * (3.0F - 2.0F * t);
}

// Bilinear read from a small proxy Mat at a (possibly fractional) proxy coordinate. Lets the
// full-res composite sample a low-res glow field without materializing a full-res upsample.
[[nodiscard]] inline float sample_mat_gray(const cv::Mat& m, float fx, float fy) {
    fx = std::clamp(fx, 0.0F, static_cast<float>(m.cols - 1));
    fy = std::clamp(fy, 0.0F, static_cast<float>(m.rows - 1));
    const int x0 = static_cast<int>(fx);
    const int y0 = static_cast<int>(fy);
    const int x1 = std::min(x0 + 1, m.cols - 1);
    const int y1 = std::min(y0 + 1, m.rows - 1);
    const float tx = fx - static_cast<float>(x0);
    const float ty = fy - static_cast<float>(y0);
    const float* r0 = m.ptr<float>(y0);
    const float* r1 = m.ptr<float>(y1);
    const float top = r0[x0] * (1.0F - tx) + r0[x1] * tx;
    const float bot = r1[x0] * (1.0F - tx) + r1[x1] * tx;
    return top * (1.0F - ty) + bot * ty;
}

[[nodiscard]] inline cv::Vec3f sample_mat_rgb(const cv::Mat& m, float fx, float fy) {
    fx = std::clamp(fx, 0.0F, static_cast<float>(m.cols - 1));
    fy = std::clamp(fy, 0.0F, static_cast<float>(m.rows - 1));
    const int x0 = static_cast<int>(fx);
    const int y0 = static_cast<int>(fy);
    const int x1 = std::min(x0 + 1, m.cols - 1);
    const int y1 = std::min(y0 + 1, m.rows - 1);
    const float tx = fx - static_cast<float>(x0);
    const float ty = fy - static_cast<float>(y0);
    const cv::Vec3f* r0 = m.ptr<cv::Vec3f>(y0);
    const cv::Vec3f* r1 = m.ptr<cv::Vec3f>(y1);
    const cv::Vec3f top = r0[x0] * (1.0F - tx) + r0[x1] * tx;
    const cv::Vec3f bot = r1[x0] * (1.0F - tx) + r1[x1] * tx;
    return top * (1.0F - ty) + bot * ty;
}

// filmic_v3 subtractive density.
constexpr float kOklabChromaRef   = 0.35F; // chroma normalization reference (OKLCh C)
constexpr float kDensityLumaMax   = 0.55F; // max fractional L reduction at full density
constexpr float kDensityLimitSoft = 0.06F; // soft width of the low-luma limiter

// filmic_v3 colour compression.
constexpr float kCompressK        = 3.0F;   // chroma-shoulder hardness scale
constexpr float kCompressCrossLo  = 0.05F;  // neutral gate lower edge (OKLCh C) for crosstalk
constexpr float kCompressCrossHi  = 0.10F;  // neutral gate upper edge
constexpr float kCompressLeanGain = 0.20F;  // max radians of neighbour lean at full crosstalk
constexpr float kLeanRedSign      = 1.0F;   // sign chosen so red leans toward orange
constexpr float kLeanBlueSign     = -1.0F;  // sign chosen so blue leans toward cyan

// Film grain belongs in optical density, not as a display-space overlay. The
// stock response and artist-facing Amount control apply the remaining shaping.
constexpr float kGrainDensitySigma = 0.060F;
constexpr float kGrainDensityFloor = 1.0e-5F;
constexpr float kGrainMaxDensity = 12.0F;

// Smoothstep with explicit [lo, hi] range → [0, 1]. Distinct signature from the single-arg overload.
[[nodiscard]] inline float smoothstep01(const float lo, const float hi, const float x) {
    const float t = std::clamp((x - lo) / std::max(hi - lo, 1.0e-6F), 0.0F, 1.0F);
    return t * t * (3.0F - 2.0F * t);
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
    std::int32_t grain_clumpiness_q = 0;
    std::int32_t grain_micro_grit_q = 0;
    std::int32_t grain_layer_correlation_q = 0;

    [[nodiscard]] bool matches(const GrainNoiseCacheKey& other) const noexcept {
        return width == other.width &&
            height == other.height &&
            seed == other.seed &&
            grain_size_q == other.grain_size_q &&
            grain_roughness_q == other.grain_roughness_q &&
            grain_chroma_q == other.grain_chroma_q &&
            grain_clumpiness_q == other.grain_clumpiness_q &&
            grain_micro_grit_q == other.grain_micro_grit_q &&
            grain_layer_correlation_q == other.grain_layer_correlation_q;
    }
};

struct GrainNoiseCacheEntry {
    GrainNoiseCacheKey key;
    cv::Mat noise_r;
    cv::Mat noise_g;
    cv::Mat noise_b;
    // Second, coprime-period noise octave used only on the tiled (large-export) path to
    // detile the wrap-around texture (empty otherwise). Summed with the primary octave at
    // 1/sqrt(2) during compositing: each octave is individually seamless, so the sum is
    // seamless, but the combined period (lcm of the two tile sizes) exceeds any image, so
    // the primary tile's repetition is broken. Grain variance/spectrum are unchanged.
    cv::Mat noise_r2;
    cv::Mat noise_g2;
    cv::Mat noise_b2;
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

[[nodiscard]] std::array<float, 8193> build_power_lut(const float exponent) {
    std::array<float, 8193> lut{};
    for (std::size_t index = 0; index < lut.size(); ++index) {
        const float x = static_cast<float>(index) / static_cast<float>(lut.size() - 1);
        lut[index] = std::pow(x, exponent);
    }
    return lut;
}

[[nodiscard]] std::array<float, 8193> build_grain_modulation_lut() {
    std::array<float, 8193> lut{};
    for (std::size_t index = 0; index < lut.size(); ++index) {
        const float x = static_cast<float>(index) / static_cast<float>(lut.size() - 1);
        lut[index] = (std::pow(x, 0.6F) * std::pow(1.0F - x, 0.9F)) / 0.364F;
    }
    return lut;
}

[[nodiscard]] std::array<float, 8193> build_density_from_linear_lut() {
    std::array<float, 8193> lut{};
    for (std::size_t index = 0; index < lut.size(); ++index) {
        const float linear = static_cast<float>(index) / static_cast<float>(lut.size() - 1U);
        lut[index] = -std::log(std::max(linear, kGrainDensityFloor));
    }
    return lut;
}

[[nodiscard]] std::array<float, 8193> build_linear_from_density_lut() {
    std::array<float, 8193> lut{};
    for (std::size_t index = 0; index < lut.size(); ++index) {
        const float density = kGrainMaxDensity * static_cast<float>(index) / static_cast<float>(lut.size() - 1U);
        lut[index] = std::exp(-density);
    }
    return lut;
}

template <std::size_t N>
[[nodiscard]] float sample_unit_lut(const std::array<float, N>& lut, const float value) {
    const float clamped = clamp01(value);
    const float scaled = clamped * static_cast<float>(N - 1);
    const auto lower = static_cast<std::size_t>(scaled);
    const auto upper = std::min(lower + 1, N - 1);
    const float t = scaled - static_cast<float>(lower);
    return lut[lower] + (lut[upper] - lut[lower]) * t;
}

template <std::size_t N>
[[nodiscard]] float sample_density_lut(const std::array<float, N>& lut, const float density) {
    const float clamped = std::clamp(density, 0.0F, kGrainMaxDensity);
    const float scaled = clamped * static_cast<float>(N - 1U) / kGrainMaxDensity;
    const auto lower = static_cast<std::size_t>(scaled);
    const auto upper = std::min(lower + 1U, N - 1U);
    const float t = scaled - static_cast<float>(lower);
    return lut[lower] + (lut[upper] - lut[lower]) * t;
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

[[nodiscard]] cv::Mat generate_filmic_grain_noise_channel(
    const cv::Mat& fine_in,
    const cv::Mat& clump_in,
    const float grain_size,
    const float size_multiplier,
    const float scale_factor,
    const float roughness,
    const float clumpiness,
    const float micro_grit) {
    const float channel_grain_size = std::max(0.05F, grain_size * size_multiplier * scale_factor);

    const float particle_sigma = std::clamp(0.55F + channel_grain_size * 0.72F, 0.55F, 1.85F);
    cv::Mat particle;
    cv::GaussianBlur(
        fine_in,
        particle,
        cv::Size(0, 0),
        particle_sigma,
        particle_sigma,
        cv::BORDER_REFLECT_101);
    normalize_zero_mean_unit_variance(particle);

    cv::Mat micro = fine_in.clone();
    cv::Mat micro_soft;
    cv::GaussianBlur(micro, micro_soft, cv::Size(3, 3), 0.55, 0.55, cv::BORDER_REFLECT_101);
    micro -= micro_soft;
    normalize_zero_mean_unit_variance(micro);

    cv::Mat clump_soft;
    const float clump_sigma = std::clamp(particle_sigma * (1.55F + clumpiness * 0.75F), 1.05F, 3.25F);
    cv::GaussianBlur(
        clump_in,
        clump_soft,
        cv::Size(0, 0),
        clump_sigma,
        clump_sigma,
        cv::BORDER_REFLECT_101);

    cv::Mat clump_wide;
    cv::GaussianBlur(
        clump_soft,
        clump_wide,
        cv::Size(0, 0),
        std::clamp(clump_sigma * 2.8F, 3.0F, 7.0F),
        std::clamp(clump_sigma * 2.8F, 3.0F, 7.0F),
        cv::BORDER_REFLECT_101);
    cv::Mat clump = clump_soft - clump_wide;
    normalize_zero_mean_unit_variance(clump);

    const float clump_blend = std::clamp(0.025F + clumpiness * 0.075F, 0.02F, 0.11F);
    const float micro_blend = std::clamp(micro_grit * 0.10F, 0.0F, 0.14F);
    cv::Mat noise = (1.0F - clump_blend - micro_blend) * particle + micro_blend * micro + clump_blend * clump;
    normalize_zero_mean_unit_variance(noise);

    const float shape = std::clamp(0.72F + roughness * 1.10F, 0.72F, 1.82F);
    const float normalizer = std::tanh(shape);
    for (int y = 0; y < noise.rows; ++y) {
        float* row = noise.ptr<float>(y);
        for (int x = 0; x < noise.cols; ++x) {
            row[x] = std::tanh(row[x] * shape) / normalizer;
        }
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
    parallel_for_index(static_cast<std::ptrdiff_t>(rgb_linear.pixel_count()), [&](std::ptrdiff_t i) {
        const float r = rgb_linear.pixels[i * 3 + 0];
        const float g = rgb_linear.pixels[i * 3 + 1];
        const float b = rgb_linear.pixels[i * 3 + 2];

        contaminated.pixels[i * 3 + 0] = clamp01(r + g * g_to_r + b * b_to_r);
        contaminated.pixels[i * 3 + 1] = clamp01(g + r * r_to_g + b * b_to_g);
        contaminated.pixels[i * 3 + 2] = clamp01(b + r * r_to_b + g * g_to_b);
    });
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
    const float yellow_green_muting = response.yellow_green_muting * fc;
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
    parallel_for_index(static_cast<std::ptrdiff_t>(rgb_linear.pixel_count()), [&](std::ptrdiff_t i) {
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
            const float weight_yellow_green = std::pow(clampf(std::cos(hue - 1.75F), 0.0F, 1.0F), 2.0F);
            float c_new = chroma * (1.0F - red_comp * weight_red_orange * z3);
            c_new *= 1.0F - blue_comp * weight_blue_cyan * z5;
            c_new *= 1.0F - yellow_green_muting * weight_yellow_green * mid_zone;

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
    });
    return out;
}

[[nodiscard]] Image apply_color_response_and_coupling_pipeline(
    const Image& rgb_linear,
    const ZoneMasks& zone_masks,
    const FilmResponsePlan& response,
    NativeEngineMetadata* metadata,
    const char* timing_prefix) {
    if (rgb_linear.channels != 3) {
        throw std::invalid_argument("apply_color_response_and_coupling_pipeline expects a 3-channel RGB image");
    }
    // Zone masks may be a lower-resolution proxy (export path); sampled scale-aware below.
    const int mask_w = rgb_linear.width;
    const int mask_h = rgb_linear.height;

    const float fc = response.film_color / 100.0F;
    constexpr float kBiasScale = 0.004F;
    const float n_dens = std::clamp(response.emulsion_color_density / 100.0F, -1.0F, 1.0F)
        * response.emulsion_density_sensitivity;
    const float chroma_boost = (1.0F + (response.chroma_boost - 1.0F) * fc) * (1.0F + kEmulsionDensityGain * n_dens);
    const float red_comp = response.red_orange_compression * fc;
    const float blue_comp = response.blue_cyan_compression * fc;
    const float yellow_green_muting = response.yellow_green_muting * fc;
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

    // Highlight Saturation control: + keeps colour in highlights, - bleaches toward clean
    // white. Additive authority (works even when the stock's base desat is small), lightly
    // trimmed by the per-stock sensitivity. The bleach direction also cools the warm
    // highlight bias so blown areas wash to neutral white rather than saturated orange.
    // 0 = stock default (byte-identical to before).
    const float hi_sat = std::clamp(response.highlight_color_hold / 100.0F, -1.0F, 1.0F)
        * (0.65F + 0.35F * std::clamp(response.highlight_hold_sensitivity, 0.0F, 1.0F));
    const float hi_bias_cool = 1.0F - kHiSatBiasCool * std::max(0.0F, -hi_sat);
    const float hi_a_scale = response.highlight_bias_lab[1] * kBiasScale * fc * hi_bias_cool;
    const float hi_b_scale = response.highlight_bias_lab[2] * kBiasScale * fc * hi_bias_cool;
    hi_comp = std::max(hi_comp * (1.0F - kHoldGainHi * hi_sat), 0.0F);
    const float highlight_desat_effective =
        std::clamp(highlight_desat - kHiSatDesatGain * hi_sat, 0.0F, 0.95F);

    // Shadow Saturation control: + keeps colour in shadows, - mutes them. 0 = stock default.
    const float sh_sat = std::clamp(response.shadow_color_retention / 100.0F, -1.0F, 1.0F)
        * (0.65F + 0.35F * std::clamp(response.shadow_retention_sensitivity, 0.0F, 1.0F));
    sh_comp = std::clamp(sh_comp - kShSatGain * sh_sat, 0.0F, 1.0F);

    // NOTE: the M7-003 palette_range anchor pass was retired in Film Lab v1
    // Slice 3; palette separation is superseded by the filmic_v3 Color
    // Compression stage (neighbour-lean crosstalk + saturation compression).

    Image out(rgb_linear.width, rgb_linear.height, 3);
    const bool trace_gamut_reentry = should_trace_gamut_reentry(metadata, timing_prefix);
    std::size_t gamut_reentry_count = 0U;
    parallel_for_index(static_cast<std::ptrdiff_t>(rgb_linear.pixel_count()), [&](std::ptrdiff_t i) {
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

        const int mx = static_cast<int>(i % mask_w);
        const int my = static_cast<int>(i / mask_w);
        const float z1 = sample_mask_scaled(zone_masks.zones[1], mx, my, mask_w, mask_h);
        const float z2 = sample_mask_scaled(zone_masks.zones[2], mx, my, mask_w, mask_h);
        const float z3 = sample_mask_scaled(zone_masks.zones[3], mx, my, mask_w, mask_h);
        const float z4 = sample_mask_scaled(zone_masks.zones[4], mx, my, mask_w, mask_h);
        const float z5 = sample_mask_scaled(zone_masks.zones[5], mx, my, mask_w, mask_h);

        const float shadow_zone = z1 + z2 * 0.5F;
        const float mid_zone = z2 * 0.5F + z3 + z4 * 0.5F;
        const float hi_zone = z4 * 0.5F + z5;

        const float a_bias = shadow_zone * shadow_a_scale + mid_zone * mid_a_scale + hi_zone * hi_a_scale;
        const float b_bias = shadow_zone * shadow_b_scale + mid_zone * mid_b_scale + hi_zone * hi_b_scale;

        const float red_cos = clampf(std::cos(hue - 0.6F), 0.0F, 1.0F);
        const float blue_cos = clampf(std::cos(hue - 4.0F), 0.0F, 1.0F);
        const float yellow_green_cos = clampf(std::cos(hue - 1.75F), 0.0F, 1.0F);
        const float weight_red_orange = red_cos * red_cos;
        const float weight_blue_cyan = blue_cos * blue_cos;
        const float weight_yellow_green = yellow_green_cos * yellow_green_cos;
        float c_new = chroma * (1.0F - red_comp * weight_red_orange * z3);
        c_new *= 1.0F - blue_comp * weight_blue_cyan * z5;
        c_new *= 1.0F - yellow_green_muting * weight_yellow_green * mid_zone;

        if (neon_comp > 0.0F) {
            constexpr float kKneeStart = 0.15F;
            constexpr float kKneeEnd = 0.35F;
            constexpr float kKneeRange = kKneeEnd - kKneeStart;
            const float knee_t = clampf((c_new - kKneeStart) / kKneeRange, 0.0F, 1.0F);
            const float knee_w = knee_t * knee_t;
            const float c_neon = kKneeStart + (c_new - kKneeStart) * (1.0F - neon_comp * knee_w);
            c_new = c_new * (1.0F - knee_w) + c_neon * knee_w;
        }

        const float c_final = c_new * (1.0F - highlight_desat_effective * z5);
        OklabPixel adjusted = oklch_to_oklab_pixel({base_oklab.l, std::max(c_final, 0.0F), hue});
        adjusted.a += a_bias;
        adjusted.b += b_bias;
        const auto adjusted_rgb = oklab_to_rgb_pixel_unclamped(adjusted);
        const bool r_low = adjusted_rgb[0] < 0.0F;
        const bool g_low = adjusted_rgb[1] < 0.0F;
        const bool b_low = adjusted_rgb[2] < 0.0F;
        const bool r_high = adjusted_rgb[0] > 1.0F;
        const bool g_high = adjusted_rgb[1] > 1.0F;
        const bool b_high = adjusted_rgb[2] > 1.0F;
        const bool any_low = r_low || g_low || b_low;
        const bool any_high = r_high || g_high || b_high;
        const bool needs_gamut_clamp = any_low || any_high;

        if (needs_gamut_clamp) {
            if (trace_gamut_reentry) {
#if DFEE_HAS_OPENMP
#pragma omp atomic
#endif
                ++gamut_reentry_count;
            }
            const OklabPixel coupled_oklab = rgb_to_oklab_pixel(
                clamp01(adjusted_rgb[0]),
                clamp01(adjusted_rgb[1]),
                clamp01(adjusted_rgb[2]));
            lch = oklab_to_oklch_pixel(coupled_oklab);
        } else {
            lch = oklab_to_oklch_pixel(adjusted);
        }

        const float t_hi = clampf((lch.l - hi_start) / std::max(1.0F - hi_start, 1.0e-6F), 0.0F, 1.0F);
        const float hi_mask = std::pow(t_hi, hi_rate);
        const float c_hi = lch.c * (1.0F - hi_mask * hi_comp);

        const float t_sh = clampf(1.0F - lch.l / std::max(sh_start, 1.0e-6F), 0.0F, 1.0F);
        const float sh_mask = t_sh * std::sqrt(t_sh);
        c_new = c_hi * (1.0F - sh_mask * sh_comp);

        const float d_h = std::fmod((hi_hue_target - lch.h) + std::numbers::pi_v<float>, 2.0F * std::numbers::pi_v<float>) -
            std::numbers::pi_v<float>;
        float h_new = wrap_angle_positive(lch.h + d_h * hi_mask * hi_hue_strength);

        adjusted = oklch_to_oklab_pixel({
            lch.l,
            std::max(c_new, 0.0F),
            h_new,
        });
        const auto rgb = oklab_to_rgb_pixel(adjusted);
        out.pixels[i * 3 + 0] = rgb[0];
        out.pixels[i * 3 + 1] = rgb[1];
        out.pixels[i * 3 + 2] = rgb[2];
    });

    if (trace_gamut_reentry) {
        append_timing_metric(metadata, timing_prefix, "__gamut_reentry_count", static_cast<double>(gamut_reentry_count));
        append_timing_metric(
            metadata,
            timing_prefix,
            "__gamut_reentry_ratio_percent",
            100.0 * static_cast<double>(gamut_reentry_count) / static_cast<double>(rgb_linear.pixel_count()));
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
    // Zone masks may be a lower-resolution proxy (export path) — sampled scale-aware
    // below, so no full-resolution upsample is materialized.

    Image normalized(rgb_linear.width, rgb_linear.height, 3);
    const float exp_factor = std::pow(2.0F, pre_film.exposure_compensation_stops);
    const int w = rgb_linear.width;
    const int h = rgb_linear.height;

    parallel_for_index(static_cast<std::ptrdiff_t>(rgb_linear.pixel_count()), [&](std::ptrdiff_t i) {
        const int x = static_cast<int>(i % w);
        const int y_px = static_cast<int>(i / w);
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
        oklab.b += sample_mask_scaled(zone_masks.zones[1], x, y_px, w, h) * pre_film.shadow_blue_normalization;
        oklab.a -= sample_mask_scaled(zone_masks.zones[3], x, y_px, w, h) * pre_film.green_magenta_stabilization *
            (oklab.a < 0.0F ? -1.0F : (oklab.a > 0.0F ? 1.0F : 0.0F));

        const auto rgb = oklab_to_rgb_pixel(oklab);
        normalized.pixels[i * 3 + 0] = rgb[0];
        normalized.pixels[i * 3 + 1] = rgb[1];
        normalized.pixels[i * 3 + 2] = rgb[2];
    });

    return normalized;
}

Image FilmRenderer::apply_panchromatic_conversion(
    const Image& rgb_linear,
    const FilmResponsePlan& response) const {
    if (rgb_linear.channels != 3) {
        throw std::invalid_argument("apply_panchromatic_conversion expects a 3-channel RGB image");
    }

    Image monochrome(rgb_linear.width, rgb_linear.height, 3);
    parallel_for_index(static_cast<std::ptrdiff_t>(rgb_linear.pixel_count()), [&](std::ptrdiff_t i) {
        const float y_pan =
            response.pan_weight_r * rgb_linear.pixels[i * 3 + 0] +
            response.pan_weight_g * rgb_linear.pixels[i * 3 + 1] +
            response.pan_weight_b * rgb_linear.pixels[i * 3 + 2];
        monochrome.pixels[i * 3 + 0] = y_pan;
        monochrome.pixels[i * 3 + 1] = y_pan;
        monochrome.pixels[i * 3 + 2] = y_pan;
    });
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

            // Highlight rolloff (filmic_v3): above the knee, compress highlights DOWN with a
            // Reinhard shoulder so bright regions retain gradation and ease into a soft,
            // creamy near-white instead of clipping to paper-white. Stronger amount pulls the
            // white point lower (more protective). Off (amount 0 / knee >= 1) for
            // filmic_v2/parity, so those stay byte-identical.
            if (response.highlight_rolloff_amount > 0.0F && response.highlight_rolloff_knee < 1.0F &&
                s_curve > response.highlight_rolloff_knee) {
                const float knee = response.highlight_rolloff_knee;
                const float range = 1.0F - knee;
                const float t = (s_curve - knee) / range;
                const float k = response.highlight_rolloff_amount;
                s_curve = knee + range * (t / (1.0F + k * t));
            }

            const float toe_knee = std::max(0.05F, response.shadow_lift_knee);
            const float toe_fade = clampf(s_curve / toe_knee, 0.0F, 1.0F);
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

    // tone_response_strength < 1 applies the stock's tone curve subtly by blending the
    // toned result back toward the untoned input. Used for rendered (TIFF) inputs, which
    // already carry a baked-in tone curve — the stock's tonal character (toe/shoulder/
    // midtone) still shows through, but we don't double-map and blow the highlights.
    // 1.0 (default, RAW/parity) = full tone response, byte-identical to before.
    const float tone_k = clampf(response.tone_response_strength, 0.0F, 1.0F);
    parallel_for_index(static_cast<std::ptrdiff_t>(rgb_linear.pixel_count()), [&](std::ptrdiff_t pixel_index) {
        const std::size_t base = static_cast<std::size_t>(pixel_index) * 3U;
        if (tone_k >= 0.999F) {
            toned.pixels[base + 0U] = apply_tone_curve(rgb_linear.pixels[base + 0U], 0);
            toned.pixels[base + 1U] = apply_tone_curve(rgb_linear.pixels[base + 1U], 1);
            toned.pixels[base + 2U] = apply_tone_curve(rgb_linear.pixels[base + 2U], 2);
        } else {
            for (int c = 0; c < 3; ++c) {
                const float in = rgb_linear.pixels[base + static_cast<std::size_t>(c)];
                const float out = apply_tone_curve(in, c);
                toned.pixels[base + static_cast<std::size_t>(c)] = in + tone_k * (out - in);
            }
        }
    });

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
    const FilmResponsePlan& response,
    NativeEngineMetadata* metadata,
    const char* timing_prefix) const {
    return apply_color_response_and_coupling_pipeline(rgb_linear, zone_masks, response, metadata, timing_prefix);
}

Image FilmRenderer::apply_luminance_chroma_coupling(
    const Image& rgb_linear,
    const FilmResponsePlan& response) const {
    return apply_color_pipeline(rgb_linear, nullptr, response, false, true);
}

Image FilmRenderer::apply_subtractive_density(
    const Image& rgb_linear,
    const FilmResponsePlan& response) const {
    if (rgb_linear.channels != 3) {
        throw std::invalid_argument("apply_subtractive_density expects a 3-channel RGB image");
    }
    const float amt = std::max(response.density_strength, 0.0F)
        * std::clamp(response.film_color_density / 100.0F, 0.0F, 2.0F);
    Image out(rgb_linear.width, rgb_linear.height, 3);
    if (amt <= 0.0F) {
        out.pixels = rgb_linear.pixels;
        return out;
    }
    const float lo = response.density_low_luma_limit;
    parallel_for_index(static_cast<std::ptrdiff_t>(rgb_linear.pixel_count()), [&](std::ptrdiff_t i) {
        const OklabPixel lab = rgb_to_oklab_pixel(
            rgb_linear.pixels[i * 3 + 0],
            rgb_linear.pixels[i * 3 + 1],
            rgb_linear.pixels[i * 3 + 2]);
        const OklchPixel lch = oklab_to_oklch_pixel(lab);
        const float c_norm = std::clamp(lch.c / kOklabChromaRef, 0.0F, 1.0F);
        const float limiter = smoothstep01(lo, lo + kDensityLimitSoft, lch.l); // 0 deep shadow -> 1 above
        const float reduce = kDensityLumaMax * amt * c_norm * limiter;         // fractional L reduction
        const float l_new = std::max(lch.l * (1.0F - reduce), 0.0F);
        const OklabPixel adjusted = oklch_to_oklab_pixel({l_new, lch.c, lch.h});
        const auto rgb = oklab_to_rgb_pixel(adjusted);
        out.pixels[i * 3 + 0] = rgb[0];
        out.pixels[i * 3 + 1] = rgb[1];
        out.pixels[i * 3 + 2] = rgb[2];
    });
    return out;
}

Image FilmRenderer::apply_color_compression(
    const Image& rgb_linear,
    const FilmResponsePlan& response) const {
    if (rgb_linear.channels != 3) {
        throw std::invalid_argument("apply_color_compression expects a 3-channel RGB image");
    }
    const float ctl = std::clamp(response.film_color_compression / 100.0F, 0.0F, 2.0F);
    const float eff_strength = std::max(response.compression_strength, 0.0F) * ctl;
    const float eff_cross = std::max(response.compression_crosstalk, 0.0F) * ctl;
    const float t0 = std::clamp(response.compression_threshold, 0.0F, 1.0F);
    Image out(rgb_linear.width, rgb_linear.height, 3);
    if (eff_strength <= 0.0F && eff_cross <= 0.0F) {
        out.pixels = rgb_linear.pixels;
        return out;
    }
    parallel_for_index(static_cast<std::ptrdiff_t>(rgb_linear.pixel_count()), [&](std::ptrdiff_t i) {
        const OklabPixel lab = rgb_to_oklab_pixel(
            rgb_linear.pixels[i * 3 + 0], rgb_linear.pixels[i * 3 + 1], rgb_linear.pixels[i * 3 + 2]);
        const OklchPixel lch = oklab_to_oklch_pixel(lab);
        float cn = lch.c / kOklabChromaRef;                 // normalized chroma
        if (eff_strength > 0.0F && cn > t0) {               // soft chroma shoulder above threshold
            const float excess = cn - t0;
            const float k = kCompressK * eff_strength;
            cn = t0 + excess / (1.0F + k * excess);
        }
        const float c_new = std::max(cn * kOklabChromaRef, 0.0F);
        float h_new = lch.h;
        if (eff_cross > 0.0F) {                             // bounded, chroma-gated neighbour lean
            const float g_c = smoothstep01(kCompressCrossLo, kCompressCrossHi, lch.c);
            const float red_cos = clampf(std::cos(lch.h - 0.6F), 0.0F, 1.0F);
            const float blue_cos = clampf(std::cos(lch.h - 4.0F), 0.0F, 1.0F);
            const float w_red = red_cos * red_cos;
            const float w_blue = blue_cos * blue_cos;
            const float lean = kLeanRedSign * w_red + kLeanBlueSign * w_blue;
            h_new = wrap_angle_positive(lch.h + kCompressLeanGain * eff_cross * g_c * lean);
        }
        const OklabPixel adj = oklch_to_oklab_pixel({lch.l, c_new, h_new});
        const auto rgb = oklab_to_rgb_pixel(adj);
        out.pixels[i * 3 + 0] = rgb[0];
        out.pixels[i * 3 + 1] = rgb[1];
        out.pixels[i * 3 + 2] = rgb[2];
    });
    return out;
}

Image FilmRenderer::apply_hue_saturation(
    const Image& rgb_linear,
    const FilmResponsePlan& response) const {
    if (rgb_linear.channels != 3) {
        throw std::invalid_argument("apply_hue_saturation expects a 3-channel RGB image");
    }
    Image out(rgb_linear.width, rgb_linear.height, 3);

    const auto gain = [&](const char* key) {
        const auto it = response.hue_chroma_gain.find(key);
        return it != response.hue_chroma_gain.end() ? it->second : 0.0F;
    };
    // Hue-family centres in OKLCh radians (pure sRGB primaries/secondaries).
    struct Family { float center; float gain; };
    const std::array<Family, 7> families{{
        {0.51F, gain("red")},    {1.05F, gain("orange")}, {1.92F, gain("yellow")},
        {2.48F, gain("green")},  {3.40F, gain("cyan")},   {4.61F, gain("blue")},
        {5.73F, gain("magenta")},
    }};
    bool any = false;
    for (const auto& f : families) {
        if (f.gain != 0.0F) { any = true; break; }
    }
    if (!any) {
        out.pixels = rgb_linear.pixels;
        return out;
    }

    // Smooth Gaussian hue weighting; total gain clamped so the boost stays realistic.
    constexpr float kSigma = 0.55F;      // ~31 deg falloff -> smooth overlap, no bands
    constexpr float kMaxTotal = 0.35F;   // hard ceiling on the per-pixel chroma multiply
    const float inv_two_sigma_sq = 1.0F / (2.0F * kSigma * kSigma);
    const float two_pi = 2.0F * std::numbers::pi_v<float>;
    parallel_for_index(static_cast<std::ptrdiff_t>(rgb_linear.pixel_count()), [&](std::ptrdiff_t i) {
        const OklabPixel lab = rgb_to_oklab_pixel(
            rgb_linear.pixels[i * 3 + 0], rgb_linear.pixels[i * 3 + 1], rgb_linear.pixels[i * 3 + 2]);
        const OklchPixel lch = oklab_to_oklch_pixel(lab);
        float total = 0.0F;
        for (const auto& f : families) {
            if (f.gain == 0.0F) {
                continue;
            }
            float d = std::fmod(lch.h - f.center + std::numbers::pi_v<float>, two_pi);
            if (d < 0.0F) {
                d += two_pi;
            }
            d -= std::numbers::pi_v<float>;               // shortest circular distance
            total += std::exp(-(d * d) * inv_two_sigma_sq) * f.gain;
        }
        total = clampf(total, -kMaxTotal, kMaxTotal);
        // Chroma multiply: proportional to existing chroma, so neutrals are untouched.
        const float c_new = std::max(lch.c * (1.0F + total), 0.0F);
        const OklabPixel adj = oklch_to_oklab_pixel({lch.l, c_new, lch.h});
        const auto rgb = oklab_to_rgb_pixel(adj);
        out.pixels[i * 3 + 0] = rgb[0];
        out.pixels[i * 3 + 1] = rgb[1];
        out.pixels[i * 3 + 2] = rgb[2];
    });
    return out;
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
    // Masks may be a lower-resolution proxy (export path); upsampled on use below.
    if (effects.halation_strength <= 0.0F && effects.bloom_strength <= 0.0F) {
        return rgb_linear;
    }

    cv::Mat rgb = rgb_image_to_mat(rgb_linear, false);

    if (effects.halation_strength > 0.0F) {
        cv::Mat source = luminance_image_to_mat_scaled(spatial_masks.halation_source_mask, rgb_linear.width, rgb_linear.height);
        cv::Mat receiver = luminance_image_to_mat_scaled(spatial_masks.halation_receiver_mask, rgb_linear.width, rgb_linear.height);
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
        cv::Mat z5 = luminance_image_to_mat_scaled(zone_masks.zones[5], rgb_linear.width, rgb_linear.height);
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

Image FilmRenderer::apply_filmic_halation_bloom(
    const Image& rgb_linear,
    const ZoneMasks& zone_masks,
    const SpatialMasks& spatial_masks,
    const MaterialEffectsPlan& effects) const {
    if (rgb_linear.channels != 3) {
        throw std::invalid_argument("apply_filmic_halation_bloom expects a 3-channel RGB image");
    }
    // filmic halation is threshold-driven (physical: bright regions cause the halo), so the
    // analyzer's specular source/receiver masks and the highlight zone mask are not used here.
    (void)zone_masks;
    (void)spatial_masks;
    if (effects.halation_strength <= 0.0F && effects.bloom_strength <= 0.0F) {
        return rgb_linear;
    }

    cv::Mat rgb = rgb_image_to_mat(rgb_linear, false);   // full-res working buffer, composited in place
    const int full_w = rgb.cols;
    const int full_h = rgb.rows;

    // --- Build the glow on a small proxy. Physically, halation is light that penetrated the
    // emulsion, reflected off the film base/pressure plate and re-exposed the surrounding area
    // as a warm/red halo (pronounced on rem-jet-removed stocks like CineStill 800T, minimal on
    // stocks with intact anti-halation backing). The glow is inherently low-frequency, so we
    // compute it on a downscaled proxy and sample it back bilinearly at full res -- visually
    // identical to a full-res blur, but the glow fields stay tens of MB instead of ~1 GB. ---
    constexpr int kGlowProxyEdge = 1024;
    const int proxy_long = std::max(full_w, full_h);
    const float pscale = proxy_long > kGlowProxyEdge
        ? static_cast<float>(kGlowProxyEdge) / static_cast<float>(proxy_long)
        : 1.0F;
    const int pw = std::max(1, static_cast<int>(std::lround(full_w * pscale)));
    const int ph = std::max(1, static_cast<int>(std::lround(full_h * pscale)));
    cv::Mat proxy_rgb;
    if (pscale < 1.0F) {
        cv::resize(rgb, proxy_rgb, cv::Size(pw, ph), 0.0, 0.0, cv::INTER_AREA);
    } else {
        proxy_rgb = rgb;   // small image: read the shared buffer (only read before compositing)
    }

    const float hal_thresh = std::clamp(effects.halation_threshold, 0.30F, 0.80F);
    cv::Mat glow_src(ph, pw, CV_32F);      // scalar halation emitter (highlights above threshold)
    cv::Mat bloom_src(ph, pw, CV_32FC3);   // colour bloom emitter (highlight-weighted colour)
    for (int y = 0; y < ph; ++y) {
        const cv::Vec3f* prow = proxy_rgb.ptr<cv::Vec3f>(y);
        float* grow = glow_src.ptr<float>(y);
        cv::Vec3f* brow = bloom_src.ptr<cv::Vec3f>(y);
        for (int x = 0; x < pw; ++x) {
            const cv::Vec3f& p = prow[x];
            const float lum = 0.2126F * p[0] + 0.7152F * p[1] + 0.0722F * p[2];
            const float highlight = smoothstep01((lum - hal_thresh) / 0.20F);
            const float excess = std::max(0.0F, lum - (hal_thresh - 0.08F));
            const float source = clamp01(highlight * (0.35F + excess));
            grow[x] = source;
            const float bloom_weight = source * (0.55F + 0.45F * highlight);
            brow[x] = cv::Vec3f(p[0] * bloom_weight, p[1] * bloom_weight, p[2] * bloom_weight);
        }
    }

    // Blur radii in proxy pixels (radius params are normalized to a 2048 px short edge).
    const int pshort = std::max(1, std::min(pw, ph));
    const auto proxy_radius = [&](const float radius_2048, const float floor_px) {
        return std::max(floor_px, radius_2048 * static_cast<float>(pshort) / 2048.0F);
    };
    const float hal_tight_sigma = proxy_radius(std::max(1.0F, effects.halation_radius_inner), 1.0F);
    const float hal_wide_sigma =
        std::max(hal_tight_sigma + 1.0F, proxy_radius(std::max(effects.halation_radius_inner + 1.0F, effects.halation_radius_outer), 2.0F));
    const float bloom_tight_sigma = std::max(2.0F, static_cast<float>(pshort) / 95.0F);
    const float bloom_mid_sigma = std::max(4.0F, static_cast<float>(pshort) / 42.0F);
    const float bloom_wide_sigma = std::max(8.0F, static_cast<float>(pshort) / 18.0F);

    cv::Mat hal_tight;
    cv::Mat hal_wide;
    cv::Mat bloom_tight;
    cv::Mat bloom_mid;
    cv::Mat bloom_wide;
    cv::GaussianBlur(glow_src, hal_tight, cv::Size(0, 0), hal_tight_sigma);
    cv::GaussianBlur(glow_src, hal_wide, cv::Size(0, 0), hal_wide_sigma);
    cv::GaussianBlur(bloom_src, bloom_tight, cv::Size(0, 0), bloom_tight_sigma);
    cv::GaussianBlur(bloom_src, bloom_mid, cv::Size(0, 0), bloom_mid_sigma);
    cv::GaussianBlur(bloom_src, bloom_wide, cv::Size(0, 0), bloom_wide_sigma);

    const float halation_strength = std::max(0.0F, effects.halation_strength);
    const float bloom_strength = std::max(0.0F, effects.bloom_strength);
    const float combined_strength = std::clamp(halation_strength + bloom_strength, 0.0F, 2.0F);
    // Calibrated so the stock's authored strength manifests film-accurately: a subtle warm halo
    // at ~0.1-0.3 (Portra/Gold), minimal at ~0.05 (slide/B&W with good anti-halation) and a
    // strong red halation at ~0.6 (CineStill 800T). Additive (screen) -- halation adds light.
    const float hal_core_gain = 1.15F;
    const float hal_fringe_gain = 0.55F;
    const float bloom_gain = bloom_strength * 0.22F;

    const float sx = static_cast<float>(pw) / static_cast<float>(full_w);
    const float sy = static_cast<float>(ph) / static_cast<float>(full_h);
    parallel_for_rows(full_h, [&](int y) {
        const float py = (static_cast<float>(y) + 0.5F) * sy - 0.5F;
        cv::Vec3f* row = rgb.ptr<cv::Vec3f>(y);
        for (int x = 0; x < full_w; ++x) {
            const float px = (static_cast<float>(x) + 0.5F) * sx - 0.5F;
            cv::Vec3f& pixel = row[x];
            const float lum = 0.2126F * pixel[0] + 0.7152F * pixel[1] + 0.0722F * pixel[2];

            // Gentle highlight shoulder (film shoulders off where it diffuses) -- subtle, so the
            // net effect is a glow rather than a darkening.
            const float source_here = sample_mat_gray(glow_src, px, py);
            const float shoulder_loss = source_here * combined_strength * 0.06F;
            pixel[0] *= 1.0F - shoulder_loss;
            pixel[1] *= 1.0F - shoulder_loss;
            pixel[2] *= 1.0F - shoulder_loss;

            // Additive warm halation. The blur already localizes it around highlights; the
            // receiver term biases the deposit toward the darker ring around a highlight (where
            // a halo actually reads) without ever gating it to zero.
            const float core = sample_mat_gray(hal_tight, px, py) * halation_strength * hal_core_gain;
            const float fringe = sample_mat_gray(hal_wide, px, py) * halation_strength * hal_fringe_gain;
            const float receiver = 0.35F + 0.65F * clamp01(1.0F - lum);
            for (int channel = 0; channel < 3; ++channel) {
                const float add = (core * effects.halation_warm_core[static_cast<std::size_t>(channel)] +
                                   fringe * effects.halation_red_fringe[static_cast<std::size_t>(channel)]) *
                                  receiver;
                // Screen blend: keeps bright cores from hard-clipping while still adding light.
                pixel[channel] = pixel[channel] + add - pixel[channel] * add;
            }

            // Soft white/colour bloom.
            const cv::Vec3f bt = sample_mat_rgb(bloom_tight, px, py);
            const cv::Vec3f bm = sample_mat_rgb(bloom_mid, px, py);
            const cv::Vec3f bw = sample_mat_rgb(bloom_wide, px, py);
            pixel[0] += (0.52F * bt[0] + 0.31F * bm[0] + 0.17F * bw[0]) * bloom_gain * 1.05F;
            pixel[1] += (0.52F * bt[1] + 0.31F * bm[1] + 0.17F * bw[1]) * bloom_gain * 1.01F;
            pixel[2] += (0.52F * bt[2] + 0.31F * bm[2] + 0.17F * bw[2]) * bloom_gain * 0.88F;

            for (int channel = 0; channel < 3; ++channel) {
                if (pixel[channel] > 0.92F) {
                    const float over = pixel[channel] - 0.92F;
                    pixel[channel] = 0.92F + over / (1.0F + over * 3.5F);
                }
                pixel[channel] = clamp01(pixel[channel]);
            }
        }
    });

    return mat_to_rgb_image(rgb);
}

Image FilmRenderer::apply_film_grain(
    const Image& rgb_linear,
    const SpatialMasks& spatial_masks,
    const MaterialEffectsPlan& effects) const {
    if (rgb_linear.channels != 3) {
        throw std::invalid_argument("apply_film_grain expects a 3-channel RGB image");
    }
    // Receptivity mask may be a lower-resolution proxy (export path); upsampled on use.
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
        .grain_clumpiness_q = 0,
        .grain_micro_grit_q = 0,
        .grain_layer_correlation_q = 0,
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

    Image out(rgb_linear.width, rgb_linear.height, 3);
    constexpr std::array<float, 3> kStrengthMults{0.75F, 0.95F, 1.35F};
    const float strength_r = effects.grain_strength * 0.038F * kStrengthMults[0];
    const float strength_g = effects.grain_strength * 0.038F * kStrengthMults[1];
    const float strength_b = effects.grain_strength * 0.038F * kStrengthMults[2];
    std::vector<float> receptivity_scratch;
    const auto& grain_receptivity = scaled_receptivity_values(spatial_masks.grain_receptivity_mask, w, h, receptivity_scratch);
    static const auto kGammaEncodeLut = build_power_lut(1.0F / 2.2F);
    static const auto kGammaDecodeLut = build_power_lut(2.2F);
    static const auto kGrainModulationLut = build_grain_modulation_lut();

    parallel_for_rows(h, [&](int y) {
        const float* noise_r_row = noise_r.ptr<float>(y);
        const float* noise_g_row = noise_g.ptr<float>(y);
        const float* noise_b_row = noise_b.ptr<float>(y);
        for (int x = 0; x < w; ++x) {
            const std::size_t pixel_index = static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x);
            const std::size_t base = pixel_index * 3U;
            const float smooth_mod = grain_receptivity[pixel_index];

            const float gamma_r = sample_unit_lut(kGammaEncodeLut, rgb_linear.pixels[base + 0]);
            const float gamma_g = sample_unit_lut(kGammaEncodeLut, rgb_linear.pixels[base + 1]);
            const float gamma_b = sample_unit_lut(kGammaEncodeLut, rgb_linear.pixels[base + 2]);

            const float gamma_out_r = clamp01(gamma_r + noise_r_row[x] * strength_r * sample_unit_lut(kGrainModulationLut, gamma_r) * smooth_mod);
            const float gamma_out_g = clamp01(gamma_g + noise_g_row[x] * strength_g * sample_unit_lut(kGrainModulationLut, gamma_g) * smooth_mod);
            const float gamma_out_b = clamp01(gamma_b + noise_b_row[x] * strength_b * sample_unit_lut(kGrainModulationLut, gamma_b) * smooth_mod);

            out.pixels[base + 0] = sample_unit_lut(kGammaDecodeLut, gamma_out_r);
            out.pixels[base + 1] = sample_unit_lut(kGammaDecodeLut, gamma_out_g);
            out.pixels[base + 2] = sample_unit_lut(kGammaDecodeLut, gamma_out_b);
        }
    });

    return out;
}

Image FilmRenderer::apply_filmic_grain(
    const Image& rgb_linear,
    const SpatialMasks& spatial_masks,
    const MaterialEffectsPlan& effects) const {
    if (rgb_linear.channels != 3) {
        throw std::invalid_argument("apply_filmic_grain expects a 3-channel RGB image");
    }
    // Grain is coordinate-addressable (Phase 3); it no longer reads the receptivity mask.
    (void)spatial_masks;
    if (effects.grain_strength == 0.0F) {
        return rgb_linear;
    }

    const int h = rgb_linear.height;
    const int w = rgb_linear.width;
    const float scale_factor = static_cast<float>(w) / 2048.0F;
    const bool is_mono = effects.grain_chroma_strength <= 0.0F;
    // Large images generate the noise on a bounded PERIODIC tile and sample it wrapped, so
    // the field stays ~48 MB instead of the full-res ~1.6 GB. Grain feature SIZE is unchanged
    // (scale_factor is from the full width); only the repeat period is bounded. Images that
    // already fit the tile keep the exact old path (byte-identical).
    constexpr int kGrainTileMax = 2048;
    constexpr int kGrainTileMaxB = 1728;  // coprime-ish 2nd octave (gcd 64 -> lcm 55296 >> any image)
    constexpr int kGrainWrapMargin = 24;  // >= 3x max blur sigma, so the wrapped blur is seamless
    const bool grain_tiled = (w > kGrainTileMax || h > kGrainTileMax);
    const int gen_w = grain_tiled ? std::min(w, kGrainTileMax) : w;
    const int gen_h = grain_tiled ? std::min(h, kGrainTileMax) : h;
    // 2nd-octave canvas: only shrink on axes that are actually tiled (period < image), so a
    // non-tiled axis keeps its full-width period and never gains repetition from detiling.
    const int gen_w2 = (w > kGrainTileMax) ? kGrainTileMaxB : gen_w;
    const int gen_h2 = (h > kGrainTileMax) ? kGrainTileMaxB : gen_h;
    const std::uint32_t grain_seed = effects.grain_seed != 0U ? effects.grain_seed : compute_grain_seed(rgb_linear);
    const GrainNoiseCacheKey cache_key{
        .width = w,
        .height = h,
        .seed = grain_seed ^ 0x9E3779B9U,
        .grain_size_q = quantize_grain_param(effects.grain_size),
        .grain_roughness_q = quantize_grain_param(effects.grain_roughness),
        .grain_chroma_q = quantize_grain_param(effects.grain_chroma_strength),
        .grain_clumpiness_q = quantize_grain_param(effects.grain_clumpiness),
        .grain_micro_grit_q = quantize_grain_param(effects.grain_micro_grit),
        .grain_layer_correlation_q = quantize_grain_param(effects.grain_layer_correlation),
    };
    static thread_local std::optional<GrainNoiseCacheEntry> filmic_grain_noise_cache;

    cv::Mat noise_r;
    cv::Mat noise_g;
    cv::Mat noise_b;
    cv::Mat noise_r2;  // 2nd octave (tiled path only)
    cv::Mat noise_g2;
    cv::Mat noise_b2;

    if (filmic_grain_noise_cache.has_value() && filmic_grain_noise_cache->key.matches(cache_key)) {
        noise_r = filmic_grain_noise_cache->noise_r;
        noise_g = filmic_grain_noise_cache->noise_g;
        noise_b = filmic_grain_noise_cache->noise_b;
        noise_r2 = filmic_grain_noise_cache->noise_r2;
        noise_g2 = filmic_grain_noise_cache->noise_g2;
        noise_b2 = filmic_grain_noise_cache->noise_b2;
    } else {
        std::mt19937_64 rng(static_cast<std::uint64_t>(grain_seed) ^ 0xD1B54A32D192ED03ULL);
        // Fine, resolution-scaled grain: grain_size drives a small blur setting the physical
        // grain cell; roughness shapes particle hardness. No sparse-impulse clumps (those
        // produced the "blurred oil-blotch" look).
        // Gentler size response: allow the full slider range and grow grain size more slowly
        // so low/mid sizes are subtle and the top is not oversized.
        const float grain_cell = std::clamp(effects.grain_size, 0.05F, 2.0F) * std::max(scale_factor, 0.35F);
        const float grain_sigma = std::clamp(0.30F + grain_cell * 0.55F, 0.30F, 3.0F);
        // Gentler roughness/crispness response (less extreme at the top).
        const float roughness = std::clamp(effects.grain_roughness, 0.0F, 1.0F);

        // Generate one correlated-noise tile at an explicit size. On the tiled path the blur
        // is wrap-padded so the tile is periodic (seamless when wrap-sampled); when not tiled
        // (tw==w, th==h) this is the exact original full-image path (byte-identical).
        auto make_tile = [&](std::mt19937_64& r, const int th, const int tw) {
            cv::Mat m = make_standard_normal_mat(th, tw, r);
            if (grain_sigma > 0.35F) {
                if (grain_tiled) {
                    cv::Mat padded;
                    cv::copyMakeBorder(m, padded, kGrainWrapMargin, kGrainWrapMargin,
                                       kGrainWrapMargin, kGrainWrapMargin, cv::BORDER_WRAP);
                    cv::GaussianBlur(padded, padded, cv::Size(0, 0), grain_sigma);
                    m = padded(cv::Rect(kGrainWrapMargin, kGrainWrapMargin, tw, th)).clone();
                } else {
                    cv::GaussianBlur(m, m, cv::Size(0, 0), grain_sigma);
                }
            }
            normalize_zero_mean_unit_variance(m);
            if (roughness > 0.0F) {
                const float p = 1.0F / (0.70F + roughness * 0.55F);
                for (int yy = 0; yy < m.rows; ++yy) {
                    float* row = m.ptr<float>(yy);
                    for (int xx = 0; xx < m.cols; ++xx) {
                        const float v = row[xx];
                        row[xx] = std::copysign(std::pow(std::fabs(v), p), v);
                    }
                }
                normalize_zero_mean_unit_variance(m);
            }
            return m;
        };

        // Most colour-grain structure is shared between dye layers. Independent
        // variation remains restrained so it does not turn into RGB speckle.
        const float base_chroma = clampf(effects.grain_chroma_strength * 1.35F, 0.0F, 0.42F);
        const float layer_correlation = std::clamp(effects.grain_layer_correlation, 0.0F, 1.0F);
        const float chroma_mix = base_chroma * std::clamp(0.12F + (1.0F - layer_correlation) * 0.35F, 0.12F, 0.47F);

        // Assemble the R/G/B noise for one octave from its mono + independent chroma tiles.
        const auto assemble = [&](cv::Mat& out_r, cv::Mat& out_g, cv::Mat& out_b,
                                  const cv::Mat& mono, const cv::Mat& ind_r, const cv::Mat& ind_b) {
            if (is_mono) {
                out_r = mono;
                out_g = mono;
                out_b = mono;
            } else {
                out_g = mono;
                out_r = (1.0F - chroma_mix) * mono + chroma_mix * ind_r;
                out_b = (1.0F - chroma_mix) * mono + chroma_mix * ind_b;
                normalize_zero_mean_unit_variance(out_r);
                normalize_zero_mean_unit_variance(out_b);
            }
        };

        // Octave A (primary tile). Draw order matches the original exactly on the non-tiled
        // path (mono, ind_r, ind_b) so preview output stays byte-identical.
        cv::Mat mono = make_tile(rng, gen_h, gen_w);
        cv::Mat ind_r = is_mono ? cv::Mat() : make_tile(rng, gen_h, gen_w);
        cv::Mat ind_b = is_mono ? cv::Mat() : make_tile(rng, gen_h, gen_w);
        assemble(noise_r, noise_g, noise_b, mono, ind_r, ind_b);

        // Octave B (detiling): only on the tiled path, at the coprime period.
        if (grain_tiled) {
            cv::Mat mono2 = make_tile(rng, gen_h2, gen_w2);
            cv::Mat ind_r2 = is_mono ? cv::Mat() : make_tile(rng, gen_h2, gen_w2);
            cv::Mat ind_b2 = is_mono ? cv::Mat() : make_tile(rng, gen_h2, gen_w2);
            assemble(noise_r2, noise_g2, noise_b2, mono2, ind_r2, ind_b2);
        }

        filmic_grain_noise_cache = GrainNoiseCacheEntry{
            .key = cache_key,
            .noise_r = noise_r,
            .noise_g = noise_g,
            .noise_b = noise_b,
            .noise_r2 = noise_r2,
            .noise_g2 = noise_g2,
            .noise_b2 = noise_b2,
        };
    }

    Image out(rgb_linear.width, rgb_linear.height, 3);
    // Per-dye-layer amplitude (blue layer slightly grainier) for colour grain only.
    // Monochrome grain must stay truly neutral across channels, so use uniform amplitude.
    constexpr std::array<float, 3> kColorAmpMults{0.97F, 1.00F, 1.05F};
    const std::array<float, 3> kAmpMults = is_mono ? std::array<float, 3>{1.0F, 1.0F, 1.0F} : kColorAmpMults;
    const float pgi_visibility = std::clamp(effects.grain_target_pgi / 40.0F, 0.55F, 1.55F);
    const float stock_visibility = pgi_visibility * std::clamp(0.82F + effects.grain_midtone_response * 0.18F, 0.65F, 1.25F);
    // Spatially uniform: no receptivity / texture-detail term (that spatial
    // gating caused the blotches). Modulated only by each pixel's smooth tone response.
    const float gs = std::max(0.0F, effects.grain_strength);
    const float strength_shaped = gs / (1.0F + 0.6F * gs);   // 0.5->0.38, 1.0->0.63, 2.0->0.91
    const float sigma_base = strength_shaped * kGrainDensitySigma * stock_visibility;
    static const auto kGammaEncodeLut = build_power_lut(1.0F / 2.2F);
    static const auto kDensityFromLinearLut = build_density_from_linear_lut();
    static const auto kLinearFromDensityLut = build_linear_from_density_lut();

    const int noise_h = noise_g.rows;   // == h when not tiled -> wrapped indices are identity
    const int noise_w = noise_g.cols;
    const int noise_h2 = grain_tiled ? noise_g2.rows : 1;
    const int noise_w2 = grain_tiled ? noise_g2.cols : 1;
    constexpr float kInvSqrt2 = 0.70710678F;  // keeps unit variance when summing the 2 octaves
    parallel_for_rows(h, [&](const int y) {
        const float* noise_r_row = noise_r.ptr<float>(y % noise_h);
        const float* noise_g_row = noise_g.ptr<float>(y % noise_h);
        const float* noise_b_row = noise_b.ptr<float>(y % noise_h);
        const float* noise_r2_row = grain_tiled ? noise_r2.ptr<float>(y % noise_h2) : nullptr;
        const float* noise_g2_row = grain_tiled ? noise_g2.ptr<float>(y % noise_h2) : nullptr;
        const float* noise_b2_row = grain_tiled ? noise_b2.ptr<float>(y % noise_h2) : nullptr;
        for (int x = 0; x < w; ++x) {
            const int nx = x % noise_w;
            float nr = noise_r_row[nx];
            float ng = noise_g_row[nx];
            float nb = noise_b_row[nx];
            if (grain_tiled) {
                const int nx2 = x % noise_w2;
                nr = (nr + noise_r2_row[nx2]) * kInvSqrt2;
                ng = (ng + noise_g2_row[nx2]) * kInvSqrt2;
                nb = (nb + noise_b2_row[nx2]) * kInvSqrt2;
            }
            const std::size_t pixel_index = static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x);
            const std::size_t base = pixel_index * 3U;

            const float input_r = clamp01(rgb_linear.pixels[base + 0]);
            const float input_g = clamp01(rgb_linear.pixels[base + 1]);
            const float input_b = clamp01(rgb_linear.pixels[base + 2]);
            const float luma_linear = 0.2126F * input_r + 0.7152F * input_g + 0.0722F * input_b;
            const float luma_density = sample_unit_lut(kDensityFromLinearLut, luma_linear);
            const float y_gamma = sample_unit_lut(kGammaEncodeLut, luma_linear);

            const float lifted_shadow = smoothstep01((0.42F - y_gamma) / 0.34F);
            const float lower_mid = smoothstep01((y_gamma - 0.16F) / 0.24F) * (1.0F - smoothstep01((y_gamma - 0.58F) / 0.24F));
            const float midtone = smoothstep01((y_gamma - 0.30F) / 0.22F) * (1.0F - smoothstep01((y_gamma - 0.72F) / 0.22F));
            const float highlight = smoothstep01((y_gamma - 0.70F) / 0.22F);
            float shadow_zone_scale = 1.0F;
            float lower_mid_zone_scale = 1.0F;
            float midtone_zone_scale = 1.0F;
            float highlight_zone_scale = 1.0F;
            if (effects.grain_peak_zone == "midtones_heavy") {
                shadow_zone_scale = 0.76F;
                lower_mid_zone_scale = 0.78F;
                midtone_zone_scale = 1.24F;
                highlight_zone_scale = 0.72F;
            } else if (effects.grain_peak_zone == "midtones") {
                shadow_zone_scale = 0.84F;
                lower_mid_zone_scale = 0.90F;
                midtone_zone_scale = 1.12F;
                highlight_zone_scale = 0.76F;
            } else if (effects.grain_peak_zone == "highlights_light") {
                shadow_zone_scale = 0.82F;
                lower_mid_zone_scale = 0.90F;
                midtone_zone_scale = 0.96F;
                highlight_zone_scale = 0.48F;
            }
            const float shadow_response = std::clamp(
                (effects.grain_shadow_response + effects.grain_underexposure_coarsening * 0.20F) * shadow_zone_scale,
                0.0F,
                1.5F);
            const float lower_mid_response = std::clamp(effects.grain_midtone_response * lower_mid_zone_scale, 0.0F, 1.5F);
            const float midtone_response = std::clamp(effects.grain_midtone_response * midtone_zone_scale, 0.0F, 1.5F);
            const float highlight_response = std::clamp(
                (effects.grain_highlight_response - effects.grain_overexposure_smoothing * 0.18F) * highlight_zone_scale,
                0.0F,
                0.85F);
            const float density_mod =
                (0.35F + shadow_response * 0.55F * lifted_shadow + lower_mid_response * 0.95F * lower_mid + midtone_response * 0.55F * midtone) *
                (1.0F - (0.70F - highlight_response) * highlight);
            // Tonal (per-luminance) modulation only — smooth, cannot blotch. No spatial term.
            // Clear highlights and blocked shadows remain quiet. This envelope
            // is signal-dependent but never tied to neighbouring image detail.
            const float clear_highlight_taper = smoothstep01(0.16F, 0.60F, luma_density);
            const float blocked_shadow_taper = 1.0F - smoothstep01(3.00F, 5.00F, luma_density);
            const float sigma_luma = sigma_base * std::max(0.0F, density_mod) *
                clear_highlight_taper * blocked_shadow_taper;

            const auto apply_density_grain = [&](const float input, const float noise, const float channel_multiplier) {
                if (input <= kGrainDensityFloor || input >= 1.0F - kGrainDensityFloor) {
                    return input;
                }
                const float sigma = sigma_luma * channel_multiplier;
                // +0.5*sigma^2 compensates the log-normal inverse transform,
                // preventing a zero-mean field from visibly darkening a flat patch.
                const float adjusted_density = sample_unit_lut(kDensityFromLinearLut, input) + sigma * noise + 0.5F * sigma * sigma;
                return clamp01(sample_density_lut(kLinearFromDensityLut, adjusted_density));
            };

            out.pixels[base + 0] = apply_density_grain(input_r, nr, kAmpMults[0]);
            out.pixels[base + 1] = apply_density_grain(input_g, ng, kAmpMults[1]);
            out.pixels[base + 2] = apply_density_grain(input_b, nb, kAmpMults[2]);
        }
    });

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

    // Per-channel print tone curve (deeper print engine): a filmic S-curve per R/G/B whose
    // per-channel toe/shoulder differences put the print's colour into the tone scale
    // (warm highlights, dense/cool shadows, etc.). Identity when toe == shoulder == 0, so
    // profiles without these fields are unchanged. Runs before the paper black-point lift
    // so matte (open blacks) and contrasty (rich blacks) prints both behave correctly.
    const float p_toe = std::max(0.0F, print_finish.print_toe) * strength;
    const float p_shoulder = std::max(0.0F, print_finish.print_shoulder) * strength;
    if (p_toe > 0.0F || p_shoulder > 0.0F) {
        constexpr std::size_t kPrintCurveLut = 4096U;
        std::array<std::vector<float>, 3> curve_luts;
        for (int c = 0; c < 3; ++c) {
            const float alpha = 1.0F + p_toe * print_finish.channel_toe_mult[static_cast<std::size_t>(c)];
            const float beta = 1.0F + p_shoulder * print_finish.channel_shoulder_mult[static_cast<std::size_t>(c)];
            curve_luts[static_cast<std::size_t>(c)].resize(kPrintCurveLut);
            for (std::size_t i = 0; i < kPrintCurveLut; ++i) {
                const float x = static_cast<float>(i) / static_cast<float>(kPrintCurveLut - 1U);
                const float xs = clampf(x, 1.0e-6F, 1.0F - 1.0e-6F);
                const float xa = std::pow(xs, alpha);
                curve_luts[static_cast<std::size_t>(c)][i] =
                    clamp01(xa / (xa + std::pow(1.0F - xs, beta)));
            }
        }
        for (std::size_t i = 0; i < rgb.pixel_count(); ++i) {
            for (int c = 0; c < 3; ++c) {
                const auto& lut = curve_luts[static_cast<std::size_t>(c)];
                const float scaled = clamp01(rgb.pixels[i * 3 + static_cast<std::size_t>(c)]) *
                    static_cast<float>(kPrintCurveLut - 1U);
                const std::size_t lo = static_cast<std::size_t>(scaled);
                const std::size_t hi = std::min(lo + 1U, kPrintCurveLut - 1U);
                const float t = scaled - static_cast<float>(lo);
                rgb.pixels[i * 3 + static_cast<std::size_t>(c)] = lut[lo] * (1.0F - t) + lut[hi] * t;
            }
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
