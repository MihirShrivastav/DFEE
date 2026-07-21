#include "dfee/analyzer.hpp"
#include "dfee/bias.hpp"
#include "dfee/color_spaces.hpp"
#include "dfee/image.hpp"
#include "dfee/profile.hpp"
#include "dfee/renderer.hpp"
#include "dfee/session.hpp"
#include "dfee/solver.hpp"
#include "dfee/tone_controls.hpp"
#include "dfee/version.hpp"

#include <array>
#include <cassert>
#include <cmath>
#if defined(_WIN32) && defined(_DEBUG)
#include <cstdlib>
#include <crtdbg.h>
#endif
#include <filesystem>
#include <fstream>
#include <iostream>
#include <numbers>
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

    dfee::SolverControls auto_balanced_controls = controls;
    auto_balanced_controls.exposure_intent = "Auto";
    const auto auto_balanced_plan = solver.solve(input, stock, auto_balanced_controls, &print_stock);
    assert(auto_balanced_plan.pre_film_normalization.exposure_compensation_stops >
        plan.pre_film_normalization.exposure_compensation_stops);
}

void test_render_plan_solver_rich_grain_profile_fields() {
    dfee::FilmStockProfile stock;
    stock.stock_id = "synthetic_grain_profile";
    stock.stock_name = "Synthetic Grain Profile";
    stock.stock_type = dfee::StockType::ColorNegative;
    stock.numeric_values = {
        {"adaptation.base_iso", 400.0},
        {"tone_response.toe_strength", 0.35},
        {"tone_response.toe_length", 0.25},
        {"tone_response.shoulder_strength", 0.5},
        {"tone_response.highlight_rolloff_start", 0.72},
        {"tone_response.black_density_floor", 0.02},
        {"hue_saturation_response.saturation_boost", 1.0},
        {"hue_saturation_response.red_orange_midtone_compression", 0.2},
        {"hue_saturation_response.cyan_blue_highlight_compression", 0.2},
        {"hue_saturation_response.neon_compression", 0.5},
        {"hue_saturation_response.highlight_desaturation", 0.4},
        {"grain.size", 0.48},
        {"grain.strength", 0.52},
        {"grain.roughness", 0.44},
        {"grain.chroma_strength", 0.18},
        {"grain.target_pgi_35mm_4x6", 58.0},
        {"grain.clumpiness", 0.71},
        {"grain.micro_grit", 0.37},
        {"grain.layer_correlation", 0.51},
        {"grain.shadow_response", 1.08},
        {"grain.midtone_response", 1.16},
        {"grain.highlight_response", 0.29},
        {"grain.underexposure_coarsening", 0.49},
        {"grain.overexposure_smoothing", 0.21},
        {"halation.strength", 0.2},
    };
    stock.string_values = {
        {"grain.family", "modern_color_negative_high_speed"},
    };

    dfee::SolverInput input;
    input.tonal_distribution.tonal_skew = "normal";
    input.tonal_distribution.dynamic_range_stops = 10.0F;
    input.tonal_distribution.midtone_anchor = 0.35F;
    input.tonal_distribution.highlight_headroom = 0.3F;
    input.tonal_distribution.shadow_depth = 0.08F;
    input.tonal_distribution.luma_p95 = 0.78F;
    input.camera_input_bias = dfee::CameraBiasAnalysis{
        .neutral_confidence = 0.95F,
    };
    input.raw_iso = 400;

    dfee::SolverControls controls;
    controls.grain_amount = "Auto";

    const dfee::RenderPlanSolver solver;
    const auto plan = solver.solve(input, stock, controls);

    assert(plan.material_effects.grain_family == "modern_color_negative_high_speed");
    assert(std::fabs(plan.material_effects.grain_target_pgi - 58.0F) < 1.0e-4F);
    assert(std::fabs(plan.material_effects.grain_clumpiness - 0.71F) < 1.0e-4F);
    assert(std::fabs(plan.material_effects.grain_micro_grit - 0.37F) < 1.0e-4F);
    assert(std::fabs(plan.material_effects.grain_layer_correlation - 0.51F) < 1.0e-4F);
    assert(std::fabs(plan.material_effects.grain_shadow_response - 1.08F) < 1.0e-4F);
    assert(std::fabs(plan.material_effects.grain_midtone_response - 1.16F) < 1.0e-4F);
    assert(std::fabs(plan.material_effects.grain_highlight_response - 0.29F) < 1.0e-4F);
    assert(std::fabs(plan.material_effects.grain_underexposure_coarsening - 0.49F) < 1.0e-4F);
    assert(std::fabs(plan.material_effects.grain_overexposure_smoothing - 0.21F) < 1.0e-4F);
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

void test_yellow_green_muting() {
    dfee::Image rgb(1, 1, 3);
    rgb.pixels = {0.25F, 0.72F, 0.08F};
    const auto luminance = dfee::compute_luminance(rgb);
    const dfee::ImageStateAnalyzer analyzer;
    const auto zones = analyzer.generate_zone_masks(luminance, 0.18F);

    dfee::FilmResponsePlan baseline;
    baseline.film_color = 100.0F;
    dfee::FilmResponsePlan muted = baseline;
    muted.yellow_green_muting = 0.70F;

    const dfee::FilmRenderer renderer;
    const auto baseline_result = renderer.apply_color_response(rgb, zones, baseline);
    const auto muted_result = renderer.apply_color_response(rgb, zones, muted);
    const auto baseline_oklab = dfee::rgb_to_oklab(baseline_result);
    const auto muted_oklab = dfee::rgb_to_oklab(muted_result);
    const float baseline_chroma = std::hypot(baseline_oklab.at(0, 0, 1), baseline_oklab.at(0, 0, 2));
    const float muted_chroma = std::hypot(muted_oklab.at(0, 0, 1), muted_oklab.at(0, 0, 2));
    assert(muted_chroma < baseline_chroma);
}

void test_filmic_grain_profile_placement_and_texture_masking() {
    dfee::Image rgb(48, 16, 3);
    for (int y = 0; y < rgb.height; ++y) {
        for (int x = 0; x < rgb.width; ++x) {
            const float value = x < 16 ? 0.10F : (x < 32 ? 0.38F : 0.72F);
            rgb.at(x, y, 0) = value;
            rgb.at(x, y, 1) = value;
            rgb.at(x, y, 2) = value;
        }
    }

    dfee::SpatialMasks masks;
    masks.grain_receptivity_mask = dfee::LuminanceImage(rgb.width, rgb.height);
    for (int y = 0; y < rgb.height; ++y) {
        for (int x = 0; x < rgb.width; ++x) {
            masks.grain_receptivity_mask.at(x, y) = x < 16 ? 0.0F : 1.0F;
        }
    }

    dfee::MaterialEffectsPlan effects;
    effects.grain_strength = 0.75F;
    effects.grain_size = 0.60F;
    effects.grain_chroma_strength = 0.0F;
    effects.grain_seed = 8080U;
    effects.grain_peak_zone = "midtones_heavy";
    effects.grain_texture_masking = 1.0F;

    const dfee::FilmRenderer renderer;
    const auto grained = renderer.apply_filmic_grain(rgb, masks, effects);

    const auto mean_delta = [&](const dfee::Image& adjusted, const int begin_x, const int end_x) {
        double total = 0.0;
        std::size_t count = 0U;
        for (int y = 0; y < rgb.height; ++y) {
            for (int x = begin_x; x < end_x; ++x) {
                total += std::fabs(adjusted.at(x, y, 0) - rgb.at(x, y, 0));
                ++count;
            }
        }
        return total / static_cast<double>(count);
    };

    // Anti-blotch invariant: grain is spatially UNIFORM and is NOT gated by the
    // receptivity mask (the x<16 region has receptivity 0 but still receives grain).
    // Uniformity across receptivity is verified in depth by test_filmic_grain_uniform_softlight.
    assert(mean_delta(grained, 0, 16) > 1.0e-3);
    // Tonal placement: the midtone band (0.38) is grainier than the highlight band (0.72).
    assert(mean_delta(grained, 16, 32) > mean_delta(grained, 32, 48));
}

void test_filmic_halation_profile_geometry_and_colour() {
    // Halation blur radii are resolution-scaled (relative to a 2048px reference), so the
    // glow only reaches a meaningful distance on a production-scale image. Use a bright
    // disc on a larger field and sample the glow ring just outside the disc edge.
    const int w = 160, h = 160;
    const int cx = 80, cy = 80, rad = 12;
    dfee::Image rgb(w, h, 3);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const int dx = x - cx, dy = y - cy;
            const bool inside = dx * dx + dy * dy <= rad * rad;
            rgb.at(x, y, 0) = inside ? 1.0F : 0.02F;
            rgb.at(x, y, 1) = inside ? 0.95F : 0.02F;
            rgb.at(x, y, 2) = inside ? 0.88F : 0.02F;
        }
    }

    dfee::LuminanceImage luminance = dfee::compute_luminance(rgb);
    const dfee::ImageStateAnalyzer analyzer;
    const auto zones = analyzer.generate_zone_masks(luminance, 0.18F);
    dfee::SpatialMasks masks;
    masks.halation_source_mask = dfee::LuminanceImage(w, h);
    masks.halation_receiver_mask = dfee::LuminanceImage(w, h);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const int dx = x - cx, dy = y - cy;
            if (dx * dx + dy * dy <= rad * rad) {
                masks.halation_source_mask.at(x, y) = 1.0F;
            }
        }
    }
    for (float& value : masks.halation_receiver_mask.values) {
        value = 1.0F;
    }

    dfee::MaterialEffectsPlan effects;
    effects.halation_strength = 0.8F;
    effects.bloom_strength = 0.0F;
    effects.halation_trigger = "specular_only";
    effects.halation_radius_inner = 40.0F;
    effects.halation_radius_outer = 120.0F;
    effects.halation_warm_core = {1.0F, 0.45F, 0.10F};
    effects.halation_red_fringe = {1.0F, 0.05F, 0.00F};

    const dfee::FilmRenderer renderer;
    const auto warm = renderer.apply_filmic_halation_bloom(rgb, zones, masks, effects);
    effects.halation_warm_core = {0.0F, 0.0F, 1.0F};
    effects.halation_red_fringe = {0.0F, 0.0F, 1.0F};
    const auto blue = renderer.apply_filmic_halation_bloom(rgb, zones, masks, effects);

    // Average the glow over a thin ring just outside the disc (robust to blur profile).
    const auto ring_delta = [&](const dfee::Image& out, int ch) {
        double sum = 0.0;
        int count = 0;
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                const int dx = x - cx, dy = y - cy;
                const int d2 = dx * dx + dy * dy;
                if (d2 > (rad + 1) * (rad + 1) && d2 <= (rad + 8) * (rad + 8)) {
                    sum += static_cast<double>(out.at(x, y, ch)) - rgb.at(x, y, ch);
                    ++count;
                }
            }
        }
        return static_cast<float>(sum / std::max(1, count));
    };
    const float warm_red_delta = ring_delta(warm, 0);
    const float warm_blue_delta = ring_delta(warm, 2);
    const float blue_red_delta = ring_delta(blue, 0);
    const float blue_blue_delta = ring_delta(blue, 2);
    assert(warm_red_delta > 1.0e-4F);            // glow actually reaches the ring
    assert(warm_red_delta > warm_blue_delta);    // warm core tints red > blue
    assert(blue_blue_delta > blue_red_delta);    // blue core tints blue > red
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

void test_halation_bloom() {
    dfee::Image rgb(64, 64, 3);
    for (int y = 0; y < rgb.height; ++y) {
        for (int x = 0; x < rgb.width; ++x) {
            rgb.at(x, y, 0) = 0.08F;
            rgb.at(x, y, 1) = 0.08F;
            rgb.at(x, y, 2) = 0.08F;
        }
    }
    for (int y = 24; y < 40; ++y) {
        for (int x = 24; x < 40; ++x) {
            rgb.at(x, y, 0) = 0.92F;
            rgb.at(x, y, 1) = 0.88F;
            rgb.at(x, y, 2) = 0.82F;
        }
    }
    for (int y = 28; y < 36; ++y) {
        for (int x = 28; x < 36; ++x) {
            rgb.at(x, y, 0) = 1.0F;
            rgb.at(x, y, 1) = 0.98F;
            rgb.at(x, y, 2) = 0.92F;
        }
    }

    dfee::ZoneMasks zone_masks;
    for (auto& zone : zone_masks.zones) {
        zone = dfee::LuminanceImage(rgb.width, rgb.height);
    }
    dfee::SpatialMasks spatial_masks;
    spatial_masks.halation_source_mask = dfee::LuminanceImage(rgb.width, rgb.height);
    spatial_masks.halation_receiver_mask = dfee::LuminanceImage(rgb.width, rgb.height);
    const auto luminance = dfee::compute_luminance(rgb);
    for (int y = 0; y < rgb.height; ++y) {
        for (int x = 0; x < rgb.width; ++x) {
            if (x >= 30 && x < 34 && y >= 30 && y < 34) {
                spatial_masks.halation_source_mask.at(x, y) = 1.0F;
            }
            if (x >= 20 && x < 44 && y >= 20 && y < 44) {
                spatial_masks.halation_receiver_mask.at(x, y) = 1.0F - luminance.at(x, y);
            }
            zone_masks.zones[5].at(x, y) = dfee::clamp01((luminance.at(x, y) - 0.6F) / 0.4F);
        }
    }

    dfee::MaterialEffectsPlan effects;
    effects.halation_strength = 0.35F;
    effects.bloom_strength = 0.22F;

    const dfee::FilmRenderer renderer;
    const auto adjusted = renderer.apply_halation_bloom(rgb, zone_masks, spatial_masks, effects);

    double mean_abs_delta = 0.0;
    float max_abs_delta = 0.0F;
    for (std::size_t i = 0; i < rgb.value_count(); ++i) {
        const float delta = std::fabs(adjusted.pixels[i] - rgb.pixels[i]);
        mean_abs_delta += delta;
        max_abs_delta = std::max(max_abs_delta, delta);
    }
    mean_abs_delta /= static_cast<double>(rgb.value_count());

    assert(mean_abs_delta > 1.0e-5);
    assert(max_abs_delta > 1.0e-3F);
    assert(adjusted.at(30, 30, 0) >= adjusted.at(30, 30, 2));
}

