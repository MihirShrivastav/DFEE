#include "dfee/renderer.hpp"

#include <array>
#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

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

}  // namespace dfee
