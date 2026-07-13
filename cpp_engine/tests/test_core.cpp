#include "dfee/analyzer.hpp"
#include "dfee/bias.hpp"
#include "dfee/color_spaces.hpp"
#include "dfee/image.hpp"
#include "dfee/profile.hpp"
#include "dfee/renderer.hpp"
#include "dfee/session.hpp"
#include "dfee/solver.hpp"
#include "dfee/version.hpp"

#include <array>
#include <cassert>
#include <cmath>
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
    const auto masked = renderer.apply_filmic_grain(rgb, masks, effects);
    effects.grain_texture_masking = 0.0F;
    const auto unmasked = renderer.apply_filmic_grain(rgb, masks, effects);

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

    assert(mean_delta(masked, 0, 16) < mean_delta(unmasked, 0, 16) * 0.20);
    assert(mean_delta(masked, 16, 32) > mean_delta(masked, 32, 48));
}

void test_filmic_halation_profile_geometry_and_colour() {
    dfee::Image rgb(64, 64, 3);
    for (int y = 0; y < rgb.height; ++y) {
        for (int x = 0; x < rgb.width; ++x) {
            rgb.at(x, y, 0) = 0.02F;
            rgb.at(x, y, 1) = 0.02F;
            rgb.at(x, y, 2) = 0.02F;
        }
    }
    rgb.at(32, 32, 0) = 1.0F;
    rgb.at(32, 32, 1) = 0.95F;
    rgb.at(32, 32, 2) = 0.88F;

    dfee::LuminanceImage luminance = dfee::compute_luminance(rgb);
    const dfee::ImageStateAnalyzer analyzer;
    const auto zones = analyzer.generate_zone_masks(luminance, 0.18F);
    dfee::SpatialMasks masks;
    masks.halation_source_mask = dfee::LuminanceImage(rgb.width, rgb.height);
    masks.halation_receiver_mask = dfee::LuminanceImage(rgb.width, rgb.height);
    masks.halation_source_mask.at(32, 32) = 1.0F;
    for (float& value : masks.halation_receiver_mask.values) {
        value = 1.0F;
    }

    dfee::MaterialEffectsPlan effects;
    effects.halation_strength = 0.8F;
    effects.bloom_strength = 0.0F;
    effects.halation_trigger = "specular_only";
    effects.halation_radius_inner = 16.0F;
    effects.halation_radius_outer = 40.0F;
    effects.halation_warm_core = {1.0F, 0.45F, 0.10F};
    effects.halation_red_fringe = {1.0F, 0.05F, 0.00F};

    const dfee::FilmRenderer renderer;
    const auto warm = renderer.apply_filmic_halation_bloom(rgb, zones, masks, effects);
    effects.halation_warm_core = {0.0F, 0.0F, 1.0F};
    effects.halation_red_fringe = {0.0F, 0.0F, 1.0F};
    const auto blue = renderer.apply_filmic_halation_bloom(rgb, zones, masks, effects);

    const int sample_x = 38;
    const int sample_y = 32;
    const float warm_red_delta = warm.at(sample_x, sample_y, 0) - rgb.at(sample_x, sample_y, 0);
    const float warm_blue_delta = warm.at(sample_x, sample_y, 2) - rgb.at(sample_x, sample_y, 2);
    const float blue_red_delta = blue.at(sample_x, sample_y, 0) - rgb.at(sample_x, sample_y, 0);
    const float blue_blue_delta = blue.at(sample_x, sample_y, 2) - rgb.at(sample_x, sample_y, 2);
    assert(warm_red_delta > warm_blue_delta);
    assert(blue_blue_delta > blue_red_delta);
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

    assert(luma(adjusted, 48, 48) < luma(rgb, 48, 48));
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
    assert(stocks.size() >= 27U);
    for (const auto& active_stock : stocks) {
        const auto plan = solver.solve(input, active_stock);
        assert(std::isfinite(plan.film_response.yellow_green_muting));
        assert(std::isfinite(plan.material_effects.grain_texture_masking));
        assert(plan.material_effects.grain_peak_zone == active_stock.string_values.at("grain.peak_zone"));
        assert(plan.material_effects.halation_trigger == active_stock.string_values.at("halation.trigger"));
        assert(std::fabs(plan.material_effects.halation_radius_inner - active_stock.numeric_values.at("halation.radius_inner")) < 1.0e-5F);
        assert(std::fabs(plan.material_effects.halation_radius_outer - active_stock.numeric_values.at("halation.radius_outer")) < 1.0e-5F);
        assert(std::fabs(plan.film_response.yellow_green_muting - active_stock.numeric_values.at("hue_saturation_response.yellow_green_muting")) < 1.0e-5F);
        assert(std::fabs(plan.film_response.pan_weight_r - active_stock.numeric_values.at("color_response.pan_weight_r")) < 1.0e-5F);
        assert(std::fabs(plan.film_response.pan_weight_g - active_stock.numeric_values.at("color_response.pan_weight_g")) < 1.0e-5F);
        assert(std::fabs(plan.film_response.pan_weight_b - active_stock.numeric_values.at("color_response.pan_weight_b")) < 1.0e-5F);
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

void test_color_character_request_fields_default_to_neutral_zero() {
    dfee::NativePreviewRenderRequest request;
    require_close(request.highlight_color_hold, 0.0F, 1.0e-6F);
    require_close(request.shadow_color_retention, 0.0F, 1.0e-6F);
    require_close(request.palette_separation, 0.0F, 1.0e-6F);
    require_close(request.emulsion_color_density, 0.0F, 1.0e-6F);

    dfee::SolverControls controls;
    controls.highlight_color_hold = request.highlight_color_hold;
    controls.shadow_color_retention = request.shadow_color_retention;
    controls.palette_separation = request.palette_separation;
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
        out << "    separation_sensitivity: 0.7\n";
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
    if (!profile.numeric_values.contains("color_character.palette.separation_sensitivity")) {
        throw std::runtime_error("color_character.palette.separation_sensitivity not found in profile");
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

// Helper: signed shortest-arc delta from h toward anchor a (radians)
static float hue_delta(float h, float a) {
    constexpr float kPi = std::numbers::pi_v<float>;
    float d = std::fmod((a - h) + kPi, 2.0F * kPi) - kPi;
    return d;
}

void test_palette_separation_neutral_pixel_preserved() {
    // A near-gray pixel (very low chroma) must be byte-unchanged when palette_separation=+100.
    // Use equal R=G=B: perfectly neutral, OKLCh c == 0.
    dfee::Image rgb(1, 1, 3);
    rgb.pixels = {0.45F, 0.45F, 0.45F};

    const auto zones = make_flat_zone_masks(rgb);

    dfee::FilmResponsePlan response;
    response.stock_type = "color_negative";
    response.film_color = 100.0F;
    response.palette_separation = 100.0F;
    response.palette_separation_sensitivity = 1.0F;
    // palette_anchors left empty → default six anchors

    const dfee::FilmRenderer renderer;
    const auto out = renderer.apply_color_response_and_coupling(rgb, zones, response);

    // Neutral pixel: chroma is exactly 0, so g_c == 0. Hue shift must be 0.
    // The output pixel must be indistinguishable from the baseline (palette_separation=0).
    response.palette_separation = 0.0F;
    const auto baseline = renderer.apply_color_response_and_coupling(rgb, zones, response);

    const float delta_r = std::fabs(out.at(0, 0, 0) - baseline.at(0, 0, 0));
    const float delta_g = std::fabs(out.at(0, 0, 1) - baseline.at(0, 0, 1));
    const float delta_b = std::fabs(out.at(0, 0, 2) - baseline.at(0, 0, 2));

    if (delta_r > 1.0e-5F || delta_g > 1.0e-5F || delta_b > 1.0e-5F) {
        throw std::runtime_error(
            "palette_separation=+100 changed a neutral pixel: dR=" + std::to_string(delta_r) +
            " dG=" + std::to_string(delta_g) + " dB=" + std::to_string(delta_b));
    }
}

void test_palette_separation_direction() {
    // A saturated pixel at a hue clearly offset between two default anchors (0 and π/3 ≈ 1.047).
    // We need h_baseline comfortably closer to one anchor than the other so the nearest is unambiguous.
    // We place the pixel at h ≈ 0.3 rad (between 0 and π/3), nearest anchor = 0.
    // That means delta_to_nearest < 0 (anchor is clockwise), sin(delta) < 0, n_sep=+1 → h decreases.
    // For n_sep = -1 → h increases (away from 0 toward π/3).
    //
    // Build the pixel via OKLCh → OKLab → RGB at L=0.60, C=0.15, h=0.30 rad.
    const float kL = 0.60F;
    const float kC = 0.15F;
    const float kH = 0.30F;  // radians — nearest default anchor is 0 (distance 0.30), π/3 is 0.747 away
    const float a_in = kC * std::cos(kH);
    const float b_in = kC * std::sin(kH);
    // oklab → linear rgb (same matrix as renderer)
    const float lp = kL + 0.3963377774F * a_in + 0.2158017574F * b_in;
    const float mp = kL - 0.1055613458F * a_in - 0.0638541728F * b_in;
    const float sp = kL - 0.0894841775F * a_in - 1.2914855480F * b_in;
    const float lv = lp * lp * lp;
    const float mv = mp * mp * mp;
    const float sv = sp * sp * sp;
    const float r_px = std::clamp(4.0767416621F * lv - 3.3077115913F * mv + 0.2309699292F * sv, 0.0F, 1.0F);
    const float g_px = std::clamp(-1.2684380046F * lv + 2.6097574011F * mv - 0.3413193965F * sv, 0.0F, 1.0F);
    const float b_px = std::clamp(-0.0041960863F * lv - 0.7034186147F * mv + 1.7076147010F * sv, 0.0F, 1.0F);

    dfee::Image rgb(1, 1, 3);
    rgb.pixels = {r_px, g_px, b_px};

    const auto zones = make_flat_zone_masks(rgb);

    dfee::FilmResponsePlan response;
    response.stock_type = "color_negative";
    response.film_color = 100.0F;
    response.palette_separation_sensitivity = 1.0F;
    // palette_anchors empty → default 0, π/3, 2π/3, π, 4π/3, 5π/3

    // Baseline
    response.palette_separation = 0.0F;
    const dfee::FilmRenderer renderer;
    const auto baseline = renderer.apply_color_response_and_coupling(rgb, zones, response);
    const float h_baseline = read_hue(baseline, 0);

    // The pixel must have enough chroma to be affected (well above kPaletteChromaHi=0.06)
    const float chroma = read_chroma(baseline, 0);
    if (chroma < 0.06F) {
        throw std::runtime_error(
            "direction test pixel has too little chroma to trigger palette separation: c=" + std::to_string(chroma));
    }

    // Find nearest default anchor to the baseline hue
    constexpr float kPi = std::numbers::pi_v<float>;
    const std::array<float, 6> default_anchors = {
        0.0F, kPi / 3.0F, 2.0F * kPi / 3.0F, kPi, 4.0F * kPi / 3.0F, 5.0F * kPi / 3.0F
    };
    float nearest_anchor = default_anchors[0];
    float nearest_dist = std::fabs(hue_delta(h_baseline, default_anchors[0]));
    for (const float anchor : default_anchors) {
        const float dist = std::fabs(hue_delta(h_baseline, anchor));
        if (dist < nearest_dist) {
            nearest_dist = dist;
            nearest_anchor = anchor;
        }
    }

    if (nearest_dist < 0.1F) {
        throw std::runtime_error(
            "direction test: baseline hue " + std::to_string(h_baseline) +
            " is too close to nearest anchor " + std::to_string(nearest_anchor) +
            " (dist=" + std::to_string(nearest_dist) + "); test premise broken");
    }

    // At +100: should attract toward nearest anchor
    response.palette_separation = 100.0F;
    const auto attracted = renderer.apply_color_response_and_coupling(rgb, zones, response);
    const float h_attracted = read_hue(attracted, 0);

    // At −100: should repel from nearest anchor
    response.palette_separation = -100.0F;
    const auto repelled = renderer.apply_color_response_and_coupling(rgb, zones, response);
    const float h_repelled = read_hue(repelled, 0);

    const float dist_attracted = std::fabs(hue_delta(h_attracted, nearest_anchor));
    const float dist_repelled  = std::fabs(hue_delta(h_repelled,  nearest_anchor));

    // +100 must move hue closer to the nearest anchor
    if (!(dist_attracted < nearest_dist - 1.0e-5F)) {
        throw std::runtime_error(
            "palette_separation=+100 did not attract hue toward nearest anchor: "
            "h_base=" + std::to_string(h_baseline) +
            " h_attracted=" + std::to_string(h_attracted) +
            " nearest_anchor=" + std::to_string(nearest_anchor) +
            " dist_baseline=" + std::to_string(nearest_dist) +
            " dist_attracted=" + std::to_string(dist_attracted));
    }

    // −100 must move hue further from the nearest anchor
    if (!(dist_repelled > nearest_dist + 1.0e-5F)) {
        throw std::runtime_error(
            "palette_separation=-100 did not repel hue away from anchor: "
            "h_base=" + std::to_string(h_baseline) +
            " h_repelled=" + std::to_string(h_repelled) +
            " nearest_anchor=" + std::to_string(nearest_anchor) +
            " dist_baseline=" + std::to_string(nearest_dist) +
            " dist_repelled=" + std::to_string(dist_repelled));
    }
}

void test_palette_separation_wrap_stability() {
    // Two saturated pixels: one at hue just below 0 (≈ 359°) and one just above (≈ 1°).
    // Anchor at 0 rad. Both must move TOWARD 0 — no sign flip or large jump at the seam.
    //
    // To construct pixels at known hues, we use OKLCh → OKLab → RGB.
    // Pixel A: hue = 359° = 2π - π/90 ≈ 6.2134 rad  (just clockwise of 0)
    // Pixel B: hue = 1°  = π/180  ≈ 0.01745 rad     (just counter-clockwise of 0)
    // We need enough chroma (c > kPaletteChromaHi = 0.06) for g_c to be near 1.

    // Build pixels via OKLCh at L=0.65, C=0.15
    const float kL = 0.65F;
    const float kC = 0.15F;
    constexpr float kPi = std::numbers::pi_v<float>;
    const float hue_A = 2.0F * kPi - kPi / 90.0F;  // ≈ 359°
    const float hue_B = kPi / 180.0F;               // ≈ 1°

    // OKLab from OKLCh
    const auto oklch_to_rgb = [](float l, float c, float h) -> std::array<float, 3> {
        const float a = c * std::cos(h);
        const float b = c * std::sin(h);
        // oklab_to_rgb (inverse of renderer's internal fn):
        const float lp = l + 0.3963377774F * a + 0.2158017574F * b;
        const float mp = l - 0.1055613458F * a - 0.0638541728F * b;
        const float sp = l - 0.0894841775F * a - 1.2914855480F * b;
        const float lv = lp * lp * lp;
        const float mv = mp * mp * mp;
        const float sv = sp * sp * sp;
        return {
            std::clamp(4.0767416621F * lv - 3.3077115913F * mv + 0.2309699292F * sv, 0.0F, 1.0F),
            std::clamp(-1.2684380046F * lv + 2.6097574011F * mv - 0.3413193965F * sv, 0.0F, 1.0F),
            std::clamp(-0.0041960863F * lv - 0.7034186147F * mv + 1.7076147010F * sv, 0.0F, 1.0F),
        };
    };

    const auto rgb_A = oklch_to_rgb(kL, kC, hue_A);
    const auto rgb_B = oklch_to_rgb(kL, kC, hue_B);

    dfee::Image img(2, 1, 3);
    img.pixels = {
        rgb_A[0], rgb_A[1], rgb_A[2],
        rgb_B[0], rgb_B[1], rgb_B[2],
    };

    const auto zones = make_flat_zone_masks(img);

    dfee::FilmResponsePlan response;
    response.stock_type = "color_negative";
    response.film_color = 100.0F;
    response.palette_separation = 100.0F;
    response.palette_separation_sensitivity = 1.0F;
    // Default anchors: 0, π/3, 2π/3, π, 4π/3, 5π/3 — nearest to both pixels is 0 rad.

    const dfee::FilmRenderer renderer;
    const auto baseline = renderer.apply_color_response_and_coupling(img, zones,
        [&]() { auto r = response; r.palette_separation = 0.0F; return r; }());
    const auto out = renderer.apply_color_response_and_coupling(img, zones, response);

    const float h_base_A = read_hue(baseline, 0);
    const float h_base_B = read_hue(baseline, 1);
    const float h_out_A  = read_hue(out, 0);
    const float h_out_B  = read_hue(out, 1);

    // Pixel A is at ≈359°: nearest anchor is 0° = 360°. Attraction increases hue toward 2π.
    // After wrap: if h_out_A wraps to near 0, that's fine — we check via shortest-arc distance.
    // The absolute distance |delta_to_anchor| must DECREASE for both pixels.
    const float delta_A_before = std::fabs(hue_delta(h_base_A, 0.0F));
    const float delta_B_before = std::fabs(hue_delta(h_base_B, 0.0F));
    const float delta_A_after  = std::fabs(hue_delta(h_out_A,  0.0F));
    const float delta_B_after  = std::fabs(hue_delta(h_out_B,  0.0F));

    if (!(delta_A_after < delta_A_before - 1.0e-4F)) {
        throw std::runtime_error(
            "wrap stability: pixel A (hue≈359°) did not move toward anchor 0: "
            "h_before=" + std::to_string(h_base_A * 180.0F / std::numbers::pi_v<float>) + "° "
            "h_after=" + std::to_string(h_out_A * 180.0F / std::numbers::pi_v<float>) + "° "
            "delta_before=" + std::to_string(delta_A_before) +
            " delta_after=" + std::to_string(delta_A_after));
    }

    if (!(delta_B_after < delta_B_before - 1.0e-4F)) {
        throw std::runtime_error(
            "wrap stability: pixel B (hue≈1°) did not move toward anchor 0: "
            "h_before=" + std::to_string(h_base_B * 180.0F / std::numbers::pi_v<float>) + "° "
            "h_after=" + std::to_string(h_out_B * 180.0F / std::numbers::pi_v<float>) + "° "
            "delta_before=" + std::to_string(delta_B_before) +
            " delta_after=" + std::to_string(delta_B_after));
    }

    // Neither pixel must have jumped more than π radians (sign flip / large jump).
    const float jump_A = std::fabs(hue_delta(h_out_A, h_base_A));
    const float jump_B = std::fabs(hue_delta(h_out_B, h_base_B));
    constexpr float kMaxJump = 0.5F;  // well under π; max expected shift is kPaletteSepGain=0.35 rad
    if (jump_A > kMaxJump) {
        throw std::runtime_error(
            "wrap stability: pixel A had a large hue jump: " + std::to_string(jump_A) + " rad");
    }
    if (jump_B > kMaxJump) {
        throw std::runtime_error(
            "wrap stability: pixel B had a large hue jump: " + std::to_string(jump_B) + " rad");
    }
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
    if (!(color_plan.film_response.palette_separation_sensitivity > 0.0F)) {
        throw std::runtime_error(
            "portra_400: palette_separation_sensitivity must be > 0, got " +
            std::to_string(color_plan.film_response.palette_separation_sensitivity));
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
    if (std::fabs(mono_plan.film_response.palette_separation_sensitivity) > 1.0e-6F) {
        throw std::runtime_error(
            "tri_x_400: palette_separation_sensitivity must be 0 for monochrome, got " +
            std::to_string(mono_plan.film_response.palette_separation_sensitivity));
    }
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
        test_palette_separation_neutral_pixel_preserved();
        test_palette_separation_direction();
        test_palette_separation_wrap_stability();
        test_emulsion_color_density_increases_mid_saturation_chroma();
        test_solver_color_character_family_defaults();
        std::cout << "dfee_tests passed\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "dfee_tests exception: " << ex.what() << "\n";
        return 1;
    }
}
