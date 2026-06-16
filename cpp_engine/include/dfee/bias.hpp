#pragma once

#include <array>

#include "dfee/analyzer.hpp"
#include "dfee/image.hpp"
#include "dfee/raw_decode.hpp"

namespace dfee {

struct CameraBiasAnalysis {
    float neutral_confidence = 0.0F;
    std::array<float, 3> global_cast_lab{0.5F, 0.0F, 0.0F};
    std::array<float, 3> shadow_cast_lab{0.5F, 0.0F, 0.0F};
    std::array<float, 3> midtone_cast_lab{0.5F, 0.0F, 0.0F};
    std::array<float, 3> highlight_cast_lab{0.5F, 0.0F, 0.0F};
    float blue_excess_index = 0.0F;
    float green_magenta_bias = 0.0F;
    float warm_cool_bias = 0.0F;
};

class CameraBiasEstimator {
public:
    [[nodiscard]] CameraBiasAnalysis estimate_bias(
        const Image& rgb_linear,
        const DecodedRawChannelMasks& clipping_masks,
        const ZoneMasks& zone_masks) const;
};

}  // namespace dfee