void test_filmic_halation_bloom_compresses_and_diffuses_highlights() {
    dfee::Image rgb(96, 96, 3);
    for (int y = 0; y < rgb.height; ++y) {
        for (int x = 0; x < rgb.width; ++x) {
            rgb.at(x, y, 0) = 0.09F;
            rgb.at(x, y, 1) = 0.09F;
            rgb.at(x, y, 2) = 0.09F;
        }
    }
    for (int y = 40; y < 56; ++y) {
        for (int x = 40; x < 56; ++x) {
            rgb.at(x, y, 0) = 0.96F;
            rgb.at(x, y, 1) = 0.94F;
            rgb.at(x, y, 2) = 0.88F;
        }
    }

    dfee::ZoneMasks zone_masks;
    for (auto& zone : zone_masks.zones) {
        zone = dfee::LuminanceImage(rgb.width, rgb.height);
    }
    dfee::SpatialMasks spatial_masks;
    spatial_masks.halation_source_mask = dfee::LuminanceImage(rgb.width, rgb.height);
    spatial_masks.halation_receiver_mask = dfee::LuminanceImage(rgb.width, rgb.height);
    const auto luminance = dfee::compute_luminance(rgb);
    for (int y = 0; y < rgb.height; ++y) {
        for (int x = 0; x < rgb.width; ++x) {
            if (x >= 44 && x < 52 && y >= 44 && y < 52) {
                spatial_masks.halation_source_mask.at(x, y) = 1.0F;
            }
            if (x >= 32 && x < 64 && y >= 32 && y < 64) {
                spatial_masks.halation_receiver_mask.at(x, y) = 1.0F - luminance.at(x, y);
            }
            zone_masks.zones[5].at(x, y) = dfee::clamp01((luminance.at(x, y) - 0.58F) / 0.34F);
        }
    }

    dfee::MaterialEffectsPlan effects;
    effects.halation_strength = 0.65F;
    effects.bloom_strength = 0.70F;

    const dfee::FilmRenderer renderer;
    const auto adjusted = renderer.apply_filmic_halation_bloom(rgb, zone_masks, spatial_masks, effects);

    const auto luma = [](const dfee::Image& image, const int x, const int y) {
        return 0.2126F * image.at(x, y, 0) + 0.7152F * image.at(x, y, 1) + 0.0722F * image.at(x, y, 2);
    };

    // At the bright source the highlight is compressed/diffused rather than painted
    // brighter-white: its luma must not gain meaningfully (bloom refill roughly cancels
    // the density shoulder). Tolerance absorbs sub-1% drift from shared-blur refactors.
    assert(luma(adjusted, 48, 48) <= luma(rgb, 48, 48) + 0.01F);
    assert(luma(adjusted, 35, 48) > luma(rgb, 35, 48));
    assert(adjusted.at(35, 48, 0) > adjusted.at(35, 48, 1));
    assert(adjusted.at(35, 48, 1) > adjusted.at(35, 48, 2));
    assert(luma(adjusted, 8, 8) < luma(rgb, 8, 8) + 1.0e-4F);
}

void test_film_grain_determinism() {
    dfee::Image rgb(32, 32, 3);
    for (int y = 0; y < rgb.height; ++y) {
        for (int x = 0; x < rgb.width; ++x) {
            rgb.at(x, y, 0) = 0.20F + 0.40F * static_cast<float>(x) / 31.0F;
            rgb.at(x, y, 1) = 0.18F + 0.45F * static_cast<float>(y) / 31.0F;
            rgb.at(x, y, 2) = 0.16F + 0.35F * static_cast<float>(x + y) / 62.0F;
        }
    }

    dfee::SpatialMasks spatial_masks;
    spatial_masks.grain_receptivity_mask = dfee::LuminanceImage(rgb.width, rgb.height);
    for (int y = 0; y < rgb.height; ++y) {
        for (int x = 0; x < rgb.width; ++x) {
            spatial_masks.grain_receptivity_mask.at(x, y) = 0.85F;
        }
    }

    dfee::MaterialEffectsPlan effects;
    effects.grain_strength = 0.65F;
    effects.grain_size = 0.55F;
    effects.grain_roughness = 0.45F;
    effects.grain_chroma_strength = 0.12F;

    const dfee::FilmRenderer renderer;
    const auto first = renderer.apply_film_grain(rgb, spatial_masks, effects);
    const auto second = renderer.apply_film_grain(rgb, spatial_masks, effects);

    assert(first.pixels == second.pixels);

    double mean_abs_delta = 0.0;
    for (std::size_t i = 0; i < rgb.value_count(); ++i) {
        mean_abs_delta += std::fabs(first.pixels[i] - rgb.pixels[i]);
    }
    mean_abs_delta /= static_cast<double>(rgb.value_count());
    assert(mean_abs_delta > 1.0e-5);

    bool channel_difference_found = false;
    for (std::size_t i = 0; i < first.pixel_count(); ++i) {
        const float rg = std::fabs(first.pixels[i * 3 + 0] - first.pixels[i * 3 + 1]);
        const float gb = std::fabs(first.pixels[i * 3 + 1] - first.pixels[i * 3 + 2]);
        if (rg > 1.0e-5F || gb > 1.0e-5F) {
            channel_difference_found = true;
            break;
        }
    }
    assert(channel_difference_found);
}

void test_filmic_grain_density_response_and_stock_character() {
    dfee::Image rgb(48, 48, 3);
    for (int y = 0; y < rgb.height; ++y) {
        for (int x = 0; x < rgb.width; ++x) {
            float value = 0.45F;
            if (x < 16) {
                value = 0.12F;
            } else if (x >= 32) {
                value = 0.88F;
            }
            rgb.at(x, y, 0) = value;
            rgb.at(x, y, 1) = value;
            rgb.at(x, y, 2) = value;
        }
    }

    dfee::SpatialMasks spatial_masks;
    spatial_masks.grain_receptivity_mask = dfee::LuminanceImage(rgb.width, rgb.height);
    for (int y = 0; y < rgb.height; ++y) {
        for (int x = 0; x < rgb.width; ++x) {
            spatial_masks.grain_receptivity_mask.at(x, y) = 1.0F;
        }
    }

    dfee::MaterialEffectsPlan effects;
    effects.grain_strength = 0.62F;
    effects.grain_size = 0.62F;
    effects.grain_roughness = 0.55F;
    effects.grain_chroma_strength = 0.12F;
    effects.grain_seed = 12345U;

    const dfee::FilmRenderer renderer;
    const auto first = renderer.apply_filmic_grain(rgb, spatial_masks, effects);
    const auto second = renderer.apply_filmic_grain(rgb, spatial_masks, effects);
    assert(first.pixels == second.pixels);

    const auto region_delta = [](const dfee::Image& source, const dfee::Image& adjusted, const int x0, const int x1) {
        double total = 0.0;
        std::size_t count = 0;
        for (int y = 0; y < source.height; ++y) {
            for (int x = x0; x < x1; ++x) {
                for (int c = 0; c < 3; ++c) {
                    total += std::fabs(adjusted.at(x, y, c) - source.at(x, y, c));
                    ++count;
                }
            }
        }
        return static_cast<float>(total / static_cast<double>(count));
    };

    const float shadow_delta = region_delta(rgb, first, 0, 16);
    const float mid_delta = region_delta(rgb, first, 16, 32);
    const float highlight_delta = region_delta(rgb, first, 32, 48);
    assert(mid_delta > highlight_delta * 1.7F);
    assert(shadow_delta > highlight_delta * 1.2F);

    dfee::MaterialEffectsPlan fine = effects;
    fine.grain_strength = 0.14F;
    fine.grain_size = 0.14F;
    fine.grain_roughness = 0.22F;
    fine.grain_chroma_strength = 0.01F;
    fine.grain_seed = 12345U;
    const auto fine_adjusted = renderer.apply_filmic_grain(rgb, spatial_masks, fine);
    assert(region_delta(rgb, first, 16, 32) > region_delta(rgb, fine_adjusted, 16, 32) * 2.0F);

    dfee::MaterialEffectsPlan mono = effects;
    mono.grain_chroma_strength = 0.0F;
    mono.grain_seed = 777U;
    const auto mono_adjusted = renderer.apply_filmic_grain(rgb, spatial_masks, mono);
    for (std::size_t i = 0; i < mono_adjusted.pixel_count(); ++i) {
        const float r_delta = mono_adjusted.pixels[i * 3 + 0] - rgb.pixels[i * 3 + 0];
        const float g_delta = mono_adjusted.pixels[i * 3 + 1] - rgb.pixels[i * 3 + 1];
        const float b_delta = mono_adjusted.pixels[i * 3 + 2] - rgb.pixels[i * 3 + 2];
        assert(std::fabs(r_delta - g_delta) < 3.0e-3F);
        assert(std::fabs(g_delta - b_delta) < 3.0e-3F);
    }
}

void test_filmic_grain_avoids_low_frequency_blotches() {
    dfee::Image rgb(96, 96, 3);
    for (int y = 0; y < rgb.height; ++y) {
        for (int x = 0; x < rgb.width; ++x) {
            rgb.at(x, y, 0) = 0.42F;
            rgb.at(x, y, 1) = 0.45F;
            rgb.at(x, y, 2) = 0.50F;
        }
    }

    dfee::SpatialMasks spatial_masks;
    spatial_masks.grain_receptivity_mask = dfee::LuminanceImage(rgb.width, rgb.height);
    for (int y = 0; y < rgb.height; ++y) {
        for (int x = 0; x < rgb.width; ++x) {
            spatial_masks.grain_receptivity_mask.at(x, y) = 1.0F;
        }
    }

    dfee::MaterialEffectsPlan effects;
    effects.grain_strength = 0.70F;
    effects.grain_size = 0.65F;
    effects.grain_roughness = 0.58F;
    effects.grain_chroma_strength = 0.14F;
    effects.grain_clumpiness = 0.75F;
    effects.grain_micro_grit = 0.34F;
    effects.grain_layer_correlation = 0.55F;
    effects.grain_seed = 24680U;

    const dfee::FilmRenderer renderer;
    const auto adjusted = renderer.apply_filmic_grain(rgb, spatial_masks, effects);

    double sum = 0.0;
    double sum_sq = 0.0;
    for (int y = 0; y < rgb.height; ++y) {
        for (int x = 0; x < rgb.width; ++x) {
            const double delta =
                0.2126 * static_cast<double>(adjusted.at(x, y, 0) - rgb.at(x, y, 0)) +
                0.7152 * static_cast<double>(adjusted.at(x, y, 1) - rgb.at(x, y, 1)) +
                0.0722 * static_cast<double>(adjusted.at(x, y, 2) - rgb.at(x, y, 2));
            sum += delta;
            sum_sq += delta * delta;
        }
    }
    const double count = static_cast<double>(rgb.width * rgb.height);
    const double pixel_std = std::sqrt(std::max(0.0, sum_sq / count - (sum / count) * (sum / count)));

    constexpr int kBlock = 12;
    double block_sum = 0.0;
    double block_sum_sq = 0.0;
    int block_count = 0;
    for (int by = 0; by < rgb.height; by += kBlock) {
        for (int bx = 0; bx < rgb.width; bx += kBlock) {
            double block_delta = 0.0;
            int samples = 0;
            for (int y = by; y < std::min(by + kBlock, rgb.height); ++y) {
                for (int x = bx; x < std::min(bx + kBlock, rgb.width); ++x) {
                    block_delta +=
                        0.2126 * static_cast<double>(adjusted.at(x, y, 0) - rgb.at(x, y, 0)) +
                        0.7152 * static_cast<double>(adjusted.at(x, y, 1) - rgb.at(x, y, 1)) +
                        0.0722 * static_cast<double>(adjusted.at(x, y, 2) - rgb.at(x, y, 2));
                    ++samples;
                }
            }
            const double block_mean = block_delta / static_cast<double>(samples);
            block_sum += block_mean;
            block_sum_sq += block_mean * block_mean;
            ++block_count;
        }
    }
    const double block_mean = block_sum / static_cast<double>(block_count);
    const double block_std = std::sqrt(std::max(0.0, block_sum_sq / static_cast<double>(block_count) - block_mean * block_mean));

    assert(pixel_std > 1.0e-4);
    assert(block_std < pixel_std * 0.22);
}

