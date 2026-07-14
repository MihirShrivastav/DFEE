#pragma once

#include "dfee/analyzer.hpp"
#include "dfee/bridge_types.hpp"
#include "dfee/image.hpp"
#include "dfee/profile.hpp"
#include "dfee/solver.hpp"

namespace dfee {

class FilmRenderer {
public:
    [[nodiscard]] Image render(
        const Image& rgb_linear,
        const ZoneMasks& zone_masks,
        const SpatialMasks& spatial_masks,
        const RenderPlan& render_plan) const;

    [[nodiscard]] Image apply_pre_film_normalization(
        const Image& rgb_linear,
        const ZoneMasks& zone_masks,
        const PreFilmNormalization& pre_film) const;

    [[nodiscard]] Image apply_panchromatic_conversion(
        const Image& rgb_linear,
        const FilmResponsePlan& response) const;

    [[nodiscard]] Image apply_film_tone_response(
        const Image& rgb_linear,
        const FilmResponsePlan& response) const;

    [[nodiscard]] Image apply_dye_contamination(
        const Image& rgb_linear,
        const FilmResponsePlan& response) const;

    [[nodiscard]] Image apply_color_response(
        const Image& rgb_linear,
        const ZoneMasks& zone_masks,
        const FilmResponsePlan& response) const;

    [[nodiscard]] Image apply_color_response_and_coupling(
        const Image& rgb_linear,
        const ZoneMasks& zone_masks,
        const FilmResponsePlan& response,
        NativeEngineMetadata* metadata = nullptr,
        const char* timing_prefix = nullptr) const;

    [[nodiscard]] Image apply_luminance_chroma_coupling(
        const Image& rgb_linear,
        const FilmResponsePlan& response) const;

    // filmic_v3 subtractive density: reduce OKLab L proportional to normalized
    // chroma (saturated colours get denser/darker), hue and chroma preserved, a
    // low-luminance limiter protects deep shadows. No-op when the effective
    // amount is zero (monochrome / neutral / film_color_density == 0).
    [[nodiscard]] Image apply_subtractive_density(
        const Image& rgb_linear,
        const FilmResponsePlan& response) const;

    [[nodiscard]] Image apply_acutance_shaping(
        const Image& rgb_linear,
        const MaterialEffectsPlan& effects) const;

    [[nodiscard]] Image apply_clarity(
        const Image& rgb_linear,
        float amount) const;

    [[nodiscard]] Image apply_texture(
        const Image& rgb_linear,
        float amount) const;

    [[nodiscard]] Image apply_dehaze(
        const Image& rgb_linear,
        float amount) const;

    [[nodiscard]] Image apply_halation_bloom(
        const Image& rgb_linear,
        const ZoneMasks& zone_masks,
        const SpatialMasks& spatial_masks,
        const MaterialEffectsPlan& effects) const;

    [[nodiscard]] Image apply_filmic_halation_bloom(
        const Image& rgb_linear,
        const ZoneMasks& zone_masks,
        const SpatialMasks& spatial_masks,
        const MaterialEffectsPlan& effects) const;

    [[nodiscard]] Image apply_film_grain(
        const Image& rgb_linear,
        const SpatialMasks& spatial_masks,
        const MaterialEffectsPlan& effects) const;

    [[nodiscard]] Image apply_filmic_grain(
        const Image& rgb_linear,
        const SpatialMasks& spatial_masks,
        const MaterialEffectsPlan& effects) const;

    [[nodiscard]] Image apply_print_finish(
        const Image& rgb_linear,
        const PrintFinishPlan& print_finish) const;
};

}  // namespace dfee
