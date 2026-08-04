#pragma once

#include "dfee/analyzer.hpp"
#include "dfee/bridge_types.hpp"
#include "dfee/image.hpp"
#include "dfee/profile.hpp"
#include "dfee/solver.hpp"

namespace dfee {

// Phase 1 geometry (Geometry tab): a final spatial transform on the rendered image.
// Composition order: flip -> 90-degree quadrant rotate -> straighten (fine angle,
// auto-cropped to the largest inscribed axis-aligned rect so there are no black
// corners) -> normalized crop. crop_* are in [0,1] on the flipped/rotated/straightened
// image. Identity defaults leave the image untouched (byte-identical, no resample).
struct GeometryParams {
    float crop_x = 0.0F;
    float crop_y = 0.0F;
    float crop_w = 1.0F;
    float crop_h = 1.0F;
    float straighten_deg = 0.0F;  // -45..45, positive = clockwise
    int rotate_quadrant = 0;      // 0..3, 90-degree clockwise steps
    bool flip_h = false;
    bool flip_v = false;

    [[nodiscard]] bool is_identity() const {
        return crop_x == 0.0F && crop_y == 0.0F && crop_w == 1.0F && crop_h == 1.0F &&
               straighten_deg == 0.0F && rotate_quadrant == 0 && !flip_h && !flip_v;
    }
};

// Applies the geometry transform. No-op (returns the input) when g.is_identity().
[[nodiscard]] Image apply_geometry(const Image& rgb, const GeometryParams& g);

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

    // filmic_v3 colour compression: a chroma shoulder that soft-compresses high
    // chroma toward a ceiling (reduced colour dynamic range), plus a bounded,
    // chroma-gated neighbour-lean (red->orange, blue->cyan). Hue-honest;
    // neutrals preserved; no-op when the effective amounts are zero.
    [[nodiscard]] Image apply_color_compression(
        const Image& rgb_linear,
        const FilmResponsePlan& response) const;

    // filmic_v3 per-hue chroma gain: bounded, smooth per-hue-family chroma
    // multiply (from the stock's hue_chroma_gain map) so a stock can emphasise
    // its signature hues (e.g. Kodachrome reds) realistically. Proportional to
    // existing chroma (neutrals untouched); total gain clamped; no-op if empty.
    [[nodiscard]] Image apply_hue_saturation(
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
