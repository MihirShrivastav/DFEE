#include "dfee/analyzer.hpp"
#include "dfee/bias.hpp"
#include "dfee/color_spaces.hpp"
#include "dfee/image.hpp"
#include "dfee/profile.hpp"
#include "dfee/renderer.hpp"
#include "dfee/session.hpp"
#include "dfee/solver.hpp"
#include "dfee/version.hpp"

#include <cassert>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void require_close(const float actual, const float expected, const float tolerance) {
    assert(std::fabs(actual - expected) <= tolerance);
}

float channel_stddev_gamma(const dfee::Image& rgb, const int channel) {
    double sum = 0.0;
    double sum_sq = 0.0;
    const std::size_t count = rgb.pixel_count();
    for (std::size_t i = 0; i < count; ++i) {
        const float gamma = std::pow(dfee::clamp01(rgb.pixels[i * 3 + static_cast<std::size_t>(channel)]), 1.0F / 2.2F);
        sum += gamma;
        sum_sq += gamma * gamma;
    }
    const double mean = sum / static_cast<double>(count);
    return static_cast<float>(std::sqrt(std::max(0.0, sum_sq / static_cast<double>(count) - mean * mean)));
}

float gamma_luminance_stddev(const dfee::Image& rgb) {
    double sum = 0.0;
    double sum_sq = 0.0;
    const std::size_t count = rgb.pixel_count();
    for (std::size_t i = 0; i < count; ++i) {
        const float r = std::pow(dfee::clamp01(rgb.pixels[i * 3 + 0]), 1.0F / 2.2F);
        const float g = std::pow(dfee::clamp01(rgb.pixels[i * 3 + 1]), 1.0F / 2.2F);
        const float b = std::pow(dfee::clamp01(rgb.pixels[i * 3 + 2]), 1.0F / 2.2F);
        const float y = 0.2126F * r + 0.7152F * g + 0.0722F * b;
        sum += y;
        sum_sq += y * y;
    }
    const double mean = sum / static_cast<double>(count);
    return static_cast<float>(std::sqrt(std::max(0.0, sum_sq / static_cast<double>(count) - mean * mean)));
}

void test_oklab_roundtrip() {
    dfee::Image rgb(1, 1, 3);
    rgb.pixels = {0.42F, 0.18F, 0.72F};
    const dfee::Image roundtrip = dfee::oklab_to_rgb(dfee::rgb_to_oklab(rgb));
    require_close(roundtrip.pixels[0], rgb.pixels[0], 1.0e-4F);
    require_close(roundtrip.pixels[1], rgb.pixels[1], 1.0e-4F);
    require_close(roundtrip.pixels[2], rgb.pixels[2], 1.0e-4F);
}

void test_zone_partition() {
    dfee::LuminanceImage luminance(16, 16);
    for (size_t i = 0; i < luminance.values.size(); ++i) {
        luminance.values[i] = 0.01F + static_cast<float>(i) / static_cast<float>(luminance.values.size());
    }

    const dfee::ImageStateAnalyzer analyzer;
    const auto masks = analyzer.generate_zone_masks(luminance, 0.18F);
    for (size_t i = 0; i < luminance.values.size(); ++i) {
        float sum = 0.0F;
        for (const auto& zone : masks.zones) {
            sum += zone.values[i];
        }
        require_close(sum, 1.0F, 1.0e-5F);
    }
}

void test_tonal_analysis() {
    dfee::LuminanceImage luminance(10, 10);
    for (size_t i = 0; i < luminance.values.size(); ++i) {
        luminance.values[i] = static_cast<float>(i) / 99.0F;
    }

    const dfee::ImageStateAnalyzer analyzer;
    const auto tonal = analyzer.analyze_tonal(luminance, {{"red", 0.0F}, {"green", 0.0F}, {"blue", 0.0F}});
    require_close(tonal.luma_p50, 0.5F, 1.0e-5F);
    assert(tonal.white_point_actual > tonal.black_point_actual);
}