void test_filmic_grain_roughness_does_not_become_pixel_noise() {
    dfee::Image rgb(160, 120, 3);
    for (int y = 0; y < rgb.height; ++y) {
        for (int x = 0; x < rgb.width; ++x) {
            rgb.at(x, y, 0) = 0.34F;
            rgb.at(x, y, 1) = 0.38F;
            rgb.at(x, y, 2) = 0.42F;
        }
    }

    dfee::SpatialMasks spatial_masks;
    spatial_masks.grain_receptivity_mask = dfee::LuminanceImage(rgb.width, rgb.height);
    for (int y = 0; y < rgb.height; ++y) {
        for (int x = 0; x < rgb.width; ++x) {
            spatial_masks.grain_receptivity_mask.at(x, y) = 1.0F;
        }
    }

    dfee::MaterialEffectsPlan smooth;
    smooth.grain_strength = 0.68F;
    smooth.grain_size = 0.95F;
    smooth.grain_roughness = 0.0F;
    smooth.grain_chroma_strength = 0.08F;
    smooth.grain_clumpiness = 0.45F;
    smooth.grain_micro_grit = 0.02F;
    smooth.grain_layer_correlation = 0.72F;
    smooth.grain_seed = 112233U;

    dfee::MaterialEffectsPlan rough = smooth;
    rough.grain_roughness = 1.0F;

    const dfee::FilmRenderer renderer;
    const auto smooth_adjusted = renderer.apply_filmic_grain(rgb, spatial_masks, smooth);
    const auto rough_adjusted = renderer.apply_filmic_grain(rgb, spatial_masks, rough);

    const auto neighbor_energy = [](const dfee::Image& source, const dfee::Image& adjusted) {
        double total = 0.0;
        std::size_t count = 0;
        for (int y = 0; y < source.height; ++y) {
            double previous_delta = 0.0;
            for (int x = 0; x < source.width; ++x) {
                const double delta =
                    0.2126 * static_cast<double>(adjusted.at(x, y, 0) - source.at(x, y, 0)) +
                    0.7152 * static_cast<double>(adjusted.at(x, y, 1) - source.at(x, y, 1)) +
                    0.0722 * static_cast<double>(adjusted.at(x, y, 2) - source.at(x, y, 2));
                if (x > 0) {
                    total += std::fabs(delta - previous_delta);
                    ++count;
                }
                previous_delta = delta;
            }
        }
        return total / static_cast<double>(count);
    };

    const double smooth_neighbor_energy = neighbor_energy(rgb, smooth_adjusted);
    const double rough_neighbor_energy = neighbor_energy(rgb, rough_adjusted);
    assert(smooth_neighbor_energy > 1.0e-5);
    assert(rough_neighbor_energy < smooth_neighbor_energy * 1.45);
}

void test_print_finish() {
    dfee::Image rgb(8, 8, 3);
    for (int y = 0; y < rgb.height; ++y) {
        for (int x = 0; x < rgb.width; ++x) {
            rgb.at(x, y, 0) = 0.12F + 0.08F * static_cast<float>(x) / 7.0F;
            rgb.at(x, y, 1) = 0.10F + 0.55F * static_cast<float>(y) / 7.0F;
            rgb.at(x, y, 2) = 0.18F + 0.70F * static_cast<float>(x + y) / 14.0F;
        }
    }
    rgb.at(7, 7, 0) = 0.92F;
    rgb.at(7, 7, 1) = 0.88F;
    rgb.at(7, 7, 2) = 0.84F;

    dfee::PrintFinishPlan pf;
    pf.strength = 0.9F;
    pf.print_c = 2.0F;
    pf.print_m = -1.0F;
    pf.print_y = 3.0F;
    pf.print_contrast = 10.0F;
    pf.print_black_point = -2.0F;
    pf.shadow_lift = 0.03F;
    pf.contrast_boost = 1.15F;
    pf.highlight_rolloff = 0.78F;
    pf.highlight_rolloff_rate = 2.0F;
    pf.shadow_bias_lab = {0.0F, 0.8F, 1.2F};
    pf.midtone_bias_lab = {0.0F, 0.3F, 0.2F};
    pf.highlight_bias_lab = {0.0F, -0.4F, -0.6F};
    pf.red_boost = 0.2F;
    pf.blue_suppression = 0.1F;
    pf.green_shift = 0.05F;
    pf.saturation_scale = 1.08F;
    pf.grain_strength = 0.10F;
    pf.grain_size = 0.35F;

    const dfee::FilmRenderer renderer;
    const auto adjusted = renderer.apply_print_finish(rgb, pf);

    const float src_shadow = 0.2126F * rgb.at(0, 0, 0) + 0.7152F * rgb.at(0, 0, 1) + 0.0722F * rgb.at(0, 0, 2);
    const float dst_shadow = 0.2126F * adjusted.at(0, 0, 0) + 0.7152F * adjusted.at(0, 0, 1) + 0.0722F * adjusted.at(0, 0, 2);
    assert(dst_shadow > src_shadow);

    const auto src_oklab = dfee::rgb_to_oklab(rgb);
    const auto dst_oklab = dfee::rgb_to_oklab(adjusted);
    assert(std::fabs(dst_oklab.at(2, 2, 1) - src_oklab.at(2, 2, 1)) > 1.0e-4F);
    const float src_mid_spread = std::max({rgb.at(4, 4, 0), rgb.at(4, 4, 1), rgb.at(4, 4, 2)}) -
        std::min({rgb.at(4, 4, 0), rgb.at(4, 4, 1), rgb.at(4, 4, 2)});
    const float dst_mid_spread = std::max({adjusted.at(4, 4, 0), adjusted.at(4, 4, 1), adjusted.at(4, 4, 2)}) -
        std::min({adjusted.at(4, 4, 0), adjusted.at(4, 4, 1), adjusted.at(4, 4, 2)});
    assert(dst_mid_spread < src_mid_spread);

    double mean_abs_delta = 0.0;
    for (std::size_t i = 0; i < rgb.value_count(); ++i) {
        mean_abs_delta += std::fabs(adjusted.pixels[i] - rgb.pixels[i]);
        assert(adjusted.pixels[i] >= 0.0F && adjusted.pixels[i] <= 1.0F);
    }
    mean_abs_delta /= static_cast<double>(rgb.value_count());
    assert(mean_abs_delta > 1.0e-4);
}

