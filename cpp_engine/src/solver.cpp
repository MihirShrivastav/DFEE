#include "dfee/solver.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

namespace dfee {
namespace {

[[nodiscard]] float clampf(const float value, const float low, const float high) {
    return std::clamp(value, low, high);
}

[[nodiscard]] float max_clip_ratio(const std::unordered_map<std::string, float>& ratios) {
    float value = 0.0F;
    for (const auto& [_, ratio] : ratios) {
        value = std::max(value, ratio);
    }
    return value;
}

[[nodiscard]] float get_numeric(
    const std::unordered_map<std::string, double>& values,
    const std::string& key,
    const float fallback) {
    const auto it = values.find(key);
    return it != values.end() ? static_cast<float>(it->second) : fallback;
}

[[nodiscard]] std::string get_string(
    const std::unordered_map<std::string, std::string>& values,
    const std::string& key,
    const std::string& fallback) {
    const auto it = values.find(key);
    return it != values.end() && !it->second.empty() ? it->second : fallback;
}

[[nodiscard]] std::string infer_grain_family(
    const FilmStockProfile& stock_profile,
    const float grain_strength,
    const float grain_size,
    const float grain_chroma_strength) {
    if (stock_profile.stock_type == StockType::Monochrome) {
        return grain_size >= 0.70F ? "bw_cubic" : "bw_tabular";
    }
    if (stock_profile.stock_type == StockType::ColorReversal) {
        return "color_reversal_fine";
    }
    if (grain_strength >= 0.50F || grain_size >= 0.58F || grain_chroma_strength >= 0.16F) {
        return "modern_color_negative_high_speed";
    }
    if (grain_strength >= 0.34F || grain_size >= 0.40F) {
        return "consumer_color_negative";
    }
    return "modern_color_negative_fine";
}

struct GrainFamilyDefaults {
    float target_pgi = 37.0F;
    float clumpiness = 0.45F;
    float micro_grit = 0.22F;
    float layer_correlation = 0.75F;
    float shadow_response = 0.72F;
    float midtone_response = 1.0F;
    float highlight_response = 0.28F;
    float underexposure_coarsening = 0.25F;
    float overexposure_smoothing = 0.25F;
};

[[nodiscard]] GrainFamilyDefaults grain_family_defaults(const std::string& family) {
    if (family == "modern_color_negative_high_speed") {
        return {
            .target_pgi = 52.0F,
            .clumpiness = 0.62F,
            .micro_grit = 0.34F,
            .layer_correlation = 0.58F,
            .shadow_response = 0.92F,
            .midtone_response = 1.10F,
            .highlight_response = 0.34F,
            .underexposure_coarsening = 0.42F,
            .overexposure_smoothing = 0.18F,
        };
    }
    if (family == "consumer_color_negative") {
        return {
            .target_pgi = 45.0F,
            .clumpiness = 0.54F,
            .micro_grit = 0.28F,
            .layer_correlation = 0.68F,
            .shadow_response = 0.82F,
            .midtone_response = 1.05F,
            .highlight_response = 0.30F,
            .underexposure_coarsening = 0.34F,
            .overexposure_smoothing = 0.22F,
        };
    }
    if (family == "color_reversal_fine") {
        return {
            .target_pgi = 25.0F,
            .clumpiness = 0.26F,
            .micro_grit = 0.13F,
            .layer_correlation = 0.88F,
            .shadow_response = 0.48F,
            .midtone_response = 0.78F,
            .highlight_response = 0.18F,
            .underexposure_coarsening = 0.18F,
            .overexposure_smoothing = 0.34F,
        };
    }
    if (family == "bw_cubic") {
        return {
            .target_pgi = 56.0F,
            .clumpiness = 0.72F,
            .micro_grit = 0.30F,
            .layer_correlation = 1.0F,
            .shadow_response = 0.92F,
            .midtone_response = 1.15F,
            .highlight_response = 0.40F,
            .underexposure_coarsening = 0.45F,
            .overexposure_smoothing = 0.14F,
        };
    }
    if (family == "bw_tabular") {
        return {
            .target_pgi = 40.0F,
            .clumpiness = 0.42F,
            .micro_grit = 0.20F,
            .layer_correlation = 1.0F,
            .shadow_response = 0.74F,
            .midtone_response = 0.96F,
            .highlight_response = 0.32F,
            .underexposure_coarsening = 0.30F,
            .overexposure_smoothing = 0.20F,
        };
    }
    return {};
}

[[nodiscard]] std::array<float, 3> get_array3(
    const std::unordered_map<std::string, std::vector<double>>& values,
    const std::string& key,
    const std::array<float, 3>& fallback) {
    const auto it = values.find(key);
    if (it == values.end() || it->second.size() < 3) {
        return fallback;
    }
    return {
        static_cast<float>(it->second[0]),
        static_cast<float>(it->second[1]),
        static_cast<float>(it->second[2]),
    };
}

[[nodiscard]] std::unordered_map<std::string, float> get_prefixed_numeric_map(
    const std::unordered_map<std::string, double>& values,
    const std::string& prefix) {
    std::unordered_map<std::string, float> out;
    const std::string dotted = prefix + ".";
    for (const auto& [key, value] : values) {
        if (key.rfind(dotted, 0) == 0) {
            out.emplace(key.substr(dotted.size()), static_cast<float>(value));
        }
    }
    return out;
}

[[nodiscard]] bool contains_warning(const std::vector<std::string>& warnings, const std::string& warning) {
    return std::ranges::find(warnings, warning) != warnings.end();
}

[[nodiscard]] float compute_stock_bias(
    const FilmStockProfile& stock_profile,
    const TonalDistribution& tonal) {
    const std::string stock_id = stock_profile.stock_id;
    switch (stock_profile.stock_type) {
        case StockType::ColorNegative:
            if (stock_id == "portra_400" || stock_id == "portra_160" || stock_id == "portra_800" ||
                stock_id == "pro_400h" || stock_id == "cinestill_50d" || stock_id == "vision3_500t" ||
                stock_id == "vision3_250d" || stock_id == "fuji_eterna_250d") {
                return 0.65F;
            }
            if (stock_id == "ektar_100" || stock_id == "gold_200" || stock_id == "ultramax_400" ||
                stock_id == "superia_400" || stock_id == "colorplus_200") {
                return 0.35F;
            }
            return 0.40F;
        case StockType::ColorReversal:
            if (tonal.luma_p95 > 0.80F) {
                return std::log2(0.78F / tonal.luma_p95);
            }
            return -0.15F;
        case StockType::Monochrome:
            return stock_id == "delta_3200" ? 0.25F : 0.10F;
    }
    return 0.0F;
}

}  // namespace

RenderPlan RenderPlanSolver::solve(
    const SolverInput& input,
    const FilmStockProfile& stock_profile,
    const SolverControls& controls,
    const PrintStockProfile* print_stock) const {
    const auto& tonal = input.tonal_distribution;
    const auto& color = input.hue_saturation_state;
    const auto& spatial = input.spatial_frequency;
    const auto& bias = input.camera_input_bias;

    RenderPlan plan;
    plan.stock_type = to_string(stock_profile.stock_type);
    plan.input_diagnosis = {
        .tonal_state = tonal.tonal_skew,
        .dynamic_range_stops = tonal.dynamic_range_stops,
        .shadow_cast = std::fabs(tonal.shadow_depth) < 0.05F ? "normal" : "deep",
        .midtone_anchor = tonal.midtone_anchor,
        .highlight_headroom = tonal.highlight_headroom,
        .neon_risk = color.neon_risk,
        .specular_candidate_strength = spatial.specular_point_ratio,
    };

    if (tonal.tonal_skew == "highlight_stressed") {
        plan.warnings.emplace_back("HIGH_CHANNEL_CLIPPING");
    }
    if (tonal.shadow_depth < 0.02F) {
        plan.warnings.emplace_back("SHADOW_NOISE_RISK");
    }
    if (color.neon_risk > 0.05F) {
        plan.warnings.emplace_back("NEON_CHROMA_RISK");
    }
    if (bias.has_value() && bias->neutral_confidence < 0.25F) {
        plan.warnings.emplace_back("LOW_NEUTRAL_CONFIDENCE");
    }
    if (spatial.large_highlight_area_ratio > 0.10F) {
        plan.warnings.emplace_back("DIFFUSE_HIGHLIGHT_SUPPRESSION");
    }

    const float adaptation_mult = controls.adaptation_strength;
    const float raw_comp = std::log2(0.18F / std::max(tonal.midtone_anchor, 1.0e-4F));
    const float stock_bias = compute_stock_bias(stock_profile, tonal);

    float exposure_comp = 0.0F;
    if (controls.exposure_intent == "Auto") {
        exposure_comp = raw_comp + stock_bias;
    } else if (controls.exposure_intent == "Lift") {
        exposure_comp = raw_comp + stock_bias + 0.5F;
    } else if (controls.exposure_intent == "Darken") {
        exposure_comp = raw_comp + stock_bias - 0.5F;
    } else {
        exposure_comp = stock_profile.stock_type == StockType::ColorReversal
            ? raw_comp * 0.45F + stock_bias
            : raw_comp * 0.25F + stock_bias;
    }
    exposure_comp = clampf(exposure_comp * adaptation_mult, -2.5F, 2.5F);

    const float neutral_conf = bias.has_value() ? bias->neutral_confidence : 0.8F;
    const float comp_sensitivity = get_numeric(
        stock_profile.numeric_values,
        "adaptation.camera_cast_compensation_sensitivity",
        0.7F);

    float cast_correction_mult = 0.0F;
    if (controls.color_cast_handling == "Auto" ||
        controls.color_cast_handling == "Neutralize" ||
        controls.color_cast_handling == "Strong neutralize") {
        cast_correction_mult = neutral_conf * comp_sensitivity * adaptation_mult;
        if (controls.color_cast_handling == "Strong neutralize") {
            cast_correction_mult = std::max(cast_correction_mult, 0.8F);
        }
    } else if (controls.color_cast_handling == "Preserve warmth") {
        const float warm_cool_bias = bias.has_value() ? bias->warm_cool_bias : 0.0F;
        cast_correction_mult = warm_cool_bias < 0.0F
            ? neutral_conf * comp_sensitivity * 0.8F
            : neutral_conf * comp_sensitivity * 0.2F;
    }

    const float shadow_blue_norm = (bias.has_value() ? bias->blue_excess_index : 0.0F) * cast_correction_mult;
    const float green_mag_stab =
        std::fabs(bias.has_value() ? bias->green_magenta_bias : 0.0F) * cast_correction_mult;
    const float highlight_channel_recovery = max_clip_ratio(input.clipping_ratios) > 0.0F
        ? clampf(max_clip_ratio(input.clipping_ratios) * 5.0F, 0.1F, 0.9F)
        : 0.0F;

    float contrast_comp = 0.0F;
    float highlights_comp = 0.0F;
    float shadows_comp = 0.0F;
    float blacks_comp = 0.0F;
    float whites_comp = 0.0F;
    float midtones_comp = 0.0F;
    const float toe_strength_profile = get_numeric(stock_profile.numeric_values, "tone_response.toe_strength", 0.40F);

    if (stock_profile.stock_type == StockType::ColorReversal) {
        if (tonal.dynamic_range_stops > 10.0F) {
            contrast_comp = -12.0F * (tonal.dynamic_range_stops - 10.0F);
        }
        if (tonal.luma_p95 > 0.80F) {
            highlights_comp = -30.0F * ((tonal.luma_p95 - 0.80F) / 0.20F);
        }
        shadows_comp = 12.0F * (1.0F + toe_strength_profile);
    } else if (stock_profile.stock_type == StockType::ColorNegative) {
        if (tonal.dynamic_range_stops < 7.0F) {
            contrast_comp = 15.0F * (7.0F - tonal.dynamic_range_stops);
        }
        if (tonal.tonal_skew == "low_key") {
            shadows_comp = 15.0F;
        }
    } else if (stock_profile.stock_type == StockType::Monochrome) {
        contrast_comp = stock_profile.stock_id == "delta_3200" ? 8.0F : 5.0F;
    }

    if (tonal.midtone_anchor < 0.12F) {
        midtones_comp = clampf((0.15F - tonal.midtone_anchor) * 100.0F, 0.0F, 30.0F);
    } else if (tonal.midtone_anchor > 0.35F) {
        midtones_comp = clampf((0.25F - tonal.midtone_anchor) * 100.0F, -25.0F, 0.0F);
    }

    plan.pre_film_normalization = {
        .exposure_compensation_stops = exposure_comp,
        .shadow_blue_normalization = shadow_blue_norm,
        .green_magenta_stabilization = green_mag_stab,
        .highlight_channel_recovery = highlight_channel_recovery,
        .contrast_compensation = clampf(contrast_comp * adaptation_mult, -40.0F, 40.0F),
        .highlights_compensation = clampf(highlights_comp * adaptation_mult, -50.0F, 30.0F),
        .shadows_compensation = clampf(shadows_comp * adaptation_mult, -20.0F, 50.0F),
        .blacks_compensation = clampf(blacks_comp * adaptation_mult, -30.0F, 30.0F),
        .whites_compensation = clampf(whites_comp * adaptation_mult, -30.0F, 30.0F),
        .midtones_compensation = clampf(midtones_comp * adaptation_mult, -40.0F, 40.0F),
    };

    float toe_strength = get_numeric(stock_profile.numeric_values, "tone_response.toe_strength", 0.0F);
    float shoulder_strength = get_numeric(stock_profile.numeric_values, "tone_response.shoulder_strength", 0.0F);
    float highlight_rolloff_start = get_numeric(stock_profile.numeric_values, "tone_response.highlight_rolloff_start", 0.0F);
    const float black_density = get_numeric(stock_profile.numeric_values, "tone_response.black_density_floor", 0.0F);
    if (tonal.dynamic_range_stops > 11.5F) {
        shoulder_strength *= 0.9F;
        toe_strength *= 0.85F;
    } else if (tonal.dynamic_range_stops < 5.0F) {
        toe_strength *= 1.15F;
    }
    if (tonal.highlight_headroom < 0.15F) {
        highlight_rolloff_start = std::max(highlight_rolloff_start - 0.05F, 0.5F);
        shoulder_strength = std::min(shoulder_strength + 0.1F, 0.95F);
    }

    float highlight_desaturation = get_numeric(
        stock_profile.numeric_values,
        "hue_saturation_response.highlight_desaturation",
        0.0F);
    if (tonal.highlight_headroom < 0.10F) {
        highlight_desaturation = std::min(highlight_desaturation + 0.15F, 0.95F);
    }

    plan.film_response = {
        .toe_strength = toe_strength,
        .toe_length = get_numeric(stock_profile.numeric_values, "tone_response.toe_length", 0.0F),
        .midtone_density = get_numeric(stock_profile.numeric_values, "tone_response.midtone_contrast", 0.0F),
        .shoulder_strength = shoulder_strength,
        .highlight_rolloff_start = highlight_rolloff_start,
        .black_density_floor = black_density,
        .highlight_desaturation = highlight_desaturation,
        .blue_cyan_compression = get_numeric(stock_profile.numeric_values, "hue_saturation_response.cyan_blue_highlight_compression", 0.0F),
        .red_orange_compression = get_numeric(stock_profile.numeric_values, "hue_saturation_response.red_orange_midtone_compression", 0.0F),
        .neon_compression = get_numeric(stock_profile.numeric_values, "hue_saturation_response.neon_compression", 0.0F),
        .chroma_boost = get_numeric(stock_profile.numeric_values, "hue_saturation_response.saturation_boost", 1.0F),
        .channel_toe_mult = get_array3(stock_profile.numeric_arrays, "tone_response.channel_toe_mult", {1.0F, 1.0F, 1.0F}),
        .channel_shoulder_mult = get_array3(stock_profile.numeric_arrays, "tone_response.channel_shoulder_mult", {1.0F, 1.0F, 1.0F}),
        .channel_midtone_mult = get_array3(stock_profile.numeric_arrays, "tone_response.channel_midtone_mult", {1.0F, 1.0F, 1.0F}),
        .shadow_bias_lab = get_array3(stock_profile.numeric_arrays, "color_response.shadow_bias_lab", {0.0F, 0.0F, 0.0F}),
        .midtone_bias_lab = get_array3(stock_profile.numeric_arrays, "color_response.midtone_bias_lab", {0.0F, 0.0F, 0.0F}),
        .highlight_bias_lab = get_array3(stock_profile.numeric_arrays, "color_response.highlight_bias_lab", {0.0F, 0.0F, 0.0F}),
        .pan_weight_r = 0.25F,
        .pan_weight_g = 0.55F,
        .pan_weight_b = 0.20F,
        .chroma_coupling = get_prefixed_numeric_map(stock_profile.numeric_values, "chroma_coupling"),
        .dye_contamination = get_prefixed_numeric_map(stock_profile.numeric_values, "dye_contamination"),
        .stock_type = to_string(stock_profile.stock_type),
        .film_color = controls.film_color,
    };

    float grain_strength = get_numeric(stock_profile.numeric_values, "grain.strength", 0.0F);
    float grain_size = get_numeric(stock_profile.numeric_values, "grain.size", 0.0F);
    float grain_roughness = get_numeric(stock_profile.numeric_values, "grain.roughness", 0.5F);
    if (controls.grain_size >= 0.0F) {
        grain_size = controls.grain_size;
    } else if (controls.grain_amount == "Auto" && input.raw_iso.has_value()) {
        const float base_iso = get_numeric(stock_profile.numeric_values, "adaptation.base_iso", 400.0F);
        const float shot_iso = static_cast<float>(*input.raw_iso);
        if (shot_iso > 0.0F) {
            const float push_stops = std::log2(shot_iso / base_iso);
            if (push_stops > 0.0F) {
                grain_size *= 1.0F + 0.10F * push_stops;
            }
        }
    }

    if (controls.grain_roughness >= 0.0F) {
        grain_roughness = controls.grain_roughness;
    }

    if (controls.grain_strength >= 0.0F) {
        grain_strength = controls.grain_strength;
    } else if (controls.grain_amount == "Off") {
        grain_strength = 0.0F;
    } else if (controls.grain_amount == "Low") {
        grain_strength *= 0.5F;
    } else if (controls.grain_amount == "High") {
        grain_strength *= 1.5F;
        if (controls.grain_size < 0.0F) {
            grain_size *= 1.2F;
        }
    } else if (controls.grain_amount == "Auto" && input.raw_iso.has_value()) {
        const float base_iso = get_numeric(stock_profile.numeric_values, "adaptation.base_iso", 400.0F);
        const float shot_iso = static_cast<float>(*input.raw_iso);
        if (shot_iso > 0.0F) {
            const float push_stops = std::log2(shot_iso / base_iso);
            if (push_stops > 0.0F) {
                grain_strength *= 1.0F + 0.20F * push_stops;
            } else if (push_stops < 0.0F) {
                grain_strength *= std::max(0.6F, 1.0F + 0.08F * push_stops);
            }
        }
    }

    if (contains_warning(plan.warnings, "SHADOW_NOISE_RISK")) {
        const float noise_sensitivity = get_numeric(
            stock_profile.numeric_values,
            "adaptation.shadow_noise_sensitivity",
            0.6F);
        grain_strength *= 1.0F - 0.20F * noise_sensitivity;
    }

    const std::string inferred_grain_family = infer_grain_family(
        stock_profile,
        grain_strength,
        grain_size,
        get_numeric(stock_profile.numeric_values, "grain.chroma_strength", 0.0F));
    const std::string grain_family = get_string(stock_profile.string_values, "grain.family", inferred_grain_family);
    const GrainFamilyDefaults grain_defaults = grain_family_defaults(grain_family);

    float halation_strength = get_numeric(stock_profile.numeric_values, "halation.strength", 0.0F);
    float bloom_strength = 0.10F;
    if (controls.halation_amount == "Off") {
        halation_strength = 0.0F;
        bloom_strength = 0.0F;
    } else if (controls.halation_amount == "Low") {
        halation_strength *= 0.5F;
        bloom_strength *= 0.5F;
    } else if (controls.halation_amount == "High") {
        halation_strength *= 1.5F;
        bloom_strength *= 1.5F;
    }
    if (contains_warning(plan.warnings, "DIFFUSE_HIGHLIGHT_SUPPRESSION")) {
        halation_strength *= 0.3F;
        bloom_strength *= 1.2F;
    }

    plan.material_effects = {
        .grain_strength = grain_strength,
        .grain_size = grain_size,
        .grain_roughness = grain_roughness,
        .grain_chroma_strength = get_numeric(stock_profile.numeric_values, "grain.chroma_strength", 0.0F),
        .grain_family = grain_family,
        .grain_target_pgi = get_numeric(stock_profile.numeric_values, "grain.target_pgi_35mm_4x6", grain_defaults.target_pgi),
        .grain_clumpiness = get_numeric(stock_profile.numeric_values, "grain.clumpiness", grain_defaults.clumpiness),
        .grain_micro_grit = get_numeric(stock_profile.numeric_values, "grain.micro_grit", grain_defaults.micro_grit),
        .grain_layer_correlation = get_numeric(stock_profile.numeric_values, "grain.layer_correlation", grain_defaults.layer_correlation),
        .grain_shadow_response = get_numeric(stock_profile.numeric_values, "grain.shadow_response", grain_defaults.shadow_response),
        .grain_midtone_response = get_numeric(stock_profile.numeric_values, "grain.midtone_response", grain_defaults.midtone_response),
        .grain_highlight_response = get_numeric(stock_profile.numeric_values, "grain.highlight_response", grain_defaults.highlight_response),
        .grain_underexposure_coarsening = get_numeric(stock_profile.numeric_values, "grain.underexposure_coarsening", grain_defaults.underexposure_coarsening),
        .grain_overexposure_smoothing = get_numeric(stock_profile.numeric_values, "grain.overexposure_smoothing", grain_defaults.overexposure_smoothing),
        .halation_strength = halation_strength,
        .bloom_strength = bloom_strength,
        .edge_softening = clampf(0.15F, 0.05F, 0.35F) * 0.5F,
        .sharpness = controls.sharpness,
        .sharpness_mask = controls.sharpness_mask,
    };

    if (print_stock != nullptr) {
        plan.print_finish = PrintFinishPlan{
            .strength = controls.print_strength,
            .print_c = controls.print_c,
            .print_m = controls.print_m,
            .print_y = controls.print_y,
            .print_contrast = controls.print_contrast,
            .print_black_point = controls.print_black_point,
            .shadow_lift = get_numeric(print_stock->numeric_values, "tone.shadow_lift", 0.02F),
            .contrast_boost = get_numeric(print_stock->numeric_values, "tone.contrast_boost", 1.10F),
            .highlight_rolloff = get_numeric(print_stock->numeric_values, "tone.highlight_rolloff", 0.78F),
            .highlight_rolloff_rate = get_numeric(print_stock->numeric_values, "tone.highlight_rolloff_rate", 2.0F),
            .toe_depth = get_numeric(print_stock->numeric_values, "tone.toe_depth", 0.85F),
            .shadow_bias_lab = get_array3(print_stock->numeric_arrays, "color.shadow_bias_lab", {0.0F, 0.0F, 0.0F}),
            .midtone_bias_lab = get_array3(print_stock->numeric_arrays, "color.midtone_bias_lab", {0.0F, 0.0F, 0.0F}),
            .highlight_bias_lab = get_array3(print_stock->numeric_arrays, "color.highlight_bias_lab", {0.0F, 0.0F, 0.0F}),
            .blue_suppression = get_numeric(print_stock->numeric_values, "color.blue_suppression", 0.0F),
            .red_boost = get_numeric(print_stock->numeric_values, "color.red_boost", 0.0F),
            .green_shift = get_numeric(print_stock->numeric_values, "color.green_shift", 0.0F),
            .saturation_scale = get_numeric(print_stock->numeric_values, "color.saturation_scale", 1.0F),
            .grain_strength = get_numeric(print_stock->numeric_values, "grain.strength", 0.0F),
            .grain_size = get_numeric(print_stock->numeric_values, "grain.size", 0.3F),
        };
    }

    return plan;
}

}  // namespace dfee