void test_color_analysis() {
    dfee::Image rgb(4, 2, 3);
    rgb.pixels = {
        1.0F, 0.0F, 0.0F,   1.0F, 0.5F, 0.0F,   1.0F, 1.0F, 0.0F,   0.0F, 1.0F, 0.0F,
        0.0F, 1.0F, 1.0F,   0.0F, 0.0F, 1.0F,   1.0F, 0.0F, 1.0F,   0.5F, 0.5F, 0.5F,
    };
    const auto luminance = dfee::compute_luminance(rgb);
    const dfee::ImageStateAnalyzer analyzer;
    const auto zones = analyzer.generate_zone_masks(luminance, 0.18F);
    const auto color = analyzer.analyze_color(rgb, zones);

    assert(color.mean_chroma > 0.0F);
    assert(color.sat_p95 >= color.mean_chroma);
    assert(color.neon_risk > 0.0F);
    assert(color.hue_entropy > 0.0F);
    assert(color.red_orange_density > 0.0F);
    assert(color.cyan_blue_ratio > 0.0F);
    assert(color.warm_cool_ratio > 0.0F);
    assert(!color.dominant_hue_bins[0].empty());
}

void test_spatial_analysis() {
    dfee::LuminanceImage luminance(32, 32);
    for (int y = 0; y < luminance.height; ++y) {
        for (int x = 0; x < luminance.width; ++x) {
            const float gradient = static_cast<float>(x) / static_cast<float>(luminance.width - 1);
            luminance.at(x, y) = 0.05F + 0.25F * gradient;
        }
    }
    for (int y = 8; y < 24; ++y) {
        for (int x = 8; x < 24; ++x) {
            if (((x + y) % 2) == 0) {
                luminance.at(x, y) += 0.15F;
            }
        }
    }
    luminance.at(16, 16) = 1.0F;
    luminance.at(17, 16) = 0.94F;
    luminance.at(16, 17) = 0.92F;
    luminance.at(17, 17) = 0.88F;

    const dfee::ImageStateAnalyzer analyzer;
    const auto [spatial, masks] = analyzer.analyze_spatial(luminance);

    assert(spatial.texture_density > 0.0F);
    assert(spatial.smooth_area_ratio >= 0.0F && spatial.smooth_area_ratio <= 1.0F);
    assert(spatial.edge_density > 0.0F);
    assert(spatial.specular_point_ratio > 0.0F);
    assert(spatial.large_highlight_area_ratio >= 0.0F);
    assert(masks.grain_receptivity_mask.width == luminance.width);
    assert(masks.grain_receptivity_mask.height == luminance.height);
    assert(masks.halation_source_mask.at(16, 16) > 0.0F);
    assert(masks.halation_receiver_mask.at(15, 15) > 0.0F);
}

void test_camera_bias_estimator() {
    dfee::Image rgb(12, 12, 3);
    dfee::DecodedRawChannelMasks clipping_masks;
    clipping_masks.red.assign(rgb.pixel_count(), 0);
    clipping_masks.green.assign(rgb.pixel_count(), 0);
    clipping_masks.blue.assign(rgb.pixel_count(), 0);

    for (int y = 0; y < rgb.height; ++y) {
        for (int x = 0; x < rgb.width; ++x) {
            rgb.at(x, y, 0) = 0.38F;
            rgb.at(x, y, 1) = 0.40F;
            rgb.at(x, y, 2) = 0.44F;
        }
    }
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            rgb.at(x, y, 0) = 0.08F;
            rgb.at(x, y, 1) = 0.11F;
            rgb.at(x, y, 2) = 0.20F;
        }
    }
    for (int y = 8; y < 12; ++y) {
        for (int x = 8; x < 12; ++x) {
            rgb.at(x, y, 0) = 0.82F;
            rgb.at(x, y, 1) = 0.83F;
            rgb.at(x, y, 2) = 0.86F;
        }
    }

    const auto luminance = dfee::compute_luminance(rgb);
    const dfee::ImageStateAnalyzer analyzer;
    const auto zones = analyzer.generate_zone_masks(luminance, 0.18F);
    const dfee::CameraBiasEstimator estimator;
    const auto bias = estimator.estimate_bias(rgb, clipping_masks, zones);

    assert(bias.neutral_confidence > 0.0F);
    assert(bias.global_cast_lab[0] > 0.0F);
    assert(bias.blue_excess_index >= 0.0F);
    assert(bias.warm_cool_bias < 0.0F);
    assert(bias.shadow_cast_lab[2] < 0.0F);
}