void test_profile_loading() {
    const std::filesystem::path repo_root = DFEE_REPO_ROOT;
    const auto stock = dfee::load_film_stock_profile(repo_root / "profiles" / "stocks" / "astia_100.yaml");
    const auto print = dfee::load_print_stock_profile(repo_root / "profiles" / "print_stocks" / "kodak_2383.yaml");
    assert(!stock.stock_id.empty());
    assert(!stock.stock_name.empty());
    assert(!print.print_stock_id.empty());
    assert(!print.print_stock_name.empty());

    dfee::SolverInput input;
    input.tonal_distribution.midtone_anchor = 0.18F;
    input.tonal_distribution.highlight_headroom = 0.30F;
    input.tonal_distribution.shadow_depth = 0.08F;
    input.tonal_distribution.luma_p95 = 0.72F;
    input.raw_iso = 400;
    const dfee::RenderPlanSolver solver;
    const auto stocks = dfee::list_film_stock_profiles(repo_root / "profiles" / "stocks");
    assert(stocks.size() >= 25U);
    for (const auto& active_stock : stocks) {
        const auto plan = solver.solve(input, active_stock);
        assert(std::isfinite(plan.film_response.yellow_green_muting));
        assert(std::isfinite(plan.material_effects.grain_texture_masking));
        assert(plan.material_effects.grain_peak_zone == active_stock.string_values.at("grain.peak_zone"));
        assert(plan.material_effects.halation_trigger == active_stock.string_values.at("halation.trigger"));
        assert(std::fabs(plan.material_effects.halation_radius_inner - active_stock.numeric_values.at("halation.radius_inner")) < 1.0e-5F);
        assert(std::fabs(plan.material_effects.halation_radius_outer - active_stock.numeric_values.at("halation.radius_outer")) < 1.0e-5F);
        assert(std::fabs(plan.film_response.yellow_green_muting - active_stock.numeric_values.at("hue_saturation_response.yellow_green_muting")) < 1.0e-5F);
        // Panchromatic weights are a B&W concept; only B&W stocks specify them. When a
        // stock omits them the plan keeps its defaults, so only assert the match if present.
        if (active_stock.numeric_values.count("color_response.pan_weight_r") != 0U) {
            assert(std::fabs(plan.film_response.pan_weight_r - active_stock.numeric_values.at("color_response.pan_weight_r")) < 1.0e-5F);
            assert(std::fabs(plan.film_response.pan_weight_g - active_stock.numeric_values.at("color_response.pan_weight_g")) < 1.0e-5F);
            assert(std::fabs(plan.film_response.pan_weight_b - active_stock.numeric_values.at("color_response.pan_weight_b")) < 1.0e-5F);
        }
    }

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
    // select_file eagerly warms the draft decode, preview and raw-preview JPEG caches
    // (session-owned decode caches); only the full-resolution decode stays lazy.
    assert(initial_cache.cache.draft_decode_cached);
    assert(initial_cache.cache.preview_cached);
    assert(initial_cache.cache.raw_preview_jpeg_cached);
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
    // raw-preview JPEG was already warmed by select_file above.
    assert(draft_cache.cache.raw_preview_jpeg_cached);
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

void test_color_character_request_fields_default_to_neutral_zero() {
    dfee::NativePreviewRenderRequest request;
    require_close(request.highlight_color_hold, 0.0F, 1.0e-6F);
    require_close(request.shadow_color_retention, 0.0F, 1.0e-6F);
    require_close(request.palette_range, 0.0F, 1.0e-6F);
    require_close(request.emulsion_color_density, 0.0F, 1.0e-6F);

    dfee::SolverControls controls;
    controls.highlight_color_hold = request.highlight_color_hold;
    controls.shadow_color_retention = request.shadow_color_retention;
    controls.palette_range = request.palette_range;
    controls.emulsion_color_density = request.emulsion_color_density;
    require_close(controls.highlight_color_hold, 0.0F, 1.0e-6F);
}

void test_loader_accepts_optional_color_character_group() {
    const std::filesystem::path repo_root = DFEE_REPO_ROOT;
    const std::filesystem::path stocks_dir = repo_root / "profiles" / "stocks";
    const std::filesystem::path temp_path = stocks_dir / "native_cc_test_stock.yaml";
    struct CleanupGuard {
        std::filesystem::path path;
        ~CleanupGuard() { std::filesystem::remove(path); }
    } cleanup_guard{temp_path};

    {
        std::ofstream out(temp_path, std::ios::binary);
        // Base content mirrors astia_100.yaml so all required sections are present
        out << "stock_id: native_cc_test_stock\n";
        out << "stock_name: Native CC Test Stock\n";
        out << "stock_type: color_negative\n";
        out << "adaptation:\n";
        out << "  base_iso: 400\n";
        out << "  default_strength: 0.85\n";
        out << "  camera_cast_compensation_sensitivity: 0.5\n";
        out << "  highlight_stress_sensitivity: 0.6\n";
        out << "  shadow_noise_sensitivity: 0.4\n";
        out << "tone_response:\n";
        out << "  toe_strength: 0.40\n";
        out << "  toe_length: 0.28\n";
        out << "  midtone_contrast: 1.0\n";
        out << "  shoulder_strength: 0.60\n";
        out << "  highlight_rolloff_start: 0.72\n";
        out << "  black_density_floor: 0.01\n";
        out << "  channel_toe_mult: [1.0, 1.0, 1.0]\n";
        out << "  channel_shoulder_mult: [1.0, 1.0, 1.0]\n";
        out << "  channel_midtone_mult: [1.0, 1.0, 1.0]\n";
        out << "color_response:\n";
        out << "  shadow_bias_lab: [0.0, 0.0, 0.0]\n";
        out << "  midtone_bias_lab: [0.0, 0.0, 0.0]\n";
        out << "  highlight_bias_lab: [0.0, 0.0, 0.0]\n";
        out << "  blue_cast_suppression: 0.1\n";
        out << "  green_magenta_stabilization: 0.1\n";
        out << "  pan_weight_r: 0.299\n";
        out << "  pan_weight_g: 0.587\n";
        out << "  pan_weight_b: 0.114\n";
        out << "hue_saturation_response:\n";
        out << "  saturation_boost: 1.0\n";
        out << "  red_orange_midtone_compression: 0.2\n";
        out << "  yellow_green_muting: 0.1\n";
        out << "  cyan_blue_highlight_compression: 0.2\n";
        out << "  neon_compression: 0.5\n";
        out << "  highlight_desaturation: 0.4\n";
        out << "grain:\n";
        out << "  size: 0.40\n";
        out << "  strength: 0.40\n";
        out << "  roughness: 0.35\n";
        out << "  chroma_strength: 0.1\n";
        out << "  peak_zone: midtones_heavy\n";
        out << "  texture_masking: 0.7\n";
        out << "  family: modern_color_negative\n";
        out << "  target_pgi_35mm_4x6: 55\n";
        out << "  clumpiness: 0.5\n";
        out << "  micro_grit: 0.3\n";
        out << "  layer_correlation: 0.6\n";
        out << "  shadow_response: 1.0\n";
        out << "  midtone_response: 1.0\n";
        out << "  highlight_response: 0.3\n";
        out << "  underexposure_coarsening: 0.4\n";
        out << "  overexposure_smoothing: 0.2\n";
        out << "halation:\n";
        out << "  trigger: specular_only\n";
        out << "  strength: 0.1\n";
        out << "  radius_inner: 4\n";
        out << "  radius_outer: 12\n";
        out << "  warm_core: [1.0, 0.5, 0.2]\n";
        out << "  red_fringe: [1.0, 0.1, 0.0]\n";
        out << "chroma_coupling:\n";
        out << "  hi_rolloff_start: 0.75\n";
        out << "  hi_rolloff_rate: 1.5\n";
        out << "  hi_compression: 0.5\n";
        out << "  sh_rolloff_start: 0.18\n";
        out << "  sh_compression: 0.4\n";
        out << "  hi_hue_conv_rad: 0.2\n";
        out << "  hi_hue_conv_str: 0.1\n";
        out << "dye_contamination:\n";
        out << "  r_to_g: 0.0\n";
        out << "  g_to_r: 0.0\n";
        out << "  b_to_g: 0.0\n";
        out << "  b_to_r: 0.0\n";
        out << "  r_to_b: 0.0\n";
        out << "  g_to_b: 0.0\n";
        // Append the new optional color_character group with variable-length anchors
        out << "color_character:\n";
        out << "  highlight_hold_sensitivity: 0.6\n";
        out << "  shadow_retention_sensitivity: 0.5\n";
        out << "  emulsion_density_sensitivity: 0.4\n";
        out << "  palette:\n";
        out << "    range_sensitivity: 0.7\n";
        out << "    anchors: [30, 90, 150, 210, 270, 330]\n";
        out << "    anchor_weights: [1.0, 0.8, 0.6, 0.6, 0.8, 1.0]\n";
    }

    // Must not throw — let any exception propagate to main()'s catch so the test fails correctly
    const dfee::FilmStockProfile profile = dfee::load_film_stock_profile(temp_path);

    // Verify numeric leaves are present
    if (!profile.numeric_values.contains("color_character.highlight_hold_sensitivity")) {
        throw std::runtime_error("color_character.highlight_hold_sensitivity not found in profile");
    }
    if (!profile.numeric_values.contains("color_character.shadow_retention_sensitivity")) {
        throw std::runtime_error("color_character.shadow_retention_sensitivity not found in profile");
    }
    if (!profile.numeric_values.contains("color_character.emulsion_density_sensitivity")) {
        throw std::runtime_error("color_character.emulsion_density_sensitivity not found in profile");
    }
    if (!profile.numeric_values.contains("color_character.palette.range_sensitivity")) {
        throw std::runtime_error("color_character.palette.range_sensitivity not found in profile");
    }

    // Verify variable-length arrays
    if (!profile.numeric_arrays.contains("color_character.palette.anchors")) {
        throw std::runtime_error("color_character.palette.anchors not found in profile");
    }
    if (profile.numeric_arrays.at("color_character.palette.anchors").size() != 6U) {
        throw std::runtime_error("color_character.palette.anchors does not have 6 elements");
    }
    if (!profile.numeric_arrays.contains("color_character.palette.anchor_weights")) {
        throw std::runtime_error("color_character.palette.anchor_weights not found in profile");
    }
    if (profile.numeric_arrays.at("color_character.palette.anchor_weights").size() != 6U) {
        throw std::runtime_error("color_character.palette.anchor_weights does not have 6 elements");
    }
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

void test_highlight_color_hold_increases_only_highlight_chroma() {
    // Three pixels: bright chromatic (highlight zone), deep shadow chromatic, and midtone chromatic
    dfee::Image rgb(3, 1, 3);
    rgb.pixels = {
        // pixel 0 — bright highlight: high luminance, clearly chromatic
        0.95F, 0.70F, 0.55F,
        // pixel 1 — deep shadow: very low luminance, chromatic — well below highlight zones
        0.18F, 0.10F, 0.06F,
        // pixel 2 — midtone: moderate luminance, chromatic — zone-5 mask bleed is ~3.9e-4 (measurable but small)
        0.42F, 0.28F, 0.18F,
    };

    const auto luminance = dfee::compute_luminance(rgb);
    const dfee::ImageStateAnalyzer analyzer;
    const auto zones = analyzer.generate_zone_masks(luminance, 0.18F);

    // Build a response plan with meaningful highlight desaturation and hi_comp
    // so that the control has something to modulate
    dfee::FilmResponsePlan response;
    response.stock_type = "color_negative";
    response.film_color = 100.0F;
    response.highlight_desaturation = 0.6F;
    response.chroma_coupling = {
        {"hi_rolloff_start", 0.70F},
        {"hi_rolloff_rate", 2.0F},
        {"hi_compression", 0.55F},
        {"sh_rolloff_start", 0.18F},
        {"sh_compression", 0.40F},
        {"hi_hue_conv_rad", 0.28F},
        {"hi_hue_conv_str", 0.18F},
    };
    // highlight_color_hold = 0 → baseline (no effect)
    response.highlight_color_hold = 0.0F;
    response.highlight_hold_sensitivity = 1.0F;

    const dfee::FilmRenderer renderer;
    const auto baseline = renderer.apply_color_response_and_coupling(rgb, zones, response);

    // Now apply with hold = +100
    response.highlight_color_hold = 100.0F;
    const auto held = renderer.apply_color_response_and_coupling(rgb, zones, response);

    // Measure OKLab chroma (c = sqrt(a^2 + b^2)) for each pixel
    const auto to_chroma = [](const dfee::Image& img, int x) -> float {
        const auto oklab = dfee::rgb_to_oklab(img);
        const float a = oklab.at(x, 0, 1);
        const float b = oklab.at(x, 0, 2);
        return std::sqrt(a * a + b * b);
    };

    const float baseline_hi_chroma     = to_chroma(baseline, 0);
    const float held_hi_chroma         = to_chroma(held,     0);
    const float baseline_shadow_chroma = to_chroma(baseline, 1);
    const float held_shadow_chroma     = to_chroma(held,     1);
    const float baseline_mid_chroma    = to_chroma(baseline, 2);
    const float held_mid_chroma        = to_chroma(held,     2);

    // Hold=+100 must preserve MORE chroma in highlights than hold=0
    if (!(held_hi_chroma > baseline_hi_chroma)) {
        throw std::runtime_error(
            "highlight_color_hold=+100 did not increase highlight chroma: "
            "held=" + std::to_string(held_hi_chroma) +
            " baseline=" + std::to_string(baseline_hi_chroma));
    }

    // Deep-shadow pixel must be byte-unchanged (no zone-5 mask weight at this luminance)
    if (std::fabs(held_shadow_chroma - baseline_shadow_chroma) >= 1.0e-4F) {
        throw std::runtime_error(
            "highlight_color_hold=+100 affected deep-shadow chroma unexpectedly: "
            "delta=" + std::to_string(std::fabs(held_shadow_chroma - baseline_shadow_chroma)));
    }

    // Midtone locality: the highlight chroma gain must dwarf any midtone leakage by 10x.
    // A true zone-5 midtone has ~3.9e-4 mask bleed, so we use a ratio bound rather than
    // an absolute tolerance. This proves highlight-locality without over-tight thresholds.
    const float hi_delta  = held_hi_chroma - baseline_hi_chroma;
    const float mid_delta = std::fabs(held_mid_chroma - baseline_mid_chroma);
    if (!(hi_delta > mid_delta * 10.0F)) {
        throw std::runtime_error(
            "highlight_color_hold locality failed: highlight delta=" + std::to_string(hi_delta) +
            " must be >10x midtone delta=" + std::to_string(mid_delta));
    }
}

void test_shadow_color_retention_increases_shadow_chroma_without_lifting_blacks() {
    // Two pixels: deep shadow chromatic, bright highlight chromatic.
    // Pixel 0: OKLab l ≈ 0.150 (below sh_rolloff_start=0.18) — sh_mask is active.
    //   Computed: l_xyz ≈ 0.0044, cbrt ≈ 0.164 → OKLab l ≈ 0.150 → t_sh ≈ 0.167, sh_mask ≈ 0.068.
    //   This pixel is in the shadow zone so shadow chroma rolloff applies; retention will reduce it.
    // Pixel 1: OKLab l >> sh_start — sh_mask ≈ 0, so shadow retention has no effect.
    dfee::Image rgb(2, 1, 3);
    rgb.pixels = {
        // pixel 0 — deep shadow chromatic: OKLab l ≈ 0.150 < sh_start=0.18
        0.008F, 0.002F, 0.001F,
        // pixel 1 — bright highlight chromatic: OKLab l >> sh_start
        0.92F, 0.70F, 0.50F,
    };

    const auto luminance = dfee::compute_luminance(rgb);
    const dfee::ImageStateAnalyzer analyzer;
    const auto zones = analyzer.generate_zone_masks(luminance, 0.18F);

    dfee::FilmResponsePlan response;
    response.stock_type = "color_negative";
    response.film_color = 100.0F;
    response.chroma_coupling = {
        {"hi_rolloff_start", 0.75F},
        {"hi_rolloff_rate", 1.8F},
        {"hi_compression", 0.50F},
        {"sh_rolloff_start", 0.18F},
        {"sh_compression", 0.45F},
        {"hi_hue_conv_rad", 0.28F},
        {"hi_hue_conv_str", 0.18F},
    };

    // baseline: shadow_color_retention = 0 → identity
    response.shadow_color_retention = 0.0F;
    response.shadow_retention_sensitivity = 1.0F;

    const dfee::FilmRenderer renderer;
    const auto baseline = renderer.apply_color_response_and_coupling(rgb, zones, response);

    // held: shadow_color_retention = +100 → should preserve MORE chroma in shadows
    response.shadow_color_retention = 100.0F;
    const auto retained = renderer.apply_color_response_and_coupling(rgb, zones, response);

    const auto to_chroma = [](const dfee::Image& img, int x) -> float {
        const auto oklab = dfee::rgb_to_oklab(img);
        const float a = oklab.at(x, 0, 1);
        const float b = oklab.at(x, 0, 2);
        return std::sqrt(a * a + b * b);
    };

    const auto to_lightness = [](const dfee::Image& img, int x) -> float {
        const auto oklab = dfee::rgb_to_oklab(img);
        return oklab.at(x, 0, 0);
    };

    const float baseline_shadow_chroma  = to_chroma(baseline, 0);
    const float retained_shadow_chroma  = to_chroma(retained, 0);
    const float baseline_hi_chroma      = to_chroma(baseline, 1);
    const float retained_hi_chroma      = to_chroma(retained, 1);

    const float baseline_shadow_L       = to_lightness(baseline, 0);
    const float retained_shadow_L       = to_lightness(retained, 0);

    // retention=+100 must yield higher shadow chroma than retention=0
    if (!(retained_shadow_chroma > baseline_shadow_chroma)) {
        throw std::runtime_error(
            "shadow_color_retention=+100 did not increase shadow chroma: "
            "retained=" + std::to_string(retained_shadow_chroma) +
            " baseline=" + std::to_string(baseline_shadow_chroma));
    }

    // Lightness of the shadow pixel must NOT change (no black lift)
    if (std::fabs(retained_shadow_L - baseline_shadow_L) >= 1.0e-5F) {
        throw std::runtime_error(
            "shadow_color_retention=+100 changed shadow lightness (black lift!): "
            "delta=" + std::to_string(std::fabs(retained_shadow_L - baseline_shadow_L)));
    }

    // Highlight chroma must be unchanged (sh_mask is ~0 in bright highlights)
    // Use a 10x ratio bound: shadow chroma gain must dwarf any highlight leakage
    const float shadow_delta    = retained_shadow_chroma - baseline_shadow_chroma;
    const float hi_delta        = std::fabs(retained_hi_chroma - baseline_hi_chroma);
    if (!(shadow_delta > hi_delta * 10.0F)) {
        throw std::runtime_error(
            "shadow_color_retention locality failed: shadow delta=" + std::to_string(shadow_delta) +
            " must be >10x highlight delta=" + std::to_string(hi_delta));
    }
}

// ---------------------------------------------------------------------------
// Task 5: Palette Separation tests
// ---------------------------------------------------------------------------

// Helper: build a minimal ZoneMasks for a 1×N image (all midtone weight)
static dfee::ZoneMasks make_flat_zone_masks(const dfee::Image& rgb) {
    const auto luminance = dfee::compute_luminance(rgb);
    const dfee::ImageStateAnalyzer analyzer;
    return analyzer.generate_zone_masks(luminance, 0.18F);
}

// Helper: read OKLCh hue from pixel x of a 1-row image
static float read_hue(const dfee::Image& img, int x) {
    const auto oklab = dfee::rgb_to_oklab(img);
    const float a = oklab.at(x, 0, 1);
    const float b = oklab.at(x, 0, 2);
    float h = std::atan2(b, a);
    if (h < 0.0F) {
        h += 2.0F * std::numbers::pi_v<float>;
    }
    return h;
}

// Helper: read OKLCh chroma from pixel x of a 1-row image
static float read_chroma(const dfee::Image& img, int x) {
    const auto oklab = dfee::rgb_to_oklab(img);
    const float a = oklab.at(x, 0, 1);
    const float b = oklab.at(x, 0, 2);
    return std::sqrt(a * a + b * b);
}

// Helper: read OKLab L (lightness) from pixel x of a 1-row image
static float read_lightness(const dfee::Image& img, int x) {
    const auto oklab = dfee::rgb_to_oklab(img);
    return oklab.at(x, 0, 0);
}

// Helper: signed shortest-arc delta from h toward anchor a (radians)
static float hue_delta(float h, float a) {
    constexpr float kPi = std::numbers::pi_v<float>;
    float d = std::fmod((a - h) + kPi, 2.0F * kPi) - kPi;
    return d;
}


// ---------------------------------------------------------------------------
// Task 6: Emulsion Color Density + Solver family defaults
// ---------------------------------------------------------------------------

void test_emulsion_color_density_increases_mid_saturation_chroma() {
    // A mid-saturation pixel at moderate luminance.
    // With emulsion_color_density=+100 and sensitivity=1, chroma_boost is multiplied by
    // (1 + kEmulsionDensityGain * 1.0), raising output chroma vs density=0.
    // chroma_boost is set to 1.0 (neutral) to isolate the density effect from base saturation.
    dfee::Image rgb(1, 1, 3);
    rgb.pixels = {0.50F, 0.30F, 0.20F};  // mid-saturation, mid-luminance

    const auto luminance = dfee::compute_luminance(rgb);
    const dfee::ImageStateAnalyzer analyzer;
    const auto zones = analyzer.generate_zone_masks(luminance, 0.18F);

    // Baseline: density = 0 (no effect)
    dfee::FilmResponsePlan response;
    response.stock_type = "color_negative";
    response.film_color = 100.0F;
    response.chroma_boost = 1.0F;  // neutral — isolates density effect
    response.emulsion_color_density = 0.0F;
    response.emulsion_density_sensitivity = 1.0F;

    const dfee::FilmRenderer renderer;
    const auto baseline = renderer.apply_color_response_and_coupling(rgb, zones, response);

    // Active: density = +100
    response.emulsion_color_density = 100.0F;
    const auto active = renderer.apply_color_response_and_coupling(rgb, zones, response);

    const auto to_chroma = [](const dfee::Image& img, int x) -> float {
        const auto oklab = dfee::rgb_to_oklab(img);
        const float a = oklab.at(x, 0, 1);
        const float b = oklab.at(x, 0, 2);
        return std::sqrt(a * a + b * b);
    };

    const float baseline_chroma = to_chroma(baseline, 0);
    const float active_chroma   = to_chroma(active,   0);

    if (!(active_chroma > baseline_chroma)) {
        throw std::runtime_error(
            "emulsion_color_density=+100 (sensitivity=1) did not increase mid-saturation chroma: "
            "active=" + std::to_string(active_chroma) +
            " baseline=" + std::to_string(baseline_chroma));
    }
}

void test_solver_color_character_family_defaults() {
    const std::filesystem::path repo_root = DFEE_REPO_ROOT;

    // Case 1: color_negative stock (portra_400) — no color_character YAML block.
    // Solver must apply family defaults → all four sensitivities must be non-zero.
    const auto color_stock = dfee::load_film_stock_profile(
        repo_root / "profiles" / "stocks" / "portra_400.yaml");

    dfee::SolverInput input;
    input.tonal_distribution.tonal_skew = "normal";
    input.tonal_distribution.dynamic_range_stops = 11.0F;
    input.tonal_distribution.midtone_anchor = 0.18F;
    input.tonal_distribution.highlight_headroom = 0.25F;
    input.tonal_distribution.shadow_depth = 0.05F;
    input.tonal_distribution.luma_p95 = 0.78F;
    input.camera_input_bias = dfee::CameraBiasAnalysis{.neutral_confidence = 0.9F};
    input.raw_iso = 400;

    dfee::SolverControls controls;
    const dfee::RenderPlanSolver solver;
    const auto color_plan = solver.solve(input, color_stock, controls);

    if (!(color_plan.film_response.highlight_hold_sensitivity > 0.0F)) {
        throw std::runtime_error(
            "portra_400: highlight_hold_sensitivity must be > 0, got " +
            std::to_string(color_plan.film_response.highlight_hold_sensitivity));
    }
    if (!(color_plan.film_response.shadow_retention_sensitivity > 0.0F)) {
        throw std::runtime_error(
            "portra_400: shadow_retention_sensitivity must be > 0, got " +
            std::to_string(color_plan.film_response.shadow_retention_sensitivity));
    }
    if (!(color_plan.film_response.emulsion_density_sensitivity > 0.0F)) {
        throw std::runtime_error(
            "portra_400: emulsion_density_sensitivity must be > 0, got " +
            std::to_string(color_plan.film_response.emulsion_density_sensitivity));
    }
    if (!(color_plan.film_response.palette_range_sensitivity > 0.0F)) {
        throw std::runtime_error(
            "portra_400: palette_range_sensitivity must be > 0, got " +
            std::to_string(color_plan.film_response.palette_range_sensitivity));
    }

    // Case 2: monochrome stock (tri_x_400) — all four sensitivities must be 0.
    const auto mono_stock = dfee::load_film_stock_profile(
        repo_root / "profiles" / "stocks" / "tri_x_400.yaml");
    const auto mono_plan = solver.solve(input, mono_stock, controls);

    if (std::fabs(mono_plan.film_response.highlight_hold_sensitivity) > 1.0e-6F) {
        throw std::runtime_error(
            "tri_x_400: highlight_hold_sensitivity must be 0 for monochrome, got " +
            std::to_string(mono_plan.film_response.highlight_hold_sensitivity));
    }
    if (std::fabs(mono_plan.film_response.shadow_retention_sensitivity) > 1.0e-6F) {
        throw std::runtime_error(
            "tri_x_400: shadow_retention_sensitivity must be 0 for monochrome, got " +
            std::to_string(mono_plan.film_response.shadow_retention_sensitivity));
    }
    if (std::fabs(mono_plan.film_response.emulsion_density_sensitivity) > 1.0e-6F) {
        throw std::runtime_error(
            "tri_x_400: emulsion_density_sensitivity must be 0 for monochrome, got " +
            std::to_string(mono_plan.film_response.emulsion_density_sensitivity));
    }
    if (std::fabs(mono_plan.film_response.palette_range_sensitivity) > 1.0e-6F) {
        throw std::runtime_error(
            "tri_x_400: palette_range_sensitivity must be 0 for monochrome, got " +
            std::to_string(mono_plan.film_response.palette_range_sensitivity));
    }
}

void test_solver_density_defaults() {
    const std::filesystem::path repo_root = DFEE_REPO_ROOT;

    dfee::SolverInput input;
    input.tonal_distribution.tonal_skew = "normal";
    input.tonal_distribution.dynamic_range_stops = 11.0F;
    input.tonal_distribution.midtone_anchor = 0.18F;
    input.tonal_distribution.highlight_headroom = 0.25F;
    input.tonal_distribution.shadow_depth = 0.05F;
    input.tonal_distribution.luma_p95 = 0.78F;
    input.camera_input_bias = dfee::CameraBiasAnalysis{.neutral_confidence = 0.9F};
    input.raw_iso = 400;

    dfee::SolverControls controls;  // defaults: film_color_density == 100
    const dfee::RenderPlanSolver solver;

    // Color negative: control defaults to 100, family density strength non-zero.
    const auto color_stock = dfee::load_film_stock_profile(
        repo_root / "profiles" / "stocks" / "portra_400.yaml");
    const auto color_plan = solver.solve(input, color_stock, controls);
    if (std::fabs(color_plan.film_response.film_color_density - 100.0F) > 1.0e-4F) {
        throw std::runtime_error(
            "portra_400: film_color_density control default must be 100, got " +
            std::to_string(color_plan.film_response.film_color_density));
    }
    if (!(color_plan.film_response.density_strength > 0.0F)) {
        throw std::runtime_error(
            "portra_400: density_strength must be > 0, got " +
            std::to_string(color_plan.film_response.density_strength));
    }

    // Monochrome: density strength must be 0.
    const auto mono_stock = dfee::load_film_stock_profile(
        repo_root / "profiles" / "stocks" / "tri_x_400.yaml");
    const auto mono_plan = solver.solve(input, mono_stock, controls);
    if (std::fabs(mono_plan.film_response.density_strength) > 1.0e-6F) {
        throw std::runtime_error(
            "tri_x_400: density_strength must be 0 for monochrome, got " +
            std::to_string(mono_plan.film_response.density_strength));
    }
}

void test_color_compression_compresses_high_chroma_preserves_neutral() {
    const auto oklch_to_rgb_arr = [](float l, float c, float h) -> std::array<float, 3> {
        const float a = c * std::cos(h);
        const float b = c * std::sin(h);
        const dfee::Image tmp = dfee::oklab_to_rgb([&]() {
            dfee::Image lab(1, 1, 3);
            lab.at(0, 0, 0) = l; lab.at(0, 0, 1) = a; lab.at(0, 0, 2) = b;
            return lab;
        }());
        return {tmp.at(0, 0, 0), tmp.at(0, 0, 1), tmp.at(0, 0, 2)};
    };
    dfee::Image rgb(3, 1, 3);
    const auto p0 = oklch_to_rgb_arr(0.60F, 0.22F, 0.5F); // high chroma
    const auto p1 = oklch_to_rgb_arr(0.60F, 0.08F, 0.5F); // mid/low chroma (below threshold)
    const auto p2 = oklch_to_rgb_arr(0.60F, 0.00F, 0.0F); // neutral
    for (int k = 0; k < 3; ++k) { rgb.at(0, 0, k) = p0[static_cast<std::size_t>(k)]; }
    for (int k = 0; k < 3; ++k) { rgb.at(1, 0, k) = p1[static_cast<std::size_t>(k)]; }
    for (int k = 0; k < 3; ++k) { rgb.at(2, 0, k) = p2[static_cast<std::size_t>(k)]; }

    const auto lch_of = [](const dfee::Image& img) {
        return dfee::oklab_to_oklch(dfee::rgb_to_oklab(img));
    };
    const auto base = lch_of(rgb);

    dfee::FilmResponsePlan response;
    response.stock_type = "color_negative";
    response.compression_strength = 0.6F;
    response.compression_threshold = 0.45F;
    response.compression_crosstalk = 0.0F; // isolate the shoulder
    const dfee::FilmRenderer renderer;

    response.film_color_compression = 0.0F;
    const auto out0 = renderer.apply_color_compression(rgb, response);
    for (std::size_t i = 0; i < rgb.pixels.size(); ++i) {
        if (out0.pixels[i] != rgb.pixels[i]) {
            throw std::runtime_error("film_color_compression=0 must be a byte-identical no-op");
        }
    }

    response.film_color_compression = 100.0F;
    const auto out100 = renderer.apply_color_compression(rgb, response);
    const auto lch100 = lch_of(out100);
    // high-chroma pixel is compressed (chroma decreases)
    if (!(lch100.at(0, 0, 1) < base.at(0, 0, 1) - 1.0e-3F)) {
        throw std::runtime_error("high-chroma pixel must be compressed by the shoulder");
    }
    // mid/low-chroma pixel below threshold ~unchanged
    if (std::fabs(lch100.at(1, 0, 1) - base.at(1, 0, 1)) > 2.0e-3F) {
        throw std::runtime_error("below-threshold chroma must be ~unchanged");
    }
    // neutral pixel unchanged
    for (int k = 0; k < 3; ++k) {
        if (std::fabs(out100.at(2, 0, k) - rgb.at(2, 0, k)) > 1.0e-4F) {
            throw std::runtime_error("neutral pixel must be unchanged by compression");
        }
    }
    // hue of high-chroma pixel unchanged (crosstalk off)
    if (std::fabs(lch100.at(0, 0, 2) - base.at(0, 0, 2)) > 3.0e-3F) {
        throw std::runtime_error("shoulder must not shift hue");
    }
    // 200 compresses more than 100
    response.film_color_compression = 200.0F;
    const auto lch200 = lch_of(renderer.apply_color_compression(rgb, response));
    if (!(lch200.at(0, 0, 1) < lch100.at(0, 0, 1) - 1.0e-3F)) {
        throw std::runtime_error("film_color_compression=200 must compress more than 100");
    }
}

void test_color_compression_leans_neighbours_preserves_neutral() {
    const auto oklch_to_rgb_arr = [](float l, float c, float h) -> std::array<float, 3> {
        const float a = c * std::cos(h);
        const float b = c * std::sin(h);
        const dfee::Image tmp = dfee::oklab_to_rgb([&]() {
            dfee::Image lab(1, 1, 3);
            lab.at(0, 0, 0) = l; lab.at(0, 0, 1) = a; lab.at(0, 0, 2) = b;
            return lab;
        }());
        return {tmp.at(0, 0, 0), tmp.at(0, 0, 1), tmp.at(0, 0, 2)};
    };
    dfee::Image rgb(3, 1, 3);
    const auto pr = oklch_to_rgb_arr(0.55F, 0.14F, 0.5F);  // saturated red-ish
    const auto pb = oklch_to_rgb_arr(0.55F, 0.14F, 4.0F);  // saturated blue-ish
    const auto pn = oklch_to_rgb_arr(0.55F, 0.005F, 0.5F); // near-neutral
    for (int k = 0; k < 3; ++k) { rgb.at(0, 0, k) = pr[static_cast<std::size_t>(k)]; }
    for (int k = 0; k < 3; ++k) { rgb.at(1, 0, k) = pb[static_cast<std::size_t>(k)]; }
    for (int k = 0; k < 3; ++k) { rgb.at(2, 0, k) = pn[static_cast<std::size_t>(k)]; }

    const auto lch_of = [](const dfee::Image& img) {
        return dfee::oklab_to_oklch(dfee::rgb_to_oklab(img));
    };
    const auto base = lch_of(rgb);

    dfee::FilmResponsePlan response;
    response.stock_type = "color_negative";
    response.compression_strength = 0.0F;   // isolate crosstalk
    response.compression_crosstalk = 1.0F;
    response.film_color_compression = 100.0F;
    const dfee::FilmRenderer renderer;

    // crosstalk = 0 -> hue unchanged
    response.compression_crosstalk = 0.0F;
    const auto lch_off = lch_of(renderer.apply_color_compression(rgb, response));
    if (std::fabs(lch_off.at(0, 0, 2) - base.at(0, 0, 2)) > 1.0e-4F) {
        throw std::runtime_error("crosstalk=0 must not shift hue");
    }

    response.compression_crosstalk = 1.0F;
    const auto lch1 = lch_of(renderer.apply_color_compression(rgb, response));
    // red leans toward orange => hue increases (toward yellow), bounded
    const float dr = lch1.at(0, 0, 2) - base.at(0, 0, 2);
    if (!(dr > 1.0e-3F) || dr > 0.30F) {
        throw std::runtime_error("red must lean a small bounded amount toward orange, got dh=" + std::to_string(dr));
    }
    // blue leans toward cyan => hue decreases, bounded
    const float db = lch1.at(1, 0, 2) - base.at(1, 0, 2);
    if (!(db < -1.0e-3F) || db < -0.30F) {
        throw std::runtime_error("blue must lean a small bounded amount toward cyan, got dh=" + std::to_string(db));
    }
    // near-neutral hue unchanged (chroma gate)
    if (std::fabs(lch1.at(2, 0, 2) - base.at(2, 0, 2)) > 5.0e-3F) {
        throw std::runtime_error("near-neutral pixel hue must be preserved by the chroma gate");
    }
}

void test_solver_compression_defaults() {
    const std::filesystem::path repo_root = DFEE_REPO_ROOT;
    dfee::SolverInput input;
    input.tonal_distribution.tonal_skew = "normal";
    input.tonal_distribution.dynamic_range_stops = 11.0F;
    input.tonal_distribution.midtone_anchor = 0.18F;
    input.tonal_distribution.highlight_headroom = 0.25F;
    input.tonal_distribution.shadow_depth = 0.05F;
    input.tonal_distribution.luma_p95 = 0.78F;
    input.camera_input_bias = dfee::CameraBiasAnalysis{.neutral_confidence = 0.9F};
    input.raw_iso = 400;

    dfee::SolverControls controls;  // film_color_compression default 100
    const dfee::RenderPlanSolver solver;

    const auto color_stock = dfee::load_film_stock_profile(
        repo_root / "profiles" / "stocks" / "portra_400.yaml");
    const auto color_plan = solver.solve(input, color_stock, controls);
    if (std::fabs(color_plan.film_response.film_color_compression - 100.0F) > 1.0e-4F) {
        throw std::runtime_error(
            "portra_400: film_color_compression default must be 100, got " +
            std::to_string(color_plan.film_response.film_color_compression));
    }
    if (!(color_plan.film_response.compression_strength > 0.0F)) {
        throw std::runtime_error(
            "portra_400: compression_strength must be > 0, got " +
            std::to_string(color_plan.film_response.compression_strength));
    }

    // Color Compression is folded into the Color Density control: the plan's compression
    // control follows film_color_density, and the separate film_color_compression is ignored.
    dfee::SolverControls merged = controls;
    merged.film_color_density = 150.0F;
    merged.film_color_compression = 0.0F; // must have no effect now
    const auto merged_plan = solver.solve(input, color_stock, merged);
    if (std::fabs(merged_plan.film_response.film_color_compression - 150.0F) > 1.0e-4F) {
        throw std::runtime_error(
            "compression control must follow film_color_density (merged), got " +
            std::to_string(merged_plan.film_response.film_color_compression));
    }

    const auto mono_stock = dfee::load_film_stock_profile(
        repo_root / "profiles" / "stocks" / "tri_x_400.yaml");
    const auto mono_plan = solver.solve(input, mono_stock, controls);
    if (std::fabs(mono_plan.film_response.compression_strength) > 1.0e-6F) {
        throw std::runtime_error(
            "tri_x_400: compression_strength must be 0 for monochrome, got " +
            std::to_string(mono_plan.film_response.compression_strength));
    }
}

void test_filmic_grain_uniform_softlight() {
    const int w = 96, h = 90;
    dfee::Image img(w, h, 3);
    // three horizontal bands: near-black, mid-gray, near-white (linear)
    for (int y = 0; y < h; ++y) {
        const float v = (y < 30) ? 0.02F : (y < 60 ? 0.22F : 0.90F);
        for (int x = 0; x < w; ++x) {
            img.at(x, y, 0) = v; img.at(x, y, 1) = v; img.at(x, y, 2) = v;
        }
    }
    // deliberately non-uniform receptivity mask: left half 0, right half 1.
    dfee::SpatialMasks masks;
    masks.grain_receptivity_mask = dfee::LuminanceImage(w, h);
    masks.halation_source_mask = dfee::LuminanceImage(w, h);
    masks.halation_receiver_mask = dfee::LuminanceImage(w, h);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            masks.grain_receptivity_mask.values[static_cast<std::size_t>(y) * w + x] = (x < w / 2) ? 0.0F : 1.0F;
        }
    }
    dfee::MaterialEffectsPlan fx;
    fx.grain_strength = 1.0F;
    fx.grain_size = 0.5F;
    fx.grain_roughness = 0.3F;
    fx.grain_chroma_strength = 0.0F; // mono
    fx.grain_seed = 12345U;
    fx.grain_target_pgi = 37.0F;
    fx.grain_shadow_response = 0.72F;
    fx.grain_midtone_response = 1.0F;
    fx.grain_highlight_response = 0.28F;
    fx.grain_peak_zone = "lower_mid_to_mid";

    const dfee::FilmRenderer renderer;
    const auto out = renderer.apply_filmic_grain(img, masks, fx);

    auto variance = [&](const dfee::Image& im, int x0, int x1, int y0, int y1) {
        double mean = 0.0; int n = 0;
        for (int y = y0; y < y1; ++y) for (int x = x0; x < x1; ++x) { mean += im.at(x, y, 0); ++n; }
        mean /= std::max(1, n);
        double var = 0.0;
        for (int y = y0; y < y1; ++y) for (int x = x0; x < x1; ++x) { const double d = im.at(x, y, 0) - mean; var += d * d; }
        return var / std::max(1, n);
    };
    const double var_mid_left = variance(out, 0, w / 2, 30, 60);
    const double var_mid_right = variance(out, w / 2, w, 30, 60);
    const double var_black = variance(out, 0, w, 0, 30);
    const double var_white = variance(out, 0, w, 60, 90);

    // (a) visible
    if (!(var_mid_right > 1.0e-5)) {
        throw std::runtime_error("filmic grain must be visible on mid-gray, var=" + std::to_string(var_mid_right));
    }
    // (b) spatially uniform: grain must NOT be gated by the receptivity mask (anti-blotch)
    const double denom = std::max(var_mid_left, var_mid_right);
    if (std::fabs(var_mid_left - var_mid_right) / std::max(denom, 1e-12) > 0.25) {
        throw std::runtime_error("grain must be spatially uniform (ignore receptivity): left=" +
            std::to_string(var_mid_left) + " right=" + std::to_string(var_mid_right));
    }
    // (c) soft-light taper: grain weaker in extremes than mid
    if (!(var_mid_right > var_black * 1.5) || !(var_mid_right > var_white * 1.5)) {
        throw std::runtime_error("grain must taper in shadows/highlights vs mid");
    }
    // (d) determinism
    const auto out2 = renderer.apply_filmic_grain(img, masks, fx);
    for (std::size_t i = 0; i < out.pixels.size(); ++i) {
        if (out.pixels[i] != out2.pixels[i]) {
            throw std::runtime_error("filmic grain must be deterministic for a fixed seed");
        }
    }
}

