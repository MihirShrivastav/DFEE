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

// Film-sensible "scene placement" auto exposure (see
// docs/superpowers/specs/2026-08-01-film-auto-exposure.md). Instead of metering the
// midtone to a fixed grey (an averaging meter that blows skies), it anchors the
// robust diffuse highlight just below the film shoulder knee and lets the shoulder
// roll off the rest. A soft midtone term lifts dim scenes without overriding the
// highlight anchor. Returns exposure compensation in stops (pre-clamp).
// How strongly a clipping colour channel cancels the upward exposure push. Clip
// ratios are pixel fractions, so a small but visually-dominant blown channel (a red
// car hood, a neon sign) should already bite — hence a high gain.
constexpr float kClipGuardGain = 14.0F;

[[nodiscard]] float compute_scene_placement(
    const float luma_p95, const float luma_p98, const float large_highlight_area_ratio,
    const float midtone_anchor, const float hl_target, const float mid_target,
    const float stock_bias, const float channel_clip) {
    constexpr float eps = 1.0e-4F;
    // Diffuse-highlight level: p95, nudged toward p98 when the bright region is a
    // large diffuse area (a big sky), so we place the whole sky, not just its edge.
    // Speculars (top ~1-2%) are intentionally excluded.
    const float area = std::clamp(large_highlight_area_ratio * 1.5F, 0.0F, 1.0F);
    const float diffuse_hl = luma_p95 + (luma_p98 - luma_p95) * area;

    const float hl_comp = std::log2(hl_target / std::max(diffuse_hl, eps));
    // Desired upward push = scene midtone lift + the stock's rating bias (e.g. Portra
    // "expose slightly over"). The stock bias is part of the DESIRE so it too is
    // capped by the highlight knee — a stock that loves overexposure still can't push
    // diffuse highlights past the shoulder.
    const float desired = std::log2(mid_target / std::max(midtone_anchor, eps)) + stock_bias;

    if (desired >= 0.0F) {
        // Lift toward the desired exposure, but never push diffuse highlights past
        // the shoulder knee.
        const float up = std::min(desired, std::max(hl_comp, 0.0F));
        // Channel-aware guard: a saturated single-channel highlight (red hood, flower)
        // has high R/G/B but only moderate luma, so the luma anchor misses it and the
        // push would clip that channel — which the film then bleaches to a pale patch.
        // A clipping channel cancels the up-push (never affects pull-down).
        const float clip_guard = std::clamp(channel_clip * kClipGuardGain, 0.0F, 1.0F);
        return up * (1.0F - clip_guard);
    }
    // Stock/scene doesn't want to brighten. Only ever pull DOWN here (never up): if
    // the diffuse highlight is above the knee, gently darken toward it; otherwise
    // leave exposure alone. Negative film has wide over-exposure latitude, so the
    // pull-down is gentle.
    return std::min(0.0F, std::max(desired, hl_comp)) * 0.6F;
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

struct ColorCharacterDefaults {
    float highlight_hold_sensitivity;
    float shadow_retention_sensitivity;
    float emulsion_density_sensitivity;
    float palette_range_sensitivity;
};

[[nodiscard]] ColorCharacterDefaults color_character_defaults(const StockType type) {
    switch (type) {
        case StockType::ColorReversal:  return {0.85F, 0.55F, 0.70F, 0.70F};
        case StockType::ColorNegative:  return {0.70F, 0.65F, 0.60F, 0.55F};
        case StockType::Monochrome:     return {0.0F, 0.0F, 0.0F, 0.0F};
    }
    return {0.60F, 0.60F, 0.55F, 0.50F};
}

struct DensityDefaults {
    float strength;
    float low_luma_limit;
};

[[nodiscard]] DensityDefaults density_defaults(const StockType type) {
    switch (type) {
        case StockType::ColorReversal:  return {0.85F, 0.12F};
        case StockType::ColorNegative:  return {0.60F, 0.10F};
        case StockType::Monochrome:     return {0.0F, 0.10F};
    }
    return {0.55F, 0.10F};
}

struct CompressionDefaults {
    float strength;
    float threshold;
    float crosstalk;
};

[[nodiscard]] CompressionDefaults compression_defaults(const StockType type) {
    switch (type) {
        case StockType::ColorReversal:  return {0.70F, 0.45F, 0.35F};
        case StockType::ColorNegative:  return {0.55F, 0.45F, 0.30F};
        case StockType::Monochrome:     return {0.0F, 0.45F, 0.0F};
    }
    return {0.50F, 0.45F, 0.25F};
}

// filmic_v3 tone steering (Slice 2) gains. These scale how far each control moves
// its target params away from the stock default per unit of (gain - 1), where
// gain = control/100. Neutral (control 100 -> gain 1) is a byte-exact no-op for
// every term below, so raising these strengthens the *ends* of the 0-200 range
// without touching the default look. Highlight Rolloff acts on a single luma
// shoulder (chroma-safe) so it gets the full boost; Film Contrast runs per-channel
// through the tone curve, so it's pushed hard but kept just short of the knee where
// saturated colors start clipping to gamut.
constexpr float kToeContrast     = 0.52F; // Film Contrast -> toe deepening
constexpr float kMidContrast     = 0.62F; // Film Contrast -> midtone punch
constexpr float kRolloffStart    = 0.22F; // Highlight Rolloff -> earlier shoulder start
constexpr float kShoulderRolloff = 0.50F; // Highlight Rolloff -> firmer shoulder
constexpr float kRolloffBase     = 0.60F; // Highlight Rolloff -> renderer shoulder at control 100 (neutral; unchanged)
constexpr float kRolloffMax      = 1.80F; // Highlight Rolloff -> renderer shoulder cap

[[nodiscard]] std::vector<float> get_numeric_vector(
    const std::unordered_map<std::string, std::vector<double>>& values,
    const std::string& key) {
    const auto it = values.find(key);
    if (it == values.end()) {
        return {};
    }
    std::vector<float> out;
    out.reserve(it->second.size());
    for (const double v : it->second) {
        out.push_back(static_cast<float>(v));
    }
    return out;
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

    const float profile_adaptation_strength = get_numeric(
        stock_profile.numeric_values,
        "adaptation.default_strength",
        1.0F);
    const float adaptation_mult = controls.adaptation_strength * profile_adaptation_strength;
    const float stock_bias = compute_stock_bias(stock_profile, tonal);

    float exposure_comp = 0.0F;
    if (controls.subtractive_pipeline) {
        // filmic_v3: film-sensible scene placement — anchor diffuse highlights just
        // below the shoulder knee and let the film shoulder roll off the rest, rather
        // than metering the midtone to grey (which blows skies). Per-stock highlight
        // targets set the latitude: negative trusts the shoulder most, reversal least.
        const float hl_target =
            stock_profile.stock_type == StockType::ColorNegative ? 0.74F :
            stock_profile.stock_type == StockType::ColorReversal ? 0.64F :
                                                                   0.72F;  // monochrome
        const float placement = compute_scene_placement(
            tonal.luma_p95, tonal.luma_p98, spatial.large_highlight_area_ratio,
            tonal.midtone_anchor, hl_target, 0.15F, stock_bias,
            max_clip_ratio(input.clipping_ratios));
        if (controls.exposure_intent == "Auto") {
            exposure_comp = placement;
        } else if (controls.exposure_intent == "Lift") {
            exposure_comp = placement + 0.5F;
        } else if (controls.exposure_intent == "Darken") {
            exposure_comp = placement - 0.5F;
        } else {  // Preserve / as shot — trust the input, apply placement only gently
            exposure_comp = placement * 0.25F;
        }
        exposure_comp = clampf(exposure_comp * adaptation_mult, -1.5F, 2.0F);
    } else {
        // parity_v1 / filmic_v2: unchanged averaging-meter behaviour (byte-identical).
        const float raw_comp = std::log2(0.18F / std::max(tonal.midtone_anchor, 1.0e-4F));
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
    }

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

    const float blue_cast_suppression = get_numeric(
        stock_profile.numeric_values,
        "color_response.blue_cast_suppression",
        1.0F);
    const float green_magenta_stabilization = get_numeric(
        stock_profile.numeric_values,
        "color_response.green_magenta_stabilization",
        1.0F);
    const float shadow_blue_norm =
        (bias.has_value() ? bias->blue_excess_index : 0.0F) * cast_correction_mult * blue_cast_suppression;
    const float green_mag_stab =
        std::fabs(bias.has_value() ? bias->green_magenta_bias : 0.0F) * cast_correction_mult * green_magenta_stabilization;
    const float highlight_stress_sensitivity = get_numeric(
        stock_profile.numeric_values,
        "adaptation.highlight_stress_sensitivity",
        1.0F);
    const float highlight_channel_recovery = max_clip_ratio(input.clipping_ratios) > 0.0F
        ? clampf(max_clip_ratio(input.clipping_ratios) * 5.0F * highlight_stress_sensitivity, 0.1F, 0.9F)
        : 0.0F;

    float contrast_comp = 0.0F;
    float highlights_comp = 0.0F;
    float shadows_comp = 0.0F;
    float blacks_comp = 0.0F;
    float whites_comp = 0.0F;
    float midtones_comp = 0.0F;

    // filmic_v3 "trust the curve": the auto path no longer stretches dynamic range
    // (shadow-lift / highlight-recovery / contrast / midtone re-centering) — that is
    // what produced the flat, HDR-like look. The film's own tone curve provides
    // contrast and shoulder rolloff; scene placement (above) sets exposure; genuine
    // clipped-channel recovery + colour-cast normalisation are kept below. The legacy
    // parity_v1/filmic_v2 behaviour is preserved unchanged.
    if (!controls.subtractive_pipeline) {
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

    float midtone_density = get_numeric(stock_profile.numeric_values, "tone_response.midtone_contrast", 0.0F);

    // Film Profile Strength is a stock-aware master curve control. It is deliberately
    // separate from Film Contrast: strength scales the profile's authored toe/mid/
    // shoulder character as one unit, while Film Contrast remains a creative override.
    const float profile_strength = std::clamp(controls.profile_strength / 100.0F, 0.0F, 2.0F);
    const float profile_delta = profile_strength - 1.0F;
    const bool reversal = stock_profile.stock_type == StockType::ColorReversal;
    const bool monochrome = stock_profile.stock_type == StockType::Monochrome;
    const float default_toe_sensitivity = reversal ? 0.24F : (monochrome ? 0.22F : 0.16F);
    const float default_midtone_sensitivity = reversal ? 0.20F : (monochrome ? 0.24F : 0.14F);
    const float default_shoulder_sensitivity = reversal ? 0.24F : (monochrome ? 0.14F : 0.16F);
    const float default_rolloff_sensitivity = reversal ? 0.05F : (monochrome ? 0.03F : 0.04F);
    const float toe_sensitivity = get_numeric(
        stock_profile.numeric_values, "tone_response.profile_strength_toe_sensitivity", default_toe_sensitivity);
    const float midtone_sensitivity = get_numeric(
        stock_profile.numeric_values, "tone_response.profile_strength_midtone_sensitivity", default_midtone_sensitivity);
    const float shoulder_sensitivity = get_numeric(
        stock_profile.numeric_values, "tone_response.profile_strength_shoulder_sensitivity", default_shoulder_sensitivity);
    const float rolloff_sensitivity = get_numeric(
        stock_profile.numeric_values, "tone_response.profile_strength_rolloff_sensitivity", default_rolloff_sensitivity);

    // filmic_v3 tone steering (Slice 2): Film Contrast + Highlight Rolloff, resolved
    // from stock defaults x manual controls x a scene-referred adaptive factor.
    float tone_adaptive_factor = 1.0F;
    float highlight_rolloff_knee = 1.0F;    // >=1.0 keeps the renderer shoulder off (filmic_v2/parity)
    float highlight_rolloff_amount = 0.0F;  // 0 keeps the renderer shoulder off
    if (controls.subtractive_pipeline) {
        toe_strength = std::clamp(toe_strength * (1.0F + toe_sensitivity * profile_delta), 0.0F, 1.5F);
        midtone_density = std::clamp(
            midtone_density * (1.0F + midtone_sensitivity * profile_delta), 0.0F, 2.0F);
        shoulder_strength = std::clamp(
            shoulder_strength * (1.0F + shoulder_sensitivity * profile_delta), 0.0F, 0.98F);
        highlight_rolloff_start = std::clamp(
            highlight_rolloff_start - rolloff_sensitivity * profile_delta, 0.35F, 1.0F);
        if (controls.adaptive) {
            // Flat / high-DR / log-like scenes get stronger filmic tone; contrasty scenes less.
            float adapt = 1.0F;
            if (tonal.dynamic_range_stops > 10.0F) {
                adapt += std::min((tonal.dynamic_range_stops - 10.0F) * 0.08F, 0.40F);
            }
            if (tonal.dynamic_range_stops < 6.0F) {
                adapt -= std::min((6.0F - tonal.dynamic_range_stops) * 0.05F, 0.20F);
            }
            tone_adaptive_factor = std::clamp(adapt, 0.80F, 1.40F);
        }
        const float contrast_gain = std::clamp(controls.film_contrast / 100.0F, 0.0F, 2.0F) * tone_adaptive_factor;
        const float rolloff_gain = std::clamp(controls.highlight_rolloff / 100.0F, 0.0F, 2.0F) * tone_adaptive_factor;
        // Film Contrast: deepen toe + punch midtones.
        toe_strength = std::clamp(toe_strength * (1.0F + kToeContrast * (contrast_gain - 1.0F)), 0.0F, 1.5F);
        midtone_density = std::clamp(midtone_density * (1.0F + kMidContrast * (contrast_gain - 1.0F)), 0.0F, 2.0F);
        // Highlight Rolloff: earlier shoulder start + firmer shoulder.
        highlight_rolloff_start = std::clamp(
            highlight_rolloff_start - kRolloffStart * (rolloff_gain - 1.0F), 0.35F, 1.0F);
        shoulder_strength = std::clamp(
            shoulder_strength * (1.0F + kShoulderRolloff * (rolloff_gain - 1.0F)), 0.0F, 0.98F);
        // Real highlight shoulder for the renderer: a knee (where highlights begin to
        // roll off) plus a compression strength. On by default at control 100 (gain ~1),
        // stronger as the control moves forward, off at control 0.
        highlight_rolloff_knee = std::clamp(highlight_rolloff_start, 0.35F, 0.95F);
        highlight_rolloff_amount = std::clamp(kRolloffBase * rolloff_gain, 0.0F, kRolloffMax);
    }

    float highlight_desaturation = get_numeric(
        stock_profile.numeric_values,
        "hue_saturation_response.highlight_desaturation",
        0.0F);
    if (tonal.highlight_headroom < 0.10F) {
        highlight_desaturation = std::min(highlight_desaturation + 0.15F, 0.95F);
    }

    // Shadow Lift: bipolar control around the stock's natural base-fog floor (black_density).
    // 0 = the stock's authored floor (unchanged). Positive lifts the deep-shadow floor toward a
    // matte ceiling and widens its footprint into the low shadows (a genuine faded look, not just
    // a moved black point). Negative pulls the floor toward true black and tightens the footprint.
    // The renderer applies floor via: s + floor * (1 - clamp(s/knee,0,1))^2.
    constexpr float kShadowLiftFloorMax = 0.07F;  // shared ceiling so the slider ends mean the same on every stock
    const float shadow_lift_norm = std::clamp(controls.shadow_lift / 100.0F, -1.0F, 1.0F);
    float shadow_lift_floor = black_density;
    float shadow_lift_knee = 0.25F;
    if (shadow_lift_norm > 0.0F) {
        shadow_lift_floor = black_density + shadow_lift_norm * std::max(0.0F, kShadowLiftFloorMax - black_density);
        shadow_lift_knee = 0.25F + shadow_lift_norm * 0.20F;   // widen: 0.25 -> 0.45
    } else if (shadow_lift_norm < 0.0F) {
        shadow_lift_floor = std::max(0.0F, black_density * (1.0F + shadow_lift_norm));  // -> 0 at -100
        shadow_lift_knee = 0.25F + shadow_lift_norm * 0.08F;   // tighten: 0.25 -> 0.17
    }

    // Crossover strength: dial the stock's inherent dye-layer crossover. The per-channel
    // toe/shoulder/midtone multipliers ARE the curve crossover (R/G/B tone curves that
    // don't run parallel), and the shadow/highlight Lab biases are its color expression
    // (cool shadows / warm highlights). Scaling both together by one control moves the
    // whole crossover: 100 = the stock's authored amount, 0 = channels unified / no
    // crossover, 200 = double. Midtone palette bias is left alone (it's the stock's base
    // warmth, not part of the shadow<->highlight split). The separate Split Toning control
    // (crossbalance) adds a manual cast on top of this.
    const float crossover_scale = std::clamp(controls.crossover / 100.0F, 0.0F, 2.0F);
    const auto scale_mult = [crossover_scale](std::array<float, 3> m) {
        for (float& v : m) { v = 1.0F + (v - 1.0F) * crossover_scale; }
        return m;
    };
    const auto scale_bias = [crossover_scale](std::array<float, 3> b) {
        for (float& v : b) { v *= crossover_scale; }
        return b;
    };
    const std::array<float, 3> xover_channel_toe = scale_mult(
        get_array3(stock_profile.numeric_arrays, "tone_response.channel_toe_mult", {1.0F, 1.0F, 1.0F}));
    const std::array<float, 3> xover_channel_shoulder = scale_mult(
        get_array3(stock_profile.numeric_arrays, "tone_response.channel_shoulder_mult", {1.0F, 1.0F, 1.0F}));
    const std::array<float, 3> xover_channel_midtone = scale_mult(
        get_array3(stock_profile.numeric_arrays, "tone_response.channel_midtone_mult", {1.0F, 1.0F, 1.0F}));
    const std::array<float, 3> xover_shadow_bias = scale_bias(
        get_array3(stock_profile.numeric_arrays, "color_response.shadow_bias_lab", {0.0F, 0.0F, 0.0F}));
    const std::array<float, 3> xover_highlight_bias = scale_bias(
        get_array3(stock_profile.numeric_arrays, "color_response.highlight_bias_lab", {0.0F, 0.0F, 0.0F}));

    plan.film_response = {
        .toe_strength = toe_strength,
        .toe_length = get_numeric(stock_profile.numeric_values, "tone_response.toe_length", 0.0F),
        .midtone_density = midtone_density,
        .shoulder_strength = shoulder_strength,
        .highlight_rolloff_start = highlight_rolloff_start,
        .black_density_floor = shadow_lift_floor,
        .shadow_lift_knee = shadow_lift_knee,
        .highlight_desaturation = highlight_desaturation,
        .blue_cyan_compression = get_numeric(stock_profile.numeric_values, "hue_saturation_response.cyan_blue_highlight_compression", 0.0F),
        .red_orange_compression = get_numeric(stock_profile.numeric_values, "hue_saturation_response.red_orange_midtone_compression", 0.0F),
        .yellow_green_muting = get_numeric(stock_profile.numeric_values, "hue_saturation_response.yellow_green_muting", 0.0F),
        .neon_compression = get_numeric(stock_profile.numeric_values, "hue_saturation_response.neon_compression", 0.0F),
        .chroma_boost = get_numeric(stock_profile.numeric_values, "hue_saturation_response.saturation_boost", 1.0F),
        .channel_toe_mult = xover_channel_toe,
        .channel_shoulder_mult = xover_channel_shoulder,
        .channel_midtone_mult = xover_channel_midtone,
        .shadow_bias_lab = xover_shadow_bias,
        .midtone_bias_lab = get_array3(stock_profile.numeric_arrays, "color_response.midtone_bias_lab", {0.0F, 0.0F, 0.0F}),
        .highlight_bias_lab = xover_highlight_bias,
        .pan_weight_r = get_numeric(stock_profile.numeric_values, "color_response.pan_weight_r", 0.25F),
        .pan_weight_g = get_numeric(stock_profile.numeric_values, "color_response.pan_weight_g", 0.55F),
        .pan_weight_b = get_numeric(stock_profile.numeric_values, "color_response.pan_weight_b", 0.20F),
        .chroma_coupling = get_prefixed_numeric_map(stock_profile.numeric_values, "chroma_coupling"),
        .dye_contamination = get_prefixed_numeric_map(stock_profile.numeric_values, "dye_contamination"),
        .hue_chroma_gain = get_prefixed_numeric_map(stock_profile.numeric_values, "hue_chroma_gain"),
        .stock_type = to_string(stock_profile.stock_type),
        .film_color = controls.film_color,
        .highlight_color_hold = controls.highlight_color_hold,
        .shadow_color_retention = controls.shadow_color_retention,
        .palette_range = controls.palette_range,
        .emulsion_color_density = controls.emulsion_color_density,
        // RAW baseline development may already establish part of a stock's intended
        // contrast. Profiles can reconcile only the RAW tone blend; TIFF input later
        // overrides this through its rendered-input contract.
        .tone_response_strength = controls.subtractive_pipeline
            ? get_numeric(stock_profile.numeric_values, "tone_response.raw_baseline_strength", 1.0F) * profile_strength
            : 1.0F,
    };
    plan.film_response.tone_response_strength = std::clamp(
        plan.film_response.tone_response_strength, 0.0F, 1.0F);
    plan.film_response.profile_strength = controls.profile_strength;

    const ColorCharacterDefaults cc_defaults = color_character_defaults(stock_profile.stock_type);
    plan.film_response.highlight_hold_sensitivity = get_numeric(
        stock_profile.numeric_values, "color_character.highlight_hold_sensitivity",
        cc_defaults.highlight_hold_sensitivity);
    plan.film_response.shadow_retention_sensitivity = get_numeric(
        stock_profile.numeric_values, "color_character.shadow_retention_sensitivity",
        cc_defaults.shadow_retention_sensitivity);
    plan.film_response.emulsion_density_sensitivity = get_numeric(
        stock_profile.numeric_values, "color_character.emulsion_density_sensitivity",
        cc_defaults.emulsion_density_sensitivity);
    plan.film_response.palette_range_sensitivity = get_numeric(
        stock_profile.numeric_values, "color_character.palette.range_sensitivity",
        cc_defaults.palette_range_sensitivity);
    plan.film_response.palette_anchors = get_numeric_vector(
        stock_profile.numeric_arrays, "color_character.palette.anchors");
    plan.film_response.palette_anchor_weights = get_numeric_vector(
        stock_profile.numeric_arrays, "color_character.palette.anchor_weights");

    const DensityDefaults dens = density_defaults(stock_profile.stock_type);
    plan.film_response.film_color_density = controls.film_color_density;
    plan.film_response.density_strength = get_numeric(
        stock_profile.numeric_values, "density.strength", dens.strength);
    plan.film_response.density_low_luma_limit = get_numeric(
        stock_profile.numeric_values, "density.low_luma_limit", dens.low_luma_limit);

    const CompressionDefaults comp = compression_defaults(stock_profile.stock_type);
    // Color Compression is folded into the Color Density control: one slider drives both
    // the subtractive density (primary effect) and the palette-cohesion compression
    // (subtle secondary effect), so there is no separate near-inert compression slider.
    plan.film_response.film_color_compression = controls.film_color_density;
    plan.film_response.compression_strength = get_numeric(
        stock_profile.numeric_values, "compression.strength", comp.strength);
    plan.film_response.compression_threshold = get_numeric(
        stock_profile.numeric_values, "compression.threshold", comp.threshold);
    plan.film_response.compression_crosstalk = get_numeric(
        stock_profile.numeric_values, "compression.crosstalk", comp.crosstalk);

    // Per-stock crossover (drives the Crossover control). Monochrome stocks have NO colour
    // crossover — force it to zero so the always-on Crossover control can never tint a B&W
    // image (they have no `crossover:` block, so they would otherwise inherit the colour
    // default below). Colour stocks read their authored block; the teal-shadow / warm-
    // highlight default is only a fallback for a colour stock that omits the block.
    if (stock_profile.stock_type == StockType::Monochrome) {
        plan.film_response.crossover_shadow_cast = {0.0F, 0.0F};
        plan.film_response.crossover_highlight_cast = {0.0F, 0.0F};
        plan.film_response.crossover_exposure_sensitivity = 0.0F;
    } else {
        plan.film_response.crossover_shadow_cast = {
            get_numeric(stock_profile.numeric_values, "crossover.shadow_cast_a", -0.35F),
            get_numeric(stock_profile.numeric_values, "crossover.shadow_cast_b", -0.55F)};
        plan.film_response.crossover_highlight_cast = {
            get_numeric(stock_profile.numeric_values, "crossover.highlight_cast_a", 0.45F),
            get_numeric(stock_profile.numeric_values, "crossover.highlight_cast_b", 0.60F)};
        plan.film_response.crossover_exposure_sensitivity =
            get_numeric(stock_profile.numeric_values, "crossover.exposure_sensitivity", 0.40F);
    }
    // Scene-exposure key from the (raw) midtone anchor: dark scene = underexposed (-1),
    // bright scene = overexposed (+1).
    plan.film_response.scene_exposure_key = std::clamp(
        std::log2(std::max(tonal.midtone_anchor, 1.0e-4F) / 0.18F), -1.0F, 1.0F);

    plan.film_response.highlight_rolloff = controls.highlight_rolloff;
    plan.film_response.film_contrast = controls.film_contrast;
    plan.film_response.tone_adaptive_factor = tone_adaptive_factor;
    plan.film_response.highlight_rolloff_knee = highlight_rolloff_knee;
    plan.film_response.highlight_rolloff_amount = highlight_rolloff_amount;

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
                // Low-ISO shots (below the stock's box speed) read much finer — reduce grain
                // meaningfully, not just marginally. ISO 100 on a 400 stock -> ~0.56x.
                grain_strength *= std::max(0.35F, 1.0F + 0.22F * push_stops);
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

    // Preserve the value before the scene-wide suppression pass. Supplying it
    // through Custom causes that same pass to run once, matching Auto exactly.
    // The final grain_strength above remains the renderer's effective value.
    const float grain_custom_strength = contains_warning(plan.warnings, "SHADOW_NOISE_RISK")
        ? [&stock_profile, grain_strength]() {
            const float noise_sensitivity = get_numeric(
                stock_profile.numeric_values,
                "adaptation.shadow_noise_sensitivity",
                0.6F);
            const float suppression = 1.0F - 0.20F * noise_sensitivity;
            return suppression > 1.0e-6F ? grain_strength / suppression : grain_strength;
        }()
        : grain_strength;

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
    // filmic_v3 halation: scale by the strength control and resolve a luminance threshold.
    // Left untouched for parity_v1 / filmic_v2 (controls default; subtractive flag off).
    float halation_threshold = 0.58F;
    if (controls.subtractive_pipeline) {
        halation_strength *= std::clamp(controls.halation_strength / 100.0F, 0.0F, 2.0F);
        halation_threshold = std::clamp(0.72F - (controls.halation_threshold / 100.0F) * 0.34F, 0.35F, 0.75F);
    }

    plan.material_effects = {
        .grain_strength = grain_strength,
        .grain_size = grain_size,
        .grain_roughness = grain_roughness,
        .grain_custom_strength = grain_custom_strength,
        .grain_custom_size = grain_size,
        .grain_custom_roughness = grain_roughness,
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
        .grain_peak_zone = get_string(stock_profile.string_values, "grain.peak_zone", "lower_mid_to_mid"),
        .grain_texture_masking = get_numeric(stock_profile.numeric_values, "grain.texture_masking", 1.0F),
        .halation_strength = halation_strength,
        .halation_threshold = halation_threshold,
        .halation_subtractive = controls.subtractive_pipeline,
        .halation_trigger = get_string(stock_profile.string_values, "halation.trigger", "specular_only"),
        .halation_radius_inner = get_numeric(stock_profile.numeric_values, "halation.radius_inner", 5.0F),
        .halation_radius_outer = get_numeric(stock_profile.numeric_values, "halation.radius_outer", 20.0F),
        .halation_warm_core = get_array3(stock_profile.numeric_arrays, "halation.warm_core", {1.0F, 0.22F, 0.08F}),
        .halation_red_fringe = get_array3(stock_profile.numeric_arrays, "halation.red_fringe", {1.0F, 0.16F, 0.045F}),
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
            .print_toe = get_numeric(print_stock->numeric_values, "tone.print_toe", 0.0F),
            .print_shoulder = get_numeric(print_stock->numeric_values, "tone.print_shoulder", 0.0F),
            .channel_toe_mult = get_array3(print_stock->numeric_arrays, "tone.channel_toe_mult", {1.0F, 1.0F, 1.0F}),
            .channel_shoulder_mult = get_array3(print_stock->numeric_arrays, "tone.channel_shoulder_mult", {1.0F, 1.0F, 1.0F}),
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

RenderPlan RenderPlanSolver::solve_neutral(
    const SolverInput& input,
    const SolverControls& controls) const {
    const auto& tonal = input.tonal_distribution;
    const auto& color = input.hue_saturation_state;
    const auto& spatial = input.spatial_frequency;

    RenderPlan plan;
    plan.stock_type = "neutral";
    plan.input_diagnosis = {
        .tonal_state = tonal.tonal_skew,
        .dynamic_range_stops = tonal.dynamic_range_stops,
        .shadow_cast = std::fabs(tonal.shadow_depth) < 0.05F ? "normal" : "deep",
        .midtone_anchor = tonal.midtone_anchor,
        .highlight_headroom = tonal.highlight_headroom,
        .neon_risk = color.neon_risk,
        .specular_candidate_strength = spatial.specular_point_ratio,
    };

    // No-stock preview. There is no film shoulder to roll off highlights here, so
    // Auto placement is the most conservative (lowest highlight target). filmic_v3
    // uses the shared highlight-anchored scene placement; legacy pipelines keep the
    // old midtone-to-18% averaging meter for byte-identical parity.
    float exposure_compensation = 0.0F;
    if (controls.exposure_intent == "Auto") {
        if (controls.subtractive_pipeline) {
            constexpr float kNeutralHlTarget = 0.68F;
            exposure_compensation = compute_scene_placement(
                tonal.luma_p95, tonal.luma_p98, spatial.large_highlight_area_ratio,
                tonal.midtone_anchor, kNeutralHlTarget, 0.15F, 0.0F,
                max_clip_ratio(input.clipping_ratios));
        } else {
            exposure_compensation = std::log2(0.18F / std::max(tonal.midtone_anchor, 1.0e-4F));
            constexpr float kAutoHighlightCeiling = 0.82F;
            const float highlights = std::max(tonal.luma_p98, 1.0e-4F);
            const float headroom_up = std::log2(kAutoHighlightCeiling / highlights);
            if (exposure_compensation > 0.0F) {
                exposure_compensation = std::min(exposure_compensation, std::max(headroom_up, 0.0F));
            }
        }
        exposure_compensation = clampf(
            exposure_compensation * std::clamp(controls.adaptation_strength, 0.0F, 1.0F),
            -2.5F,
            2.5F);
    }

    plan.pre_film_normalization.exposure_compensation_stops = exposure_compensation;
    plan.film_response.stock_type = "neutral";
    return plan;
}

}  // namespace dfee