void test_render_plan_solver() {
    const std::filesystem::path repo_root = DFEE_REPO_ROOT;
    const auto stock = dfee::load_film_stock_profile(repo_root / "profiles" / "stocks" / "portra_400.yaml");
    const auto print_stock = dfee::load_print_stock_profile(repo_root / "profiles" / "print_stocks" / "kodak_2383.yaml");

    dfee::SolverInput input;
    input.tonal_distribution.tonal_skew = "highlight_stressed";
    input.tonal_distribution.dynamic_range_stops = 12.2F;
    input.tonal_distribution.midtone_anchor = 0.11F;
    input.tonal_distribution.highlight_headroom = 0.08F;
    input.tonal_distribution.shadow_depth = 0.01F;
    input.tonal_distribution.luma_p95 = 0.86F;
    input.hue_saturation_state.neon_risk = 0.07F;
    input.spatial_frequency.specular_point_ratio = 0.03F;
    input.spatial_frequency.large_highlight_area_ratio = 0.15F;
    input.clipping_ratios = {{"R", 0.04F}, {"G", 0.01F}, {"B", 0.0F}};
    input.camera_input_bias = dfee::CameraBiasAnalysis{
        .neutral_confidence = 0.2F,
        .global_cast_lab = {0.5F, 0.0F, 0.0F},
        .shadow_cast_lab = {0.3F, -0.01F, -0.04F},
        .midtone_cast_lab = {0.5F, 0.02F, -0.03F},
        .highlight_cast_lab = {0.8F, 0.01F, 0.01F},
        .blue_excess_index = 0.04F,
        .green_magenta_bias = 0.02F,
        .warm_cool_bias = -0.03F,
    };
    input.raw_iso = 1600;

    dfee::SolverControls controls;
    controls.adaptation_strength = 1.0F;
    controls.color_cast_handling = "Auto";
    controls.grain_amount = "Auto";
    controls.halation_amount = "High";
    controls.film_color = 110.0F;
    controls.print_strength = 0.9F;
    controls.print_c = 0.02F;
    controls.print_m = -0.01F;
    controls.print_y = 0.03F;
    controls.print_contrast = 0.1F;
    controls.print_black_point = -0.02F;

    const dfee::RenderPlanSolver solver;
    const auto plan = solver.solve(input, stock, controls, &print_stock);

    assert(plan.stock_type == "color_negative");
    assert(plan.input_diagnosis.tonal_state == "highlight_stressed");
    assert(plan.input_diagnosis.shadow_cast == "normal");
    assert(!plan.warnings.empty());
    assert(std::ranges::find(plan.warnings, "HIGH_CHANNEL_CLIPPING") != plan.warnings.end());
    assert(std::ranges::find(plan.warnings, "SHADOW_NOISE_RISK") != plan.warnings.end());
    assert(std::ranges::find(plan.warnings, "NEON_CHROMA_RISK") != plan.warnings.end());
    assert(std::ranges::find(plan.warnings, "LOW_NEUTRAL_CONFIDENCE") != plan.warnings.end());
    assert(std::ranges::find(plan.warnings, "DIFFUSE_HIGHLIGHT_SUPPRESSION") != plan.warnings.end());

    assert(plan.pre_film_normalization.exposure_compensation_stops > 0.0F);
    assert(plan.pre_film_normalization.highlight_channel_recovery >= 0.1F);
    assert(plan.pre_film_normalization.shadow_blue_normalization > 0.0F);

    assert(plan.film_response.toe_length > 0.0F);
    assert(plan.film_response.shoulder_strength > 0.0F);
    assert(plan.film_response.highlight_desaturation >= 0.6F);
    assert(plan.film_response.channel_toe_mult[0] > 0.0F);
    assert(plan.film_response.chroma_coupling.contains("hi_rolloff_start"));
    assert(plan.film_response.dye_contamination.contains("r_to_g"));
    assert(plan.film_response.film_color == 110.0F);

    assert(plan.material_effects.grain_strength > 0.0F);
    assert(plan.material_effects.grain_size > 0.45F);
    assert(plan.material_effects.halation_strength > 0.0F);
    assert(plan.material_effects.bloom_strength > 0.0F);
    assert(plan.material_effects.edge_softening > 0.0F);

    assert(plan.print_finish.has_value());
    assert(plan.print_finish->contrast_boost > 1.0F);
    assert(plan.print_finish->saturation_scale > 1.0F);
    assert(plan.print_finish->grain_size > 0.0F);
}