void test_halation_threshold_and_strength() {
    const int w = 160, h = 160;
    const int cx = 80, cy = 80, rad = 16;
    dfee::Image img(w, h, 3);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const int dx = x - cx, dy = y - cy;
            const float v = (dx * dx + dy * dy <= rad * rad) ? 0.70F : 0.25F; // moderately-bright disc
            img.at(x, y, 0) = v; img.at(x, y, 1) = v; img.at(x, y, 2) = v;
        }
    }
    const auto zones = make_flat_zone_masks(img);
    dfee::SpatialMasks masks;
    masks.grain_receptivity_mask = dfee::LuminanceImage(w, h);
    masks.halation_source_mask = dfee::LuminanceImage(w, h);
    masks.halation_receiver_mask = dfee::LuminanceImage(w, h);
    for (auto& v : masks.halation_receiver_mask.values) { v = 1.0F; } // deposit glow everywhere

    dfee::MaterialEffectsPlan fx;
    fx.bloom_strength = 0.0F;
    fx.halation_subtractive = true;
    fx.halation_radius_inner = 40.0F;
    fx.halation_radius_outer = 120.0F;
    fx.halation_warm_core = {1.0F, 0.30F, 0.10F};
    fx.halation_red_fringe = {1.0F, 0.20F, 0.05F};

    const dfee::FilmRenderer renderer;
    auto ring_added = [&](const dfee::Image& out, int ch) {
        double s = 0.0;
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                const int dx = x - cx, dy = y - cy;
                const int d2 = dx * dx + dy * dy;
                if (d2 > (rad + 1) * (rad + 1) && d2 <= (rad + 12) * (rad + 12)) {
                    s += static_cast<double>(out.at(x, y, ch)) - img.at(x, y, ch);
                }
            }
        }
        return s;
    };

    // strength 0 -> no-op (function early-returns); strength 0.6 -> visible red glow ring
    fx.halation_strength = 0.0F; fx.halation_threshold = 0.5F;
    const auto out_off = renderer.apply_filmic_halation_bloom(img, zones, masks, fx);
    fx.halation_strength = 0.6F;
    const auto out_on = renderer.apply_filmic_halation_bloom(img, zones, masks, fx);
    const double red_on = ring_added(out_on, 0);
    const double red_off = ring_added(out_off, 0);
    const double blue_on = ring_added(out_on, 2);
    if (!(red_on > red_off + 1.0e-3)) {
        throw std::runtime_error("halation strength must add glow: red_on=" + std::to_string(red_on));
    }
    // (b) lower threshold -> more of the source blooms -> more glow
    fx.halation_threshold = 0.68F; // disc 0.70 barely over -> weak
    const double red_high_thresh = ring_added(renderer.apply_filmic_halation_bloom(img, zones, masks, fx), 0);
    if (!(red_on > red_high_thresh + 1.0e-3)) {
        throw std::runtime_error("lower halation threshold must bloom more: low=" +
            std::to_string(red_on) + " high=" + std::to_string(red_high_thresh));
    }
    // (c) glow is red-orange (added red clearly exceeds added blue)
    if (!(red_on > blue_on * 1.5)) {
        throw std::runtime_error("halation glow must be red-orange: red=" + std::to_string(red_on) + " blue=" + std::to_string(blue_on));
    }
}

