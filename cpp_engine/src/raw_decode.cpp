#include "dfee/raw_decode.hpp"

#include "dfee/bridge_utils.hpp"
#include "dfee/raw_metadata.hpp"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <string>

#if DFEE_HAS_LIBRAW
#if __has_include(<libraw/libraw.h>)
#include <libraw/libraw.h>
#else
#include <libraw.h>
#endif
#endif

namespace dfee {
namespace {

void fill_summary_from_float_rgb(
    NativeRawDecodeSummary& summary,
    const std::vector<float>& pixels,
    const int width,
    const int height,
    const int channels) {
    summary.image_width = width;
    summary.image_height = height;
    summary.channels = channels;

    float min_value = std::numeric_limits<float>::max();
    float max_value = std::numeric_limits<float>::lowest();
    std::size_t clip_r = 0;
    std::size_t clip_g = 0;
    std::size_t clip_b = 0;
    const std::size_t pixel_count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);

    for (std::size_t i = 0; i < pixel_count; ++i) {
        const float r = pixels[i * 3 + 0];
        const float g = pixels[i * 3 + 1];
        const float b = pixels[i * 3 + 2];
        min_value = std::min({min_value, r, g, b});
        max_value = std::max({max_value, r, g, b});
        clip_r += r >= 0.99F ? 1 : 0;
        clip_g += g >= 0.99F ? 1 : 0;
        clip_b += b >= 0.99F ? 1 : 0;
    }

    summary.min_value = std::isfinite(min_value) ? min_value : 0.0F;
    summary.max_value = std::isfinite(max_value) ? max_value : 0.0F;
    summary.clipping_ratio_r = pixel_count > 0 ? static_cast<float>(clip_r) / static_cast<float>(pixel_count) : 0.0F;
    summary.clipping_ratio_g = pixel_count > 0 ? static_cast<float>(clip_g) / static_cast<float>(pixel_count) : 0.0F;
    summary.clipping_ratio_b = pixel_count > 0 ? static_cast<float>(clip_b) / static_cast<float>(pixel_count) : 0.0F;
    summary.raw_clipping_ratio = std::max({summary.clipping_ratio_r, summary.clipping_ratio_g, summary.clipping_ratio_b});
    summary.summary_json = serialize_native_raw_decode_summary_json(summary);
}

}  // namespace

NativeRawDecodeResponse decode_raw_from_file(const NativeRawDecodeRequest& request) {
    NativeRawDecodeResponse response;
    response.filename = request.filename;

    if (request.filename.empty()) {
        response.status = "error";
        response.error = {
            .code = "RAW_FILENAME_MISSING",
            .user_message = "Select a RAW file before continuing.",
            .detail = "decode_raw_from_file received an empty filename.",
        };
        return response;
    }

#if DFEE_HAS_LIBRAW
    if (!std::filesystem::is_regular_file(request.filename)) {
        response.status = "not_found";
        response.error = {
            .code = "RAW_FILE_NOT_FOUND",
            .user_message = "The requested RAW file was not found.",
            .detail = "Expected RAW file at: " + request.filename,
        };
        return response;
    }

    LibRaw raw_processor;
    int err = raw_processor.open_file(request.filename.c_str());
    if (err != LIBRAW_SUCCESS) {
        response.status = "error";
        response.error = {
            .code = "LIBRAW_OPEN_FAILED",
            .user_message = "The RAW file could not be opened by LibRaw.",
            .detail = std::string("LibRaw open_file failed for ") + request.filename,
        };
        return response;
    }

    auto* params = raw_processor.output_params_ptr();
    params->half_size = request.draft_mode ? 1 : 0;
    params->no_auto_bright = 1;
    params->use_camera_wb = 1;
    params->output_color = LIBRAW_COLORSPACE_sRGB;
    params->output_bps = 16;
    params->gamm[0] = 1.0;
    params->gamm[1] = 1.0;

    err = raw_processor.unpack();
    if (err != LIBRAW_SUCCESS) {
        response.status = "error";
        response.error = {
            .code = "LIBRAW_UNPACK_FAILED",
            .user_message = "The RAW file could not be unpacked by LibRaw.",
            .detail = std::string("LibRaw unpack failed for ") + request.filename,
        };
        return response;
    }

    err = raw_processor.dcraw_process();
    if (err != LIBRAW_SUCCESS) {
        response.status = "error";
        response.error = {
            .code = "LIBRAW_PROCESS_FAILED",
            .user_message = "The RAW file could not be processed by LibRaw.",
            .detail = std::string("LibRaw dcraw_process failed for ") + request.filename,
        };
        return response;
    }

    libraw_processed_image_t* image = raw_processor.dcraw_make_mem_image(&err);
    if (image == nullptr || err != LIBRAW_SUCCESS) {
        response.status = "error";
        response.error = {
            .code = "LIBRAW_MEM_IMAGE_FAILED",
            .user_message = "The RAW file could not be converted into an image buffer.",
            .detail = std::string("LibRaw dcraw_make_mem_image failed for ") + request.filename,
        };
        return response;
    }

    const int width = image->width;
    const int height = image->height;
    const int channels = image->colors;
    const int bits = image->bits;
    std::vector<float> pixels(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * static_cast<std::size_t>(channels), 0.0F);

    if (bits == 16) {
        const auto* source = reinterpret_cast<const std::uint16_t*>(image->data);
        const float scale = 1.0F / 65535.0F;
        for (std::size_t i = 0; i < pixels.size(); ++i) {
            pixels[i] = std::clamp(static_cast<float>(source[i]) * scale, 0.0F, 1.0F);
        }
    } else if (bits == 8) {
        const auto* source = reinterpret_cast<const std::uint8_t*>(image->data);
        const float scale = 1.0F / 255.0F;
        for (std::size_t i = 0; i < pixels.size(); ++i) {
            pixels[i] = std::clamp(static_cast<float>(source[i]) * scale, 0.0F, 1.0F);
        }
    } else {
        LibRaw::dcraw_clear_mem(image);
        response.status = "error";
        response.error = {
            .code = "LIBRAW_UNSUPPORTED_BPS",
            .user_message = "LibRaw returned an unsupported output bit depth.",
            .detail = "Expected 8-bit or 16-bit RGB output from LibRaw.",
        };
        return response;
    }

    response.ok = true;
    response.status = "loaded";
    response.metadata = read_raw_metadata_from_file({.filename = request.filename}).metadata;
    fill_summary_from_float_rgb(response.summary, pixels, width, height, channels);
    LibRaw::dcraw_clear_mem(image);
    return response;
#else
    response.status = "unavailable";
    response.error = {
        .code = "LIBRAW_UNAVAILABLE",
        .user_message = "Native RAW decode is not available in this build.",
        .detail = "DFEE was built without LibRaw discovery; configure with the windows-msvc-vcpkg preset.",
    };
    return response;
#endif
}

}  // namespace dfee