void test_pre_film_normalization() {
    dfee::Image rgb(4, 4, 3);
    for (int y = 0; y < rgb.height; ++y) {
        for (int x = 0; x < rgb.width; ++x) {
            rgb.at(x, y, 0) = 0.18F + 0.02F * static_cast<float>(x);
            rgb.at(x, y, 1) = 0.16F + 0.01F * static_cast<float>(y);
            rgb.at(x, y, 2) = 0.12F;
        }
    }
    rgb.at(3, 3, 0) = 0.96F;
    rgb.at(3, 3, 1) = 0.90F;
    rgb.at(3, 3, 2) = 0.82F;

    const auto luminance = dfee::compute_luminance(rgb);
    const dfee::ImageStateAnalyzer analyzer;
    const auto zones = analyzer.generate_zone_masks(luminance, 0.18F);

    dfee::PreFilmNormalization pre_film;
    pre_film.exposure_compensation_stops = 0.5F;
    pre_film.shadow_blue_normalization = 0.035F;
    pre_film.green_magenta_stabilization = 0.02F;

    const dfee::FilmRenderer renderer;
    const auto normalized = renderer.apply_pre_film_normalization(rgb, zones, pre_film);

    const float src_mid_luma = 0.2126F * rgb.at(1, 1, 0) + 0.7152F * rgb.at(1, 1, 1) + 0.0722F * rgb.at(1, 1, 2);
    const float dst_mid_luma = 0.2126F * normalized.at(1, 1, 0) + 0.7152F * normalized.at(1, 1, 1) + 0.0722F * normalized.at(1, 1, 2);
    assert(dst_mid_luma > src_mid_luma);

    const float src_high_spread = std::max({rgb.at(3, 3, 0), rgb.at(3, 3, 1), rgb.at(3, 3, 2)}) -
        std::min({rgb.at(3, 3, 0), rgb.at(3, 3, 1), rgb.at(3, 3, 2)});
    const float dst_high_spread = std::max({normalized.at(3, 3, 0), normalized.at(3, 3, 1), normalized.at(3, 3, 2)}) -
        std::min({normalized.at(3, 3, 0), normalized.at(3, 3, 1), normalized.at(3, 3, 2)});
    assert(dst_high_spread < src_high_spread);

    const auto src_oklab = dfee::rgb_to_oklab(rgb);
    const auto dst_oklab = dfee::rgb_to_oklab(normalized);
    assert(dst_oklab.at(0, 0, 2) > src_oklab.at(0, 0, 2));
}

void test_panchromatic_conversion() {
    dfee::Image rgb(2, 1, 3);
    rgb.pixels = {
        0.8F, 0.2F, 0.1F,
        0.1F, 0.8F, 0.2F,
    };

    dfee::FilmResponsePlan response;
    response.pan_weight_r = 0.25F;
    response.pan_weight_g = 0.55F;
    response.pan_weight_b = 0.20F;

    const dfee::FilmRenderer renderer;
    const auto mono = renderer.apply_panchromatic_conversion(rgb, response);

    require_close(mono.at(0, 0, 0), mono.at(0, 0, 1), 1.0e-6F);
    require_close(mono.at(0, 0, 1), mono.at(0, 0, 2), 1.0e-6F);
    require_close(mono.at(1, 0, 0), mono.at(1, 0, 1), 1.0e-6F);
    require_close(mono.at(1, 0, 1), mono.at(1, 0, 2), 1.0e-6F);
    assert(mono.at(1, 0, 0) > mono.at(0, 0, 0));
}

void test_film_tone_response() {
    dfee::Image rgb(4, 1, 3);
    rgb.pixels = {
        0.02F, 0.02F, 0.02F,
        0.18F, 0.18F, 0.18F,
        0.55F, 0.55F, 0.55F,
        1.35F, 1.35F, 1.35F,
    };

    dfee::FilmResponsePlan response;
    response.toe_strength = 0.46F;
    response.toe_length = 0.30F;
    response.midtone_density = 1.08F;
    response.shoulder_strength = 0.78F;
    response.black_density_floor = 0.01F;
    response.channel_toe_mult = {1.0F, 1.0F, 1.0F};
    response.channel_shoulder_mult = {1.0F, 1.0F, 1.0F};
    response.channel_midtone_mult = {1.0F, 1.0F, 1.0F};

    const dfee::FilmRenderer renderer;
    const auto toned = renderer.apply_film_tone_response(rgb, response);

    assert(toned.at(0, 0, 0) >= response.black_density_floor);
    assert(toned.at(1, 0, 0) > toned.at(0, 0, 0));
    assert(toned.at(2, 0, 0) > toned.at(1, 0, 0));
    assert(toned.at(3, 0, 0) <= 1.0F);
    assert(toned.at(3, 0, 0) > toned.at(1, 0, 0));
    assert(toned.at(3, 0, 0) < toned.at(2, 0, 0));
}