void test_scene_referred_tone_zones() {
    // Neutral grays across the tonal range: shadow, mid, highlight, near-white.
    dfee::Image img(4, 1, 3);
    const auto set_px = [&](int x, float v) { img.at(x, 0, 0) = v; img.at(x, 0, 1) = v; img.at(x, 0, 2) = v; };
    set_px(0, 0.05F); set_px(1, 0.18F); set_px(2, 0.60F); set_px(3, 0.90F);

    // Shadows +100 lifts the shadow but leaves the highlight essentially untouched.
    {
        dfee::Image a = img;
        dfee::apply_scene_referred_tone(a, 0, 0, 100, 0, 0, 0);
        assert(a.at(0, 0, 1) > img.at(0, 0, 1) * 1.4F);
        assert(std::fabs(a.at(2, 0, 1) - img.at(2, 0, 1)) < img.at(2, 0, 1) * 0.03F);
    }
    // Highlights +100 lifts the highlight but leaves the shadow essentially untouched.
    {
        dfee::Image a = img;
        dfee::apply_scene_referred_tone(a, 0, 100, 0, 0, 0, 0);
        assert(a.at(2, 0, 1) > img.at(2, 0, 1) * 1.2F);
        assert(std::fabs(a.at(0, 0, 1) - img.at(0, 0, 1)) < img.at(0, 0, 1) * 0.05F);
    }
    // Whites (endpoint) moves the near-white far more than the mid.
    {
        dfee::Image a = img;
        dfee::apply_scene_referred_tone(a, 0, 0, 0, 100, 0, 0);
        const float d_white = a.at(3, 0, 1) / img.at(3, 0, 1);
        const float d_mid = a.at(1, 0, 1) / img.at(1, 0, 1);
        assert(d_white > 1.3F && d_mid < 1.1F && d_white > d_mid);
    }
    // Blacks (endpoint) lifts the shadow far more than the highlight.
    {
        dfee::Image a = img;
        dfee::apply_scene_referred_tone(a, 0, 0, 0, 0, 100, 0);
        const float lift_shadow = a.at(0, 0, 1) - img.at(0, 0, 1);
        const float lift_high = a.at(2, 0, 1) - img.at(2, 0, 1);
        assert(lift_shadow > 0.01F && lift_shadow > lift_high * 5.0F);
    }
}

void test_scene_referred_tone_chroma_symmetry_noop() {
    // No-op when every control is zero (byte-identical passthrough).
    {
        dfee::Image img(3, 1, 3);
        for (int x = 0; x < 3; ++x) { img.at(x, 0, 0) = 0.2F + 0.1F * x; img.at(x, 0, 1) = 0.15F; img.at(x, 0, 2) = 0.05F; }
        dfee::Image a = img;
        dfee::apply_scene_referred_tone(a, 0, 0, 0, 0, 0, 0);
        for (std::size_t i = 0; i < img.pixel_count() * 3U; ++i) {
            assert(a.pixels[i] == img.pixels[i]);
        }
    }
    // Chroma preserved: a uniform region dodge keeps channel ratios (hue + saturation).
    {
        dfee::Image img(1, 1, 3);
        img.at(0, 0, 0) = 0.35F; img.at(0, 0, 1) = 0.12F; img.at(0, 0, 2) = 0.04F;
        dfee::Image a = img;
        dfee::apply_scene_referred_tone(a, 0, 0, 100, 0, 0, 0); // shadows dodge on a mid pixel
        assert(a.at(0, 0, 1) > img.at(0, 0, 1)); // actually changed
        const float rg_in = img.at(0, 0, 0) / img.at(0, 0, 1), rg_out = a.at(0, 0, 0) / a.at(0, 0, 1);
        const float bg_in = img.at(0, 0, 2) / img.at(0, 0, 1), bg_out = a.at(0, 0, 2) / a.at(0, 0, 1);
        assert(std::fabs(rg_in - rg_out) < 1.0e-4F && std::fabs(bg_in - bg_out) < 1.0e-4F);
    }
    // Symmetric EV response: +N and -N are opposite in log2 gain.
    {
        dfee::Image img(1, 1, 3);
        img.at(0, 0, 0) = 0.05F; img.at(0, 0, 1) = 0.05F; img.at(0, 0, 2) = 0.05F;
        dfee::Image up = img, dn = img;
        dfee::apply_scene_referred_tone(up, 0, 0, 60, 0, 0, 0);
        dfee::apply_scene_referred_tone(dn, 0, 0, -60, 0, 0, 0);
        const float lu = std::log2(up.at(0, 0, 1) / img.at(0, 0, 1));
        const float ld = std::log2(dn.at(0, 0, 1) / img.at(0, 0, 1));
        assert(std::fabs(lu + ld) < 1.0e-3F);
    }
}

