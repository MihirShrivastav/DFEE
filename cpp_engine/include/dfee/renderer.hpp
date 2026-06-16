#pragma once

#include "dfee/analyzer.hpp"
#include "dfee/image.hpp"
#include "dfee/profile.hpp"
#include "dfee/solver.hpp"

namespace dfee {

class FilmRenderer {
public:
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

    [[nodiscard]] Image apply_color_response(
        const Image& rgb_linear,
        const ZoneMasks& zone_masks,
        const FilmResponsePlan& response) const;

    [[nodiscard]] Image apply_luminance_chroma_coupling(
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
};

}  // namespace dfee