void test_color_response() {
    const std::filesystem::path repo_root = DFEE_REPO_ROOT;
    const auto stock = dfee::load_film_stock_profile(repo_root / "profiles" / "stocks" / "portra_400.yaml");

    dfee::Image rgb(3, 1, 3);
    rgb.pixels = {
        0.65F, 0.25F, 0.18F,
        0.60F, 0.50F, 0.18F,
        0.62F, 0.66F, 0.92F,
    };
    const auto luminance = dfee::compute_luminance(rgb);
    const dfee::ImageStateAnalyzer analyzer;
    const auto zones = analyzer.generate_zone_masks(luminance, 0.18F);

    dfee::FilmResponsePlan response;
    response.chroma_boost = static_cast<float>(stock.numeric_values.at("hue_saturation_response.saturation_boost"));
    response.red_orange_compression = static_cast<float>(stock.numeric_values.at("hue_saturation_response.red_orange_midtone_compression"));
    response.blue_cyan_compression = static_cast<float>(stock.numeric_values.at("hue_saturation_response.cyan_blue_highlight_compression"));
    response.neon_compression = static_cast<float>(stock.numeric_values.at("hue_saturation_response.neon_compression"));
    response.highlight_desaturation = static_cast<float>(stock.numeric_values.at("hue_saturation_response.highlight_desaturation"));
    response.shadow_bias_lab = {
        static_cast<float>(stock.numeric_arrays.at("color_response.shadow_bias_lab")[0]),
        static_cast<float>(stock.numeric_arrays.at("color_response.shadow_bias_lab")[1]),
        static_cast<float>(stock.numeric_arrays.at("color_response.shadow_bias_lab")[2]),
    };
    response.midtone_bias_lab = {
        static_cast<float>(stock.numeric_arrays.at("color_response.midtone_bias_lab")[0]),
        static_cast<float>(stock.numeric_arrays.at("color_response.midtone_bias_lab")[1]),
        static_cast<float>(stock.numeric_arrays.at("color_response.midtone_bias_lab")[2]),
    };
    response.highlight_bias_lab = {
        static_cast<float>(stock.numeric_arrays.at("color_response.highlight_bias_lab")[0]),
        static_cast<float>(stock.numeric_arrays.at("color_response.highlight_bias_lab")[1]),
        static_cast<float>(stock.numeric_arrays.at("color_response.highlight_bias_lab")[2]),
    };
    response.film_color = 100.0F;

    const dfee::FilmRenderer renderer;
    const auto adjusted = renderer.apply_color_response(rgb, zones, response);
    const auto src_oklab = dfee::rgb_to_oklab(rgb);
    const auto dst_oklab = dfee::rgb_to_oklab(adjusted);

    assert(dst_oklab.at(0, 0, 1) > src_oklab.at(0, 0, 1));
    assert(dst_oklab.at(0, 0, 2) > src_oklab.at(0, 0, 2));
    assert(std::fabs(dst_oklab.at(2, 0, 2) - src_oklab.at(2, 0, 2)) > 1.0e-4F);
}

void test_luminance_chroma_coupling() {
    dfee::Image rgb(3, 1, 3);
    rgb.pixels = {
        0.12F, 0.07F, 0.03F,
        0.82F, 0.70F, 0.42F,
        0.92F, 0.78F, 0.70F,
    };

    dfee::FilmResponsePlan response;
    response.stock_type = "color_negative";
    response.film_color = 100.0F;
    response.chroma_coupling = {
        {"hi_rolloff_start", 0.75F},
        {"hi_rolloff_rate", 1.5F},
        {"hi_compression", 0.48F},
        {"sh_rolloff_start", 0.18F},
        {"sh_compression", 0.43F},
        {"hi_hue_conv_rad", 0.30F},
        {"hi_hue_conv_str", 0.20F},
    };

    const dfee::FilmRenderer renderer;
    const auto adjusted = renderer.apply_luminance_chroma_coupling(rgb, response);
    const auto src_lch = dfee::oklab_to_oklch(dfee::rgb_to_oklab(rgb));
    const auto dst_lch = dfee::oklab_to_oklch(dfee::rgb_to_oklab(adjusted));

    assert(dst_lch.at(0, 0, 1) < src_lch.at(0, 0, 1));
    assert(dst_lch.at(2, 0, 1) < src_lch.at(2, 0, 1));
    assert(std::fabs(dst_lch.at(2, 0, 2) - src_lch.at(2, 0, 2)) > 1.0e-4F);
}

