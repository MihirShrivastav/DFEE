#pragma once

#include "dfee/bridge_types.hpp"
#include "dfee/image.hpp"

#include <cstdint>
#include <vector>

namespace dfee {

struct DecodedRawChannelMasks {
    std::vector<std::uint8_t> red;
    std::vector<std::uint8_t> green;
    std::vector<std::uint8_t> blue;
};

struct DecodedRawImage {
    Image rgb_linear;
    LuminanceImage luminance;
    DecodedRawChannelMasks clipping_masks;
    NativeRawMetadata metadata;
    NativeRawDecodeSummary summary;
};

struct DecodedRawImageResponse {
    bool ok = false;
    std::string filename;
    std::string status;
    DecodedRawImage decoded;
    NativeError error;
};

[[nodiscard]] DecodedRawImageResponse decode_raw_image_from_file(const NativeRawDecodeRequest& request);
[[nodiscard]] NativeRawDecodeResponse decode_raw_from_file(const NativeRawDecodeRequest& request);

}  // namespace dfee