void test_highlight_rolloff_compresses_highlights() {
    const int w = 64;
    dfee::Image ramp(w, 1, 3);
    for (int x = 0; x < w; ++x) {
        const float v = static_cast<float>(x) / static_cast<float>(w - 1);
        ramp.at(x, 0, 0) = v; ramp.at(x, 0, 1) = v; ramp.at(x, 0, 2) = v;
    }

    dfee::FilmResponsePlan plan;
    plan.toe_strength = 0.30F;
    plan.shoulder_strength = 0.50F;
    plan.midtone_density = 1.0F;          // neutral midtone gamma
    plan.highlight_rolloff_knee = 0.70F;
    const dfee::FilmRenderer renderer;

    plan.highlight_rolloff_amount = 0.0F; // off
    const auto off = renderer.apply_film_tone_response(ramp, plan);
    plan.highlight_rolloff_amount = 0.8F; // on
    const auto on = renderer.apply_film_tone_response(ramp, plan);

    const int bright = w - 2; // ~0.98 -> maps above the knee
    const int mid = w / 5;    // ~0.20 -> maps below the knee
    // Highlights are compressed downward; shadows/mids are untouched.
    if (!(on.at(bright, 0, 1) < off.at(bright, 0, 1) - 1.0e-4F)) {
        throw std::runtime_error("highlight rolloff must compress bright values: on=" +
            std::to_string(on.at(bright, 0, 1)) + " off=" + std::to_string(off.at(bright, 0, 1)));
    }
    if (!(std::fabs(on.at(mid, 0, 1) - off.at(mid, 0, 1)) < 1.0e-5F)) {
        throw std::runtime_error("highlight rolloff must leave mid/shadow tones unchanged");
    }
    // Tone curve stays monotonic across the ramp with rolloff engaged.
    for (int x = 1; x < w; ++x) {
        assert(on.at(x, 0, 1) >= on.at(x - 1, 0, 1) - 1.0e-5F);
    }
    // amount 0 with a knee set is a no-op (filmic_v2/parity safety).
    plan.highlight_rolloff_amount = 0.0F;
    const auto off2 = renderer.apply_film_tone_response(ramp, plan);
    assert(off2.at(bright, 0, 1) == off.at(bright, 0, 1));
}

void test_solver_auto_exposure_protects_highlights() {
    const std::filesystem::path repo_root = DFEE_REPO_ROOT;
    const auto stock = dfee::load_film_stock_profile(
        repo_root / "profiles" / "stocks" / "portra_400.yaml");
    const dfee::RenderPlanSolver solver;

    auto make_input = [](float midtone, float p99) {
        dfee::SolverInput in;
        in.tonal_distribution.tonal_skew = "normal";
        in.tonal_distribution.dynamic_range_stops = 8.0F;
        in.tonal_distribution.midtone_anchor = midtone;
        in.tonal_distribution.highlight_headroom = 0.05F;
        in.tonal_distribution.luma_p95 = std::max(0.0F, p99 - 0.03F);
        in.tonal_distribution.luma_p99 = p99;
        in.camera_input_bias = dfee::CameraBiasAnalysis{.neutral_confidence = 0.9F};
        in.raw_iso = 400;
        return in;
    };

    dfee::SolverControls base;
    base.exposure_intent = "Auto";
    base.adaptive = false;
    dfee::SolverControls v3 = base; v3.subtractive_pipeline = true;
    dfee::SolverControls v2 = base; v2.subtractive_pipeline = false;

    // High-key scene (dark mids wanting a big push, but highlights already near the
    // ceiling) -> v3 caps the upward push; parity/filmic_v2 does not (byte-identical).
    const auto bright = make_input(0.07F, 0.92F);
    const float e_v3 = solver.solve(bright, stock, v3).pre_film_normalization.exposure_compensation_stops;
    const float e_v2 = solver.solve(bright, stock, v2).pre_film_normalization.exposure_compensation_stops;
    if (!(e_v2 > 0.2F)) {
        throw std::runtime_error("v2 auto exposure should push a dark-mid scene up: " + std::to_string(e_v2));
    }
    if (!(e_v3 < e_v2 - 0.1F && e_v3 <= 0.05F)) {
        throw std::runtime_error("v3 must cap the upward push when highlights are bright: v3=" +
            std::to_string(e_v3) + " v2=" + std::to_string(e_v2));
    }

    // Modest push with real highlight headroom -> the cap does not bite (v3 == v2), so the
    // protection only engages when highlights would actually clip.
    const auto headroom = make_input(0.14F, 0.30F);
    const float d_v3 = solver.solve(headroom, stock, v3).pre_film_normalization.exposure_compensation_stops;
    const float d_v2 = solver.solve(headroom, stock, v2).pre_film_normalization.exposure_compensation_stops;
    if (!(d_v3 > 0.05F)) {
        throw std::runtime_error("v3 should still brighten a scene with highlight headroom: " + std::to_string(d_v3));
    }
    if (std::fabs(d_v3 - d_v2) > 1.0e-4F) {
        throw std::runtime_error("highlight cap must not change exposure when highlights have headroom");
    }
}

void test_solver_tone_steering() {
    const std::filesystem::path repo_root = DFEE_REPO_ROOT;
    const auto stock = dfee::load_film_stock_profile(
        repo_root / "profiles" / "stocks" / "portra_400.yaml");
    const dfee::RenderPlanSolver solver;

    auto make_input = [](float dr) {
        dfee::SolverInput in;
        in.tonal_distribution.tonal_skew = "normal";
        in.tonal_distribution.dynamic_range_stops = dr;
        in.tonal_distribution.midtone_anchor = 0.18F;
        in.tonal_distribution.highlight_headroom = 0.25F;
        in.tonal_distribution.shadow_depth = 0.05F;
        in.tonal_distribution.luma_p95 = 0.72F;
        in.camera_input_bias = dfee::CameraBiasAnalysis{.neutral_confidence = 0.9F};
        in.raw_iso = 400;
        return in;
    };
    const auto input_normal = make_input(8.0F);   // normal DR
    const auto input_flat = make_input(13.0F);    // flat / high-DR / log-like

    dfee::SolverControls sub;  // filmic_v3, adaptive, controls neutral
    sub.subtractive_pipeline = true;
    sub.adaptive = true;
    sub.film_contrast = 100.0F;
    sub.highlight_rolloff = 100.0F;

    const auto plan_normal = solver.solve(input_normal, stock, sub);
    const auto plan_flat = solver.solve(input_flat, stock, sub);

    // Normal DR -> adaptive factor ~1.0; flat -> factor > 1 (more filmic contrast).
    if (std::fabs(plan_normal.film_response.tone_adaptive_factor - 1.0F) > 0.02F) {
        throw std::runtime_error("normal-DR tone_adaptive_factor must be ~1.0, got " +
            std::to_string(plan_normal.film_response.tone_adaptive_factor));
    }
    if (!(plan_flat.film_response.tone_adaptive_factor > 1.05F)) {
        throw std::runtime_error("flat/high-DR tone_adaptive_factor must be > 1, got " +
            std::to_string(plan_flat.film_response.tone_adaptive_factor));
    }
    // Flat scene resolves higher midtone contrast than normal (adaptive strengthens it).
    if (!(plan_flat.film_response.midtone_density > plan_normal.film_response.midtone_density + 1.0e-4F)) {
        throw std::runtime_error("flat scene must resolve higher midtone_density under adaptive tone");
    }

    // parity/filmic_v2 (subtractive_pipeline=false): no steering even on a flat scene.
    dfee::SolverControls parity = sub;
    parity.subtractive_pipeline = false;
    const auto plan_flat_parity = solver.solve(input_flat, stock, parity);
    if (std::fabs(plan_flat_parity.film_response.tone_adaptive_factor - 1.0F) > 1.0e-6F) {
        throw std::runtime_error("parity tone_adaptive_factor must be exactly 1.0");
    }
    if (std::fabs(plan_flat_parity.film_response.midtone_density - plan_normal.film_response.midtone_density) > 1.0e-5F) {
        throw std::runtime_error("parity midtone_density must equal the unmodulated stock value");
    }

    // filmic_v3 with adaptive off + controls neutral: tone == stock (no steering).
    dfee::SolverControls noadapt = sub;
    noadapt.adaptive = false;
    const auto plan_flat_noadapt = solver.solve(input_flat, stock, noadapt);
    if (std::fabs(plan_flat_noadapt.film_response.tone_adaptive_factor - 1.0F) > 1.0e-6F) {
        throw std::runtime_error("adaptive-off tone_adaptive_factor must be 1.0");
    }
    if (std::fabs(plan_flat_noadapt.film_response.midtone_density - plan_flat_parity.film_response.midtone_density) > 1.0e-5F) {
        throw std::runtime_error("adaptive-off + neutral controls must equal stock midtone_density");
    }
}

void test_subtractive_density_darkens_saturated_preserves_hue_and_neutrals() {
    const auto oklch_to_rgb_arr = [](float l, float c, float h) -> std::array<float, 3> {
        const float a = c * std::cos(h);
        const float b = c * std::sin(h);
        const dfee::Image tmp = dfee::oklab_to_rgb([&]() {
            dfee::Image lab(1, 1, 3);
            lab.at(0, 0, 0) = l; lab.at(0, 0, 1) = a; lab.at(0, 0, 2) = b;
            return lab;
        }());
        return {tmp.at(0, 0, 0), tmp.at(0, 0, 1), tmp.at(0, 0, 2)};
    };

    // pixel 0: saturated (bright), pixel 1: saturated deep-shadow (below limiter),
    // pixel 2: neutral gray.
    dfee::Image rgb(3, 1, 3);
    const auto p0 = oklch_to_rgb_arr(0.60F, 0.20F, 0.5F);
    const auto p1 = oklch_to_rgb_arr(0.08F, 0.05F, 0.5F); // in-gamut dark pixel below the limiter
    const auto p2 = oklch_to_rgb_arr(0.60F, 0.00F, 0.0F);
    for (int k = 0; k < 3; ++k) { rgb.at(0, 0, k) = p0[static_cast<std::size_t>(k)]; }
    for (int k = 0; k < 3; ++k) { rgb.at(1, 0, k) = p1[static_cast<std::size_t>(k)]; }
    for (int k = 0; k < 3; ++k) { rgb.at(2, 0, k) = p2[static_cast<std::size_t>(k)]; }

    const auto lch_of = [](const dfee::Image& img) {
        return dfee::oklab_to_oklch(dfee::rgb_to_oklab(img));
    };
    const auto base = lch_of(rgb);

    dfee::FilmResponsePlan response;
    response.stock_type = "color_negative";
    response.density_strength = 0.6F;
    response.density_low_luma_limit = 0.10F;

    const dfee::FilmRenderer renderer;

    // density = 0 -> byte-identical no-op
    response.film_color_density = 0.0F;
    const auto out0 = renderer.apply_subtractive_density(rgb, response);
    for (std::size_t i = 0; i < rgb.pixels.size(); ++i) {
        if (out0.pixels[i] != rgb.pixels[i]) {
            throw std::runtime_error("film_color_density=0 must be a byte-identical no-op");
        }
    }

    // density = 100 (stock default effect)
    response.film_color_density = 100.0F;
    const auto out100 = renderer.apply_subtractive_density(rgb, response);
    const auto lch100 = lch_of(out100);

    // saturated pixel 0 must darken; hue and chroma preserved
    if (!(lch100.at(0, 0, 0) < base.at(0, 0, 0) - 1.0e-3F)) {
        throw std::runtime_error("saturated pixel L must decrease under density");
    }
    // Chroma is preserved in OKLCh intent; sRGB round-trip may clamp slightly at
    // the darker luminance, so require density does not *desaturate* (chroma
    // stays within 12% of baseline) rather than exact equality.
    if (!(lch100.at(0, 0, 1) > base.at(0, 0, 1) * 0.88F) ||
        !(lch100.at(0, 0, 1) < base.at(0, 0, 1) * 1.12F)) {
        throw std::runtime_error("density must not desaturate the saturated pixel");
    }
    if (std::fabs(lch100.at(0, 0, 2) - base.at(0, 0, 2)) > 3.0e-3F) {
        throw std::runtime_error("density must preserve hue of the saturated pixel");
    }
    // deep-shadow pixel 1 protected by the limiter (L ~ unchanged)
    if (std::fabs(lch100.at(1, 0, 0) - base.at(1, 0, 0)) > 5.0e-4F) {
        throw std::runtime_error("low-luma limiter must protect deep-shadow L");
    }
    // neutral pixel 2 unchanged
    for (int k = 0; k < 3; ++k) {
        if (std::fabs(out100.at(2, 0, k) - rgb.at(2, 0, k)) > 1.0e-4F) {
            throw std::runtime_error("neutral pixel must be unchanged by density");
        }
    }

    // density = 200 darkens the saturated pixel more than 100
    response.film_color_density = 200.0F;
    const auto out200 = renderer.apply_subtractive_density(rgb, response);
    const auto lch200 = lch_of(out200);
    if (!(lch200.at(0, 0, 0) < lch100.at(0, 0, 0) - 1.0e-3F)) {
        throw std::runtime_error("film_color_density=200 must darken more than 100");
    }
}