void test_acutance_shaping() {
    dfee::Image rgb(8, 8, 3);
    for (int y = 0; y < rgb.height; ++y) {
        for (int x = 0; x < rgb.width; ++x) {
            const float base = x < 4 ? 0.25F : 0.65F;
            const float lift = (x >= 2 && x < 6 && y >= 2 && y < 6) ? 0.03F : 0.0F;
            for (int channel = 0; channel < 3; ++channel) {
                rgb.at(x, y, channel) = base + lift;
            }
        }
    }

    dfee::MaterialEffectsPlan effects;
    effects.edge_softening = 0.12F;
    effects.sharpness = 0.55F;
    effects.sharpness_mask = 0.5F;

    const dfee::FilmRenderer renderer;
    const auto adjusted = renderer.apply_acutance_shaping(rgb, effects);
    const auto src_l = dfee::rgb_to_oklab(rgb);
    const auto dst_l = dfee::rgb_to_oklab(adjusted);

    assert(std::fabs(dst_l.at(4, 4, 0) - src_l.at(4, 4, 0)) > 1.0e-4F);
    assert(std::fabs((dst_l.at(4, 4, 0) - dst_l.at(3, 4, 0)) - (src_l.at(4, 4, 0) - src_l.at(3, 4, 0))) > 1.0e-4F);
}

void test_clarity() {
    dfee::Image rgb(32, 32, 3);
    for (int y = 0; y < rgb.height; ++y) {
        for (int x = 0; x < rgb.width; ++x) {
            float value = 0.45F;
            if (x >= 16) {
                value += 0.06F;
            }
            if (x >= 8 && x < 24 && y >= 8 && y < 24) {
                value += 0.03F;
            }
            for (int channel = 0; channel < 3; ++channel) {
                rgb.at(x, y, channel) = value;
            }
        }
    }

    const dfee::FilmRenderer renderer;
    const auto adjusted = renderer.apply_clarity(rgb, 40.0F);
    assert(channel_stddev_gamma(adjusted, 0) > channel_stddev_gamma(rgb, 0));
}

void test_texture() {
    dfee::Image rgb(32, 32, 3);
    for (int y = 0; y < rgb.height; ++y) {
        for (int x = 0; x < rgb.width; ++x) {
            const float value = 0.45F + (((x + y) % 2) == 0 ? 0.0F : 0.04F);
            for (int channel = 0; channel < 3; ++channel) {
                rgb.at(x, y, channel) = value;
            }
        }
    }

    const dfee::FilmRenderer renderer;
    const auto adjusted = renderer.apply_texture(rgb, 45.0F);
    assert(channel_stddev_gamma(adjusted, 0) > channel_stddev_gamma(rgb, 0));
}

void test_dehaze() {
    dfee::Image rgb(32, 32, 3);
    for (int y = 0; y < rgb.height; ++y) {
        for (int x = 0; x < rgb.width; ++x) {
            const float base = 0.15F + 0.55F * static_cast<float>(x) / 31.0F;
            const float value = base * (1.0F - 0.18F) + 0.18F * 0.9F;
            rgb.at(x, y, 0) = value;
            rgb.at(x, y, 1) = value;
            rgb.at(x, y, 2) = value;
        }
    }
    for (int y = 10; y < 22; ++y) {
        for (int x = 10; x < 22; ++x) {
            rgb.at(x, y, 0) *= 0.55F;
            rgb.at(x, y, 1) *= 0.60F;
            rgb.at(x, y, 2) *= 0.70F;
        }
    }

    const dfee::FilmRenderer renderer;
    const auto adjusted = renderer.apply_dehaze(rgb, 45.0F);
    assert(gamma_luminance_stddev(adjusted) > gamma_luminance_stddev(rgb));
}

