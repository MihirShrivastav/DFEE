#pragma once

#include "dfee/analyzer.hpp"
#include "dfee/image.hpp"
#include "dfee/solver.hpp"

namespace dfee {

class FilmRenderer {
public:
    [[nodiscard]] Image apply_pre_film_normalization(
        const Image& rgb_linear,
        const ZoneMasks& zone_masks,
        const PreFilmNormalization& pre_film) const;
};

}  // namespace dfee
