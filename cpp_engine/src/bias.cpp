#include "dfee/bias.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace dfee {
namespace {

struct OklabPixel {
    float l = 0.0F;
    float a = 0.0F;
    float b = 0.0F;
    float chroma = 0.0F;
};

[[nodiscard]] OklabPixel rgb_to_oklab_pixel(const float r, const float g, const float b) {
    const float rs = std::max(r, 1.0e-12F);
    const float gs = std::max(g, 1.0e-12F);
    const float bs = std::max(b, 1.0e-12F);

    const float l = 0.4122214708F * rs + 0.5363325363F * gs + 0.0514459929F * bs;
    const float m = 0.2119034982F * rs + 0.6806995451F * gs + 0.1073969566F * bs;
    const float s = 0.0883024619F * rs + 0.2817188376F * gs + 0.6299787005F * bs;

    const float lp = std::cbrt(l);
    const float mp = std::cbrt(m);
    const float sp = std::cbrt(s);

    const float out_l = 0.2104542553F * lp + 0.7936177850F * mp - 0.0040720468F * sp;
    const float out_a = 1.9779984951F * lp - 2.4285922050F * mp + 0.4505937099F * sp;
    const float out_b = 0.0259040371F * lp + 0.7827717612F * mp - 0.8086757983F * sp;
    return {
        out_l,
        out_a,
        out_b,
        std::sqrt(out_a * out_a + out_b * out_b),
    };
}

[[nodiscard]] std::array<float, 3> weighted_cast_from_zone(
    const std::vector<OklabPixel>& pixels,
    const LuminanceImage& zone_mask) {
    float weight_sum = 0.0F;
    float l_sum = 0.0F;
    float a_sum = 0.0F;
    float b_sum = 0.0F;

    for (std::size_t i = 0; i < pixels.size(); ++i) {
        const float weight = zone_mask.values[i];
        weight_sum += weight;
        l_sum += pixels[i].l * weight;
        a_sum += pixels[i].a * weight;
        b_sum += pixels[i].b * weight;
    }

    if (weight_sum <= 1.0e-4F) {
        return {0.5F, 0.0F, 0.0F};
    }
    return {
        l_sum / weight_sum,
        a_sum / weight_sum,
        b_sum / weight_sum,
    };
}

}  // namespace

CameraBiasAnalysis CameraBiasEstimator::estimate_bias(
    const Image& rgb_linear,
    const DecodedRawChannelMasks& clipping_masks,
    const ZoneMasks& zone_masks) const {
    if (rgb_linear.channels != 3) {
        throw std::invalid_argument("estimate_bias expects a 3-channel RGB image");
    }

    const std::size_t pixel_count = rgb_linear.pixel_count();
    if (clipping_masks.red.size() != pixel_count ||
        clipping_masks.green.size() != pixel_count ||
        clipping_masks.blue.size() != pixel_count) {
        throw std::invalid_argument("estimate_bias expects clipping masks to match the RGB image size");
    }
    for (const auto& zone : zone_masks.zones) {
        if (zone.width != rgb_linear.width || zone.height != rgb_linear.height) {
            throw std::invalid_argument("estimate_bias expects zone masks to match the RGB image dimensions");
        }
    }

    std::vector<OklabPixel> pixels;
    pixels.reserve(pixel_count);

    std::size_t neutral_pixels = 0;
    float neutral_l_sum = 0.0F;
    float neutral_a_sum = 0.0F;
    float neutral_b_sum = 0.0F;

    for (std::size_t i = 0; i < pixel_count; ++i) {
        const OklabPixel pixel = rgb_to_oklab_pixel(
            rgb_linear.pixels[i * 3 + 0],
            rgb_linear.pixels[i * 3 + 1],
            rgb_linear.pixels[i * 3 + 2]);
        pixels.push_back(pixel);

        const bool not_clipped =
            clipping_masks.red[i] == 0U &&
            clipping_masks.green[i] == 0U &&
            clipping_masks.blue[i] == 0U;
        const bool neutral_candidate =
            not_clipped &&
            pixel.l >= 0.15F &&
            pixel.l <= 0.75F &&
            pixel.chroma < 0.035F;
        if (neutral_candidate) {
            ++neutral_pixels;
            neutral_l_sum += pixel.l;
            neutral_a_sum += pixel.a;
            neutral_b_sum += pixel.b;
        }
    }

    CameraBiasAnalysis result;
    result.neutral_confidence = pixel_count > 0
        ? clamp01(static_cast<float>(neutral_pixels) / (static_cast<float>(pixel_count) * 0.005F))
        : 0.0F;

    if (neutral_pixels > 100U) {
        const float denom = static_cast<float>(neutral_pixels);
        result.global_cast_lab = {
            neutral_l_sum / denom,
            neutral_a_sum / denom,
            neutral_b_sum / denom,
        };
    }

    result.shadow_cast_lab = weighted_cast_from_zone(pixels, zone_masks.zones[1]);
    result.midtone_cast_lab = weighted_cast_from_zone(pixels, zone_masks.zones[3]);
    result.highlight_cast_lab = weighted_cast_from_zone(pixels, zone_masks.zones[5]);
    result.warm_cool_bias = result.midtone_cast_lab[2];
    result.green_magenta_bias = result.midtone_cast_lab[1];
    result.blue_excess_index = std::max(-result.shadow_cast_lab[2], 0.0F);
    return result;
}

}  // namespace dfee