void test_profile_loading() {
    const std::filesystem::path repo_root = DFEE_REPO_ROOT;
    const auto stock = dfee::load_film_stock_profile(repo_root / "profiles" / "stocks" / "astia_100.yaml");
    const auto print = dfee::load_print_stock_profile(repo_root / "profiles" / "print_stocks" / "kodak_2383.yaml");
    assert(!stock.stock_id.empty());
    assert(!stock.stock_name.empty());
    assert(!print.print_stock_id.empty());
    assert(!print.print_stock_name.empty());

    dfee::EngineSession session(repo_root);
    const auto listing = session.list_profiles();
    assert(!listing.stocks.empty());
    assert(!listing.print_stocks.empty());
    assert(listing.engine.engine_version == dfee::kEngineVersion);
    assert(!listing.engine.timings.empty());
    assert(listing.engine.metadata_json.find("list_profiles_total") != std::string::npos);

    const auto invalid_stock_path = repo_root / "profiles" / "stocks" / "native_invalid_stock.yaml";
    const auto invalid_print_path = repo_root / "profiles" / "print_stocks" / "native_invalid_print.yaml";
    struct CleanupGuard {
        std::filesystem::path stock_path;
        std::filesystem::path print_path;
        ~CleanupGuard() {
            std::filesystem::remove(stock_path);
            std::filesystem::remove(print_path);
        }
    } cleanup_guard{invalid_stock_path, invalid_print_path};

    {
        std::ofstream out(invalid_stock_path, std::ios::binary);
        out << "stock_id: native_invalid_stock\n";
        out << "stock_name: Native Invalid Stock\n";
        out << "stock_type: invalid_kind\n";
        out << "adaptation: 1\n";
    }
    {
        std::ofstream out(invalid_print_path, std::ios::binary);
        out << "print_stock_id: native_invalid_print\n";
        out << "print_stock_name: Native Invalid Print\n";
        out << "tone: [1, 2, 3]\n";
    }

    bool invalid_stock_threw = false;
    try {
        (void)dfee::load_film_stock_profile(invalid_stock_path);
    } catch (const std::exception&) {
        invalid_stock_threw = true;
    }
    assert(invalid_stock_threw);

    bool invalid_print_threw = false;
    try {
        (void)dfee::load_print_stock_profile(invalid_print_path);
    } catch (const std::exception&) {
        invalid_print_threw = true;
    }
    assert(invalid_print_threw);

    const auto listed_stocks = dfee::list_film_stock_profiles(repo_root / "profiles" / "stocks");
    for (const auto& listed_stock : listed_stocks) {
        assert(listed_stock.stock_id != "native_invalid_stock");
    }
    const auto listed_print_stocks = dfee::list_print_stock_profiles(repo_root / "profiles" / "print_stocks");
    for (const auto& listed_print : listed_print_stocks) {
        assert(listed_print.print_stock_id != "native_invalid_print");
    }

    const auto selected = session.select_file({.filename = "DSC00246.ARW"});
    assert(selected.ok);
    assert(!selected.engine.timings.empty());
    assert(selected.engine.metadata_json.find("select_file_total") != std::string::npos);
    const auto initial_cache = session.cache_state();
    assert(initial_cache.ok);
    assert(initial_cache.cache.selected_filename == "DSC00246.ARW");
    assert(!initial_cache.cache.draft_decode_cached);
    assert(!initial_cache.cache.preview_cached);
    assert(!initial_cache.cache.full_decode_cached);

    const auto metadata = session.read_raw_metadata({.filename = "DSC00246.ARW"});
#if DFEE_HAS_LIBRAW
    assert(metadata.ok);
    assert(metadata.metadata.image_width > 0);
    assert(metadata.metadata.raw_width > 0);
    assert(!metadata.metadata.metadata_json.empty());

    const auto draft_decode = session.decode_raw({.filename = "DSC00246.ARW", .draft_mode = true});
    assert(draft_decode.ok);
    assert(draft_decode.metadata.image_width == metadata.metadata.image_width);
    assert(draft_decode.metadata.image_height == metadata.metadata.image_height);
    const auto draft_cache = session.cache_state();
    assert(draft_cache.cache.draft_decode_cached);
    assert(draft_cache.cache.preview_cached);
    assert(!draft_cache.cache.raw_preview_jpeg_cached);
    assert(draft_cache.cache.draft_width == draft_decode.summary.image_width);
    assert(draft_cache.cache.draft_height == draft_decode.summary.image_height);
    assert(draft_cache.cache.preview_width <= draft_cache.cache.draft_width);
    assert(draft_cache.cache.preview_height <= draft_cache.cache.draft_height);

    const auto cached_draft = session.decode_raw({.filename = "DSC00246.ARW", .draft_mode = true});
    assert(cached_draft.ok);
    assert(cached_draft.status == "cached");

    const auto full_decode = session.decode_raw({.filename = "DSC00246.ARW", .draft_mode = false});
    assert(full_decode.ok);
    const auto raw_preview = session.raw_preview({.filename = "DSC00246.ARW", .max_edge = 1024});
    assert(raw_preview.ok);
    assert(raw_preview.content_type == "image/jpeg");
    assert(!raw_preview.jpeg_bytes.empty());
    const auto full_cache = session.cache_state();
    assert(full_cache.cache.raw_preview_jpeg_cached);
    assert(full_cache.cache.raw_preview_jpeg_bytes == raw_preview.jpeg_bytes.size());
    assert(full_cache.cache.full_decode_cached);
    assert(full_cache.cache.full_width == full_decode.summary.image_width);
    assert(full_cache.cache.full_height == full_decode.summary.image_height);

    const auto cached_raw_preview = session.raw_preview({.filename = "DSC00246.ARW", .max_edge = 1024});
    assert(cached_raw_preview.ok);
    assert(cached_raw_preview.status == "cached");
    assert(cached_raw_preview.jpeg_bytes == raw_preview.jpeg_bytes);
#else
    assert(!metadata.ok);
    assert(metadata.error.code == "LIBRAW_UNAVAILABLE");
    const auto failed_decode = session.decode_raw({.filename = "DSC00246.ARW", .draft_mode = true});
    assert(!failed_decode.ok);
    assert(failed_decode.error.code == "LIBRAW_UNAVAILABLE");
#endif
}

