#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "dfee/analyzer.hpp"
#include "dfee/bias.hpp"
#include "dfee/profile.hpp"

namespace dfee {

struct SolverInput {
    TonalDistribution tonal_distribution;
    ColorAnalysis hue_saturation_state;
    SpatialAnalysis spatial_frequency;
    std::unordered_map<std::string, float> clipping_ratios;
    std::optional<CameraBiasAnalysis> camera_input_bias;
    std::optional<int> raw_iso;
};

struct SolverControls {
    float adaptation_strength = 1.0F;
    std::string exposure_intent = "Preserve";
    std::string color_cast_handling = "Auto";
    std::string grain_amount = "Auto";
    float grain_strength = -1.0F;
    float grain_size = -1.0F;
    float grain_roughness = -1.0F;
    std::string halation_amount = "Auto";
    std::string output_finish = "Natural";
    float sharpness = 0.0F;
    float sharpness_mask = 0.5F;
    float film_color = 100.0F;
    float print_strength = 1.0F;
    float print_c = 0.0F;
    float print_m = 0.0F;
    float print_y = 0.0F;
    float print_contrast = 0.0F;
    float print_black_point = 0.0F;
};

struct InputDiagnosis {
    std::string tonal_state = "normal";
    float dynamic_range_stops = 0.0F;
    std::string shadow_cast = "normal";
    float midtone_anchor = 0.18F;
    float highlight_headroom = 0.0F;
    float neon_risk = 0.0F;
    float specular_candidate_strength = 0.0F;
};

struct PreFilmNormalization {
    float exposure_compensation_stops = 0.0F;
    float shadow_blue_normalization = 0.0F;
    float green_magenta_stabilization = 0.0F;
    float highlight_channel_recovery = 0.0F;
    float contrast_compensation = 0.0F;
    float highlights_compensation = 0.0F;
    float shadows_compensation = 0.0F;
    float blacks_compensation = 0.0F;
    float whites_compensation = 0.0F;
    float midtones_compensation = 0.0F;
};

struct FilmResponsePlan {
    float toe_strength = 0.0F;
    float toe_length = 0.0F;
    float midtone_density = 0.0F;
    float shoulder_strength = 0.0F;
    float highlight_rolloff_start = 0.0F;
    float black_density_floor = 0.0F;
    float highlight_desaturation = 0.0F;
    float blue_cyan_compression = 0.0F;
    float red_orange_compression = 0.0F;
    float neon_compression = 0.0F;
    float chroma_boost = 1.0F;
    std::array<float, 3> channel_toe_mult{1.0F, 1.0F, 1.0F};
    std::array<float, 3> channel_shoulder_mult{1.0F, 1.0F, 1.0F};
    std::array<float, 3> channel_midtone_mult{1.0F, 1.0F, 1.0F};
    std::array<float, 3> shadow_bias_lab{0.0F, 0.0F, 0.0F};
    std::array<float, 3> midtone_bias_lab{0.0F, 0.0F, 0.0F};
    std::array<float, 3> highlight_bias_lab{0.0F, 0.0F, 0.0F};
    float pan_weight_r = 0.25F;
    float pan_weight_g = 0.55F;
    float pan_weight_b = 0.20F;
    std::unordered_map<std::string, float> chroma_coupling;
    std::unordered_map<std::string, float> dye_contamination;
    std::string stock_type = "color_negative";
    float film_color = 100.0F;
};

struct MaterialEffectsPlan {
    float grain_strength = 0.0F;
    float grain_size = 0.0F;
    float grain_roughness = 0.0F;
    float grain_chroma_strength = 0.0F;
    std::uint32_t grain_seed = 0U;
    float halation_strength = 0.0F;
    float bloom_strength = 0.0F;
    float edge_softening = 0.0F;
    float sharpness = 0.0F;
    float sharpness_mask = 0.5F;
};

struct PrintFinishPlan {
    float strength = 1.0F;
    float print_c = 0.0F;
    float print_m = 0.0F;
    float print_y = 0.0F;
    float print_contrast = 0.0F;
    float print_black_point = 0.0F;
    float shadow_lift = 0.02F;
    float contrast_boost = 1.10F;
    float highlight_rolloff = 0.78F;
    float highlight_rolloff_rate = 2.0F;
    float toe_depth = 0.85F;
    std::array<float, 3> shadow_bias_lab{0.0F, 0.0F, 0.0F};
    std::array<float, 3> midtone_bias_lab{0.0F, 0.0F, 0.0F};
    std::array<float, 3> highlight_bias_lab{0.0F, 0.0F, 0.0F};
    float blue_suppression = 0.0F;
    float red_boost = 0.0F;
    float green_shift = 0.0F;
    float saturation_scale = 1.0F;
    float grain_strength = 0.0F;
    float grain_size = 0.3F;
};

struct RenderPlan {
    std::string stock_type = "color_negative";
    InputDiagnosis input_diagnosis;
    PreFilmNormalization pre_film_normalization;
    FilmResponsePlan film_response;
    MaterialEffectsPlan material_effects;
    std::optional<PrintFinishPlan> print_finish;
    std::vector<std::string> warnings;
};

class RenderPlanSolver {
public:
    [[nodiscard]] RenderPlan solve(
        const SolverInput& input,
        const FilmStockProfile& stock_profile,
        const SolverControls& controls = {},
        const PrintStockProfile* print_stock = nullptr) const;
};

}  // namespace dfee
