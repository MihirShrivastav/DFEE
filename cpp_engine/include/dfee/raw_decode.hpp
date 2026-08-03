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

[[nodiscard]] bool is_tiff_filename(const std::string& filename);
[[nodiscard]] DecodedRawImageResponse decode_raw_image_from_file(const NativeRawDecodeRequest& request);
[[nodiscard]] NativeRawDecodeResponse decode_raw_from_file(const NativeRawDecodeRequest& request);

// Fast, session-free thumbnail for the library grid/filmstrip. Uses the RAW's
// embedded preview (LibRaw) or a downscaled TIFF read, oriented and re-encoded as a
// small JPEG with its long edge ~max_edge. Independent of the render session.
struct ThumbnailResponse {
    bool ok = false;
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> jpeg_bytes;
    std::string error;
};
[[nodiscard]] ThumbnailResponse extract_thumbnail_jpeg(const std::string& filename, int max_edge);

}  // namespace dfee