void test_raw_failure_paths() {
    const std::filesystem::path repo_root = DFEE_REPO_ROOT;
    const std::filesystem::path raw_dir = repo_root / "raw_files";
    dfee::EngineSession session(repo_root);

    const auto unsupported_path = raw_dir / "native_core_test_unsupported.arw";
    const auto corrupt_path = raw_dir / "native_core_test_corrupt.arw";
    const auto cleanup = [&]() {
        std::filesystem::remove(unsupported_path);
        std::filesystem::remove(corrupt_path);
    };
    struct CleanupGuard {
        decltype(cleanup)& cleanup_fn;
        ~CleanupGuard() { cleanup_fn(); }
    } cleanup_guard{cleanup};

    {
        std::ofstream out(unsupported_path, std::ios::binary);
        out << "not a real raw file\n";
    }

    const auto source_raw = raw_dir / "DSC00246.ARW";
    {
        std::ifstream in(source_raw, std::ios::binary);
        std::ofstream out(corrupt_path, std::ios::binary);
        std::string buffer(4096, '\0');
        in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        out.write(buffer.data(), in.gcount());
    }

#if DFEE_HAS_LIBRAW
    const auto unsupported_metadata = session.read_raw_metadata({.filename = unsupported_path.filename().string()});
    assert(!unsupported_metadata.ok);
    assert(unsupported_metadata.error.code == "LIBRAW_UNSUPPORTED_RAW" || unsupported_metadata.error.code == "LIBRAW_OPEN_FAILED");

    const auto unsupported_decode = session.decode_raw({.filename = unsupported_path.filename().string(), .draft_mode = true});
    assert(!unsupported_decode.ok);
    assert(unsupported_decode.error.code == "LIBRAW_UNSUPPORTED_RAW" || unsupported_decode.error.code == "LIBRAW_OPEN_FAILED");

    const auto corrupt_decode = session.decode_raw({.filename = corrupt_path.filename().string(), .draft_mode = true});
    assert(!corrupt_decode.ok);
    assert(corrupt_decode.error.code == "LIBRAW_CORRUPT_RAW" || corrupt_decode.error.code == "LIBRAW_OPEN_FAILED");

    const auto corrupt_preview = session.raw_preview({.filename = corrupt_path.filename().string(), .max_edge = 1024});
    assert(!corrupt_preview.ok);
    assert(corrupt_preview.error.code == "RAW_PREVIEW_NOT_CACHED");
#else
    const auto unsupported_decode = session.decode_raw({.filename = unsupported_path.filename().string(), .draft_mode = true});
    assert(!unsupported_decode.ok);
    assert(unsupported_decode.error.code == "LIBRAW_UNAVAILABLE");
#endif

}

}  // namespace

int main() {
    try {
        test_oklab_roundtrip();
        test_zone_partition();
        test_tonal_analysis();
        test_color_analysis();
        test_spatial_analysis();
        test_camera_bias_estimator();
        test_render_plan_solver();
        test_pre_film_normalization();
        test_panchromatic_conversion();
        test_film_tone_response();
        test_color_response();
        test_luminance_chroma_coupling();
        test_acutance_shaping();
        test_clarity();
        test_texture();
        test_dehaze();
        test_profile_loading();
        test_raw_failure_paths();
        std::cout << "dfee_tests passed\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "dfee_tests exception: " << ex.what() << "\n";
        return 1;
    }
}
