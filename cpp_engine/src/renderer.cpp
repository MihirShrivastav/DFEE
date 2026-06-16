#include "dfee/renderer.hpp"
#include "dfee/color_spaces.hpp"

#include <array>
#include <algorithm>
#include <cmath>
#include <numbers>
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

[[nodiscard]] int odd_kernel_size(const int candidate, const int min_size, const int max_extent) {
    int size = std::max(min_size, candidate);
    const int max_odd = (max_extent % 2 == 0) ? std::max(3, max_extent - 1) : max_extent;
    size = std::min(size, max_odd);
    if ((size % 2) == 0) {
        size = std::max(min_size, size - 1);
    }
    return std::max(3, size);
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

}  // namespace

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
    for (int channel = 0; channel < 3; ++channel) {
        const float alpha = 1.0F + response.toe_strength * response.channel_toe_mult[static_cast<std::size_t>(channel)];
        const float beta = 1.0F + response.shoulder_strength * response.channel_shoulder_mult[static_cast<std::size_t>(channel)];
        const float mid = response.midtone_density * response.channel_midtone_mult[static_cast<std::size_t>(channel)];
        const float k = 2.0F + response.shoulder_strength * response.channel_shoulder_mult[static_cast<std::size_t>(channel)];

        for (int y = 0; y < rgb_linear.height; ++y) {
            for (int x = 0; x < rgb_linear.width; ++x) {
                const float ch = rgb_linear.at(x, y, channel);
                const float ch_safe = ch > 1.0F
                    ? 1.0F - 1.0F / std::max(1.0e-6F, 1.0F + k * (ch - 1.0F))
                    : ch;
                const float ch_clamp = clampf(ch_safe, 1.0e-12F, 1.0F - 1.0e-12F);
                float s_curve = std::pow(ch_clamp, alpha) /
                    (std::pow(ch_clamp, alpha) + std::pow(1.0F - ch_clamp, beta));

                const float delta = s_curve - 0.5F;
                const float mid_weight = std::exp(-(delta * delta) / (2.0F * 0.12F * 0.12F));
                if (mid != 1.0F) {
                    const float s_curve_gamma = std::pow(s_curve, 1.0F / mid);
                    s_curve = s_curve * (1.0F - mid_weight) + s_curve_gamma * mid_weight;
                }

                const float toe_fade = clampf(s_curve / 0.25F, 0.0F, 1.0F);
                const float shadow_weight = (1.0F - toe_fade) * (1.0F - toe_fade);
                toned.at(x, y, channel) = clamp01(s_curve + response.black_density_floor * shadow_weight);
            }
        }
    }

    return toned;
}

Image FilmRenderer::apply_color_response(
    const Image& rgb_linear,
    const ZoneMasks& zone_masks,
    const FilmResponsePlan& response) const {
    if (rgb_linear.channels != 3) {
        throw std::invalid_argument("apply_color_response expects a 3-channel RGB image");
    }
    for (const auto& zone : zone_masks.zones) {
        if (zone.width != rgb_linear.width || zone.height != rgb_linear.height) {
            throw std::invalid_argument("apply_color_response expects zone masks to match the RGB image dimensions");
        }
    }

    const float fc = response.film_color / 100.0F;
    Image out(rgb_linear.width, rgb_linear.height, 3);
    constexpr float kBiasScale = 0.004F;
    const float chroma_boost = 1.0F + (response.chroma_boost - 1.0F) * fc;
    const float red_comp = response.red_orange_compression * fc;
    const float blue_comp = response.blue_cyan_compression * fc;
    const float neon_comp = response.neon_compression * fc;
    const float highlight_desat = response.highlight_desaturation * fc;

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

        const float z1 = zone_masks.zones[1].values[i];
        const float z2 = zone_masks.zones[2].values[i];
        const float z3 = zone_masks.zones[3].values[i];
        const float z4 = zone_masks.zones[4].values[i];
        const float z5 = zone_masks.zones[5].values[i];

        const float shadow_zone = z1 + z2 * 0.5F;
        const float mid_zone = z2 * 0.5F + z3 + z4 * 0.5F;
        const float hi_zone = z4 * 0.5F + z5;

        float a_bias = 0.0F;
        float b_bias = 0.0F;
        a_bias += shadow_zone * response.shadow_bias_lab[1] * kBiasScale * fc;
        b_bias += shadow_zone * response.shadow_bias_lab[2] * kBiasScale * fc;
        a_bias += mid_zone * response.midtone_bias_lab[1] * kBiasScale * fc;
        b_bias += mid_zone * response.midtone_bias_lab[2] * kBiasScale * fc;
        a_bias += hi_zone * response.highlight_bias_lab[1] * kBiasScale * fc;
        b_bias += hi_zone * response.highlight_bias_lab[2] * kBiasScale * fc;

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
        const auto rgb = oklab_to_rgb_pixel(adjusted);
        out.pixels[i * 3 + 0] = rgb[0];
        out.pixels[i * 3 + 1] = rgb[1];
        out.pixels[i * 3 + 2] = rgb[2];
    }

    return out;
}

Image FilmRenderer::apply_luminance_chroma_coupling(
    const Image& rgb_linear,
    const FilmResponsePlan& response) const {
    if (rgb_linear.channels != 3) {
        throw std::invalid_argument("apply_luminance_chroma_coupling expects a 3-channel RGB image");
    }

    const float fc = response.film_color / 100.0F;
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

    Image out(rgb_linear.width, rgb_linear.height, 3);
    for (std::size_t i = 0; i < rgb_linear.pixel_count(); ++i) {
        const OklabPixel oklab = rgb_to_oklab_pixel(
            clamp01(rgb_linear.pixels[i * 3 + 0]),
            clamp01(rgb_linear.pixels[i * 3 + 1]),
            clamp01(rgb_linear.pixels[i * 3 + 2]));
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
        const auto rgb = oklab_to_rgb_pixel(adjusted);
        out.pixels[i * 3 + 0] = rgb[0];
        out.pixels[i * 3 + 1] = rgb[1];
        out.pixels[i * 3 + 2] = rgb[2];
    }
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

}  // namespace dfee
