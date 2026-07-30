#pragma once

// Scale-aware luminance-mask sampling. Lets the render stages read a mask that may be a
// lower-resolution proxy of the image without materializing a full-resolution upsample
// (which, at 44 MP x 10 mask channels, is ~1.8 GB). When the mask already matches the
// target size the exact stored value is returned (bit-identical — the preview path, where
// masks are generated at image resolution). When it is smaller, it is bilinear-upsampled
// using cv::resize's INTER_LINEAR coordinate convention (the export path) — visually
// identical to the old full-res resize of a smooth mask, with no seams/banding.

#include "dfee/image.hpp"

#include <algorithm>
#include <cstddef>

namespace dfee {

[[nodiscard]] inline float sample_mask_scaled(
    const LuminanceImage& mask, const int dx, const int dy, const int dst_w, const int dst_h) {
    if (mask.width == dst_w && mask.height == dst_h) {
        return mask.values[static_cast<std::size_t>(dy) * static_cast<std::size_t>(dst_w) +
                           static_cast<std::size_t>(dx)];
    }
    const float sx = static_cast<float>(mask.width) / static_cast<float>(dst_w);
    const float sy = static_cast<float>(mask.height) / static_cast<float>(dst_h);
    float fx = (static_cast<float>(dx) + 0.5F) * sx - 0.5F;
    float fy = (static_cast<float>(dy) + 0.5F) * sy - 0.5F;
    fx = std::clamp(fx, 0.0F, static_cast<float>(mask.width - 1));
    fy = std::clamp(fy, 0.0F, static_cast<float>(mask.height - 1));
    const int x0 = static_cast<int>(fx);
    const int y0 = static_cast<int>(fy);
    const int x1 = std::min(x0 + 1, mask.width - 1);
    const int y1 = std::min(y0 + 1, mask.height - 1);
    const float tx = fx - static_cast<float>(x0);
    const float ty = fy - static_cast<float>(y0);
    const auto at = [&](const int xx, const int yy) {
        return mask.values[static_cast<std::size_t>(yy) * static_cast<std::size_t>(mask.width) +
                           static_cast<std::size_t>(xx)];
    };
    const float top = at(x0, y0) * (1.0F - tx) + at(x1, y0) * tx;
    const float bot = at(x0, y1) * (1.0F - tx) + at(x1, y1) * tx;
    return top * (1.0F - ty) + bot * ty;
}

}  // namespace dfee