// ---------------------------------------------------------------------------
// Task 8: M7-003F — Synthetic zone/hue fixture (multi-control integration)
// ---------------------------------------------------------------------------

void test_color_character_synthetic_zone_hue_fixture() {
    // 12-pixel 1-row image:
    //   pixel 0         — neutral gray (palette separation neutrality)
    //   pixels 1–4     — hue wheel at midtone luminance (saturated R, G, B, cyan-ish)
    //   pixels 5–6     — shadow-zone chromatic pixels (OKLab L ≈ 0.15)
    //   pixels 7–8     — midtone chromatic pixels (moderate luminance)
    //   pixels 9–10    — highlight-zone chromatic pixels (high luminance)
    //   pixels 11      — extra chromatic pixel (mid-high luminance)
    //
    // pixel 5/6 are set so OKLab L < sh_rolloff_start=0.18 (shadow zone active).
    // pixel 9/10 are set so OKLab L >> hi_rolloff_start=0.70 (highlight zone active).

    dfee::Image rgb(12, 1, 3);
    rgb.pixels = {
        // pixel 0 — neutral gray
        0.45F, 0.45F, 0.45F,
        // pixel 1 — saturated red at midtone
        0.70F, 0.30F, 0.25F,
        // pixel 2 — saturated green at midtone
        0.25F, 0.70F, 0.30F,
        // pixel 3 — saturated blue at midtone
        0.25F, 0.30F, 0.70F,
        // pixel 4 — saturated cyan-ish at midtone
        0.25F, 0.65F, 0.65F,
        // pixel 5 — shadow chromatic (very low luminance — below sh_rolloff_start)
        0.008F, 0.004F, 0.002F,
        // pixel 6 — shadow chromatic (very low luminance — below sh_rolloff_start)
        0.006F, 0.002F, 0.004F,
        // pixel 7 — midtone chromatic
        0.42F, 0.28F, 0.18F,
        // pixel 8 — midtone chromatic
        0.38F, 0.22F, 0.35F,
        // pixel 9 — highlight chromatic (high luminance — above hi_rolloff_start=0.70)
        0.95F, 0.72F, 0.55F,
        // pixel 10 — highlight chromatic (high luminance)
        0.92F, 0.80F, 0.60F,
        // pixel 11 — mid-high chromatic
        0.60F, 0.45F, 0.30F,
    };

    const auto luminance = dfee::compute_luminance(rgb);
    const dfee::ImageStateAnalyzer analyzer;
    const auto zones = analyzer.generate_zone_masks(luminance, 0.18F);

    // Build a reusable response plan with all color character sensitivities active
    dfee::FilmResponsePlan base_response;
    base_response.stock_type = "color_negative";
    base_response.film_color = 100.0F;
    base_response.highlight_desaturation = 0.6F;
    base_response.highlight_hold_sensitivity = 1.0F;
    base_response.shadow_retention_sensitivity = 1.0F;
    base_response.emulsion_density_sensitivity = 1.0F;
    base_response.palette_range_sensitivity = 1.0F;
    base_response.chroma_coupling = {
        {"hi_rolloff_start", 0.70F},
        {"hi_rolloff_rate", 2.0F},
        {"hi_compression", 0.55F},
        {"sh_rolloff_start", 0.18F},
        {"sh_compression", 0.45F},
        {"hi_hue_conv_rad", 0.28F},
        {"hi_hue_conv_str", 0.18F},
    };
    base_response.chroma_boost = 1.0F;

    const dfee::FilmRenderer renderer;

    // -----------------------------------------------------------------------
    // (a) Highlight Color Hold: HCH=+100 must increase highlight chroma more
    //     than shadow chroma (locality: highlight gain > 10x shadow leakage)
    // -----------------------------------------------------------------------
    {
        dfee::FilmResponsePlan resp = base_response;
        resp.highlight_color_hold = 0.0F;
        const auto baseline = renderer.apply_color_response_and_coupling(rgb, zones, resp);

        resp.highlight_color_hold = 100.0F;
        const auto held = renderer.apply_color_response_and_coupling(rgb, zones, resp);

        const float hi_chroma_base  = read_chroma(baseline, 9);
        const float hi_chroma_held  = read_chroma(held,     9);
        const float sh_chroma_base  = read_chroma(baseline, 5);
        const float sh_chroma_held  = read_chroma(held,     5);

        const float hi_delta = hi_chroma_held - hi_chroma_base;
        const float sh_delta = std::fabs(sh_chroma_held - sh_chroma_base);

        if (!(hi_delta > 0.0F)) {
            throw std::runtime_error(
                "synthetic fixture (a): HCH=+100 did not increase highlight chroma: "
                "held=" + std::to_string(hi_chroma_held) +
                " baseline=" + std::to_string(hi_chroma_base));
        }
        if (!(hi_delta > sh_delta * 10.0F)) {
            throw std::runtime_error(
                "synthetic fixture (a): HCH locality failed: hi_delta=" +
                std::to_string(hi_delta) + " must be >10x sh_delta=" + std::to_string(sh_delta));
        }
    }

    // -----------------------------------------------------------------------
    // (b) Shadow Color Retention: SCR=+100 must increase shadow chroma more
    //     than highlight chroma (locality: shadow gain > 10x highlight leakage)
    // -----------------------------------------------------------------------
    {
        dfee::FilmResponsePlan resp = base_response;
        resp.shadow_color_retention = 0.0F;
        const auto baseline = renderer.apply_color_response_and_coupling(rgb, zones, resp);

        resp.shadow_color_retention = 100.0F;
        const auto retained = renderer.apply_color_response_and_coupling(rgb, zones, resp);

        const float sh_chroma_base = read_chroma(baseline, 5);
        const float sh_chroma_ret  = read_chroma(retained, 5);
        const float hi_chroma_base = read_chroma(baseline, 9);
        const float hi_chroma_ret  = read_chroma(retained, 9);

        const float sh_delta = sh_chroma_ret - sh_chroma_base;
        const float hi_delta = std::fabs(hi_chroma_ret - hi_chroma_base);

        if (!(sh_delta > 0.0F)) {
            throw std::runtime_error(
                "synthetic fixture (b): SCR=+100 did not increase shadow chroma: "
                "retained=" + std::to_string(sh_chroma_ret) +
                " baseline=" + std::to_string(sh_chroma_base));
        }
        if (!(sh_delta > hi_delta * 10.0F)) {
            throw std::runtime_error(
                "synthetic fixture (b): SCR locality failed: sh_delta=" +
                std::to_string(sh_delta) + " must be >10x hi_delta=" + std::to_string(hi_delta));
        }

        // No black lift: shadow pixel lightness must not change
        const float baseline_shadow_L = read_lightness(baseline, 5);
        const float active_shadow_L   = read_lightness(retained, 5);
        if (std::fabs(active_shadow_L - baseline_shadow_L) >= 1.0e-5F) {
            throw std::runtime_error(
                "synthetic fixture (b): SCR=+100 changed shadow lightness (black lift!): "
                "active_L=" + std::to_string(active_shadow_L) +
                " baseline_L=" + std::to_string(baseline_shadow_L));
        }
    }

    // -----------------------------------------------------------------------
    // (c) Palette Separation: neutral gray pixel (pixel 0) must be unchanged
    //     (chroma == 0, so hue shift gain is 0)
    // -----------------------------------------------------------------------
    {
        dfee::FilmResponsePlan resp = base_response;
        resp.palette_range = 0.0F;
        const auto baseline = renderer.apply_color_response_and_coupling(rgb, zones, resp);

        resp.palette_range = 100.0F;
        const auto separated = renderer.apply_color_response_and_coupling(rgb, zones, resp);

        const float dr = std::fabs(separated.at(0, 0, 0) - baseline.at(0, 0, 0));
        const float dg = std::fabs(separated.at(0, 0, 1) - baseline.at(0, 0, 1));
        const float db = std::fabs(separated.at(0, 0, 2) - baseline.at(0, 0, 2));

        if (dr > 1.0e-5F || dg > 1.0e-5F || db > 1.0e-5F) {
            throw std::runtime_error(
                "synthetic fixture (c): palette_range=+100 changed neutral gray pixel: "
                "dR=" + std::to_string(dr) + " dG=" + std::to_string(dg) +
                " dB=" + std::to_string(db));
        }
    }

    // -----------------------------------------------------------------------
    // (d) Determinism: same render with all four controls at 50.0 → byte-identical
    // -----------------------------------------------------------------------
    {
        dfee::FilmResponsePlan resp = base_response;
        resp.highlight_color_hold      = 50.0F;
        resp.shadow_color_retention    = 50.0F;
        resp.palette_range        = 50.0F;
        resp.emulsion_color_density    = 50.0F;

        const auto first  = renderer.apply_color_response_and_coupling(rgb, zones, resp);
        const auto second = renderer.apply_color_response_and_coupling(rgb, zones, resp);

        if (first.pixels.size() != second.pixels.size()) {
            throw std::runtime_error(
                "synthetic fixture (d): determinism check — output size mismatch");
        }
        for (std::size_t i = 0; i < first.pixels.size(); ++i) {
            if (first.pixels[i] != second.pixels[i]) {
                throw std::runtime_error(
                    "synthetic fixture (d): determinism check failed at pixel element " +
                    std::to_string(i) +
                    ": first=" + std::to_string(first.pixels[i]) +
                    " second=" + std::to_string(second.pixels[i]));
            }
        }
    }
}

void test_filmic_v3_version_is_supported_and_subtractive() {
    if (dfee::EngineSession::is_effect_pipeline_supported("filmic_v3") != true) {
        throw std::runtime_error("filmic_v3 must be a supported effect_pipeline_version");
    }
    if (dfee::EngineSession::is_effect_pipeline_supported("filmic_v2") != true) {
        throw std::runtime_error("filmic_v2 must remain supported");
    }
    if (dfee::EngineSession::is_effect_pipeline_supported("parity_v1") != true) {
        throw std::runtime_error("parity_v1 must remain supported");
    }
    if (dfee::EngineSession::is_effect_pipeline_supported("bogus_v9") != false) {
        throw std::runtime_error("unknown effect_pipeline_version must be rejected");
    }
}

}  // namespace

int main() {
#if defined(_WIN32) && defined(_DEBUG)
    // Route Debug CRT assert/error reporting to stderr instead of a modal dialog so
    // headless test runs fail fast (non-zero exit) rather than blocking on a popup.
    for (int report_type : {_CRT_WARN, _CRT_ERROR, _CRT_ASSERT}) {
        _CrtSetReportMode(report_type, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(report_type, _CRTDBG_FILE_STDERR);
    }
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
    try {
        test_oklab_roundtrip();
        test_zone_partition();
        test_tonal_analysis();
        test_color_analysis();
        test_spatial_analysis();
        test_camera_bias_estimator();
        test_render_plan_solver();
        test_render_plan_solver_rich_grain_profile_fields();
        test_pre_film_normalization();
        test_panchromatic_conversion();
        test_film_tone_response();
        test_color_response();
        test_yellow_green_muting();
        test_luminance_chroma_coupling();
        test_acutance_shaping();
        test_clarity();
        test_texture();
        test_dehaze();
        test_halation_bloom();
        test_filmic_halation_bloom_compresses_and_diffuses_highlights();
        test_filmic_halation_profile_geometry_and_colour();
        test_film_grain_determinism();
        test_filmic_grain_density_response_and_stock_character();
        test_filmic_grain_avoids_low_frequency_blotches();
        test_filmic_grain_roughness_does_not_become_pixel_noise();
        test_filmic_grain_profile_placement_and_texture_masking();
        test_print_finish();
        test_profile_loading();
        test_raw_failure_paths();
        test_color_character_request_fields_default_to_neutral_zero();
        test_loader_accepts_optional_color_character_group();
        test_highlight_color_hold_increases_only_highlight_chroma();
        test_shadow_color_retention_increases_shadow_chroma_without_lifting_blacks();
        test_filmic_v3_version_is_supported_and_subtractive();
        test_emulsion_color_density_increases_mid_saturation_chroma();
        test_solver_color_character_family_defaults();
        test_solver_density_defaults();
        test_solver_compression_defaults();
        test_solver_tone_steering();
        test_solver_auto_exposure_protects_highlights();
        test_scene_referred_tone_zones();
        test_scene_referred_tone_chroma_symmetry_noop();
        test_highlight_rolloff_compresses_highlights();
        test_filmic_grain_uniform_softlight();
        test_halation_threshold_and_strength();
        test_color_compression_compresses_high_chroma_preserves_neutral();
        test_color_compression_leans_neighbours_preserves_neutral();
        test_subtractive_density_darkens_saturated_preserves_hue_and_neutrals();
        test_color_character_synthetic_zone_hue_fixture();
        std::cout << "dfee_tests passed\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "dfee_tests exception: " << ex.what() << "\n";
        return 1;
    }
}
