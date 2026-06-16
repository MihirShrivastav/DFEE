#include "dfee/raw_decode.hpp"

#include "dfee/bridge_utils.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <sstream>
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

[[nodiscard]] std::string format_shutter_speed(const double shutter_speed) {
    if (shutter_speed <= 0.0) {
        return {};
    }
    if (shutter_speed < 1.0) {
        const auto denominator = static_cast<int>(std::round(1.0 / shutter_speed));
        if (denominator > 0) {
            return "1/" + std::to_string(denominator);
        }
    }

    std::ostringstream out;
    out.setf(std::ios::fixed);
    out.precision(shutter_speed < 10.0 ? 3 : 2);
    out << shutter_speed << "s";
    return out.str();
}

[[nodiscard]] std::string safe_cstr(const char* value) {
    return (value != nullptr && value[0] != '\0') ? std::string(value) : std::string{};
}

#if DFEE_HAS_LIBRAW
NativeError make_libraw_decode_error(
    const int err,
    const std::string& filename,
    const std::string& fallback_code,
    const std::string& fallback_user_message,
    const std::string& fallback_detail_prefix) {
    const std::string error_text = libraw_strerror(err);
    if (err == LIBRAW_FILE_UNSUPPORTED) {
        return {
            .code = "LIBRAW_UNSUPPORTED_RAW",
            .user_message = "The selected file is not a supported RAW format.",
            .detail = "LibRaw reported an unsupported RAW file for " + filename + ": " + error_text,
        };
    }

    if (fallback_code == "LIBRAW_UNPACK_FAILED" || fallback_code == "LIBRAW_PROCESS_FAILED" || fallback_code == "LIBRAW_MEM_IMAGE_FAILED") {
        return {
            .code = "LIBRAW_CORRUPT_RAW",
            .user_message = "The RAW file appears to be corrupt or incomplete.",
            .detail = fallback_detail_prefix + " for " + filename + ": " + error_text,
        };
    }

    return {
        .code = fallback_code,
        .user_message = fallback_user_message,
        .detail = fallback_detail_prefix + " for " + filename + ": " + error_text,
    };
}

void fill_metadata_from_raw_processor(NativeRawMetadata& metadata, const LibRaw& raw_processor) {
    const auto& idata = raw_processor.imgdata.idata;
    const auto& lens = raw_processor.imgdata.lens;
    const auto& other = raw_processor.imgdata.other;
    const auto& color = raw_processor.imgdata.color;
    const auto& sizes = raw_processor.imgdata.sizes;

    metadata.camera_make = safe_cstr(idata.make);
    metadata.camera_model = safe_cstr(idata.model);
    metadata.lens_model = safe_cstr(lens.Lens);
    metadata.iso = other.iso_speed > 0.0F ? static_cast<int>(std::lround(other.iso_speed)) : 100;
    metadata.shutter_speed = other.shutter > 0.0F ? other.shutter : (1.0 / 125.0);
    metadata.shutter_speed_str = format_shutter_speed(metadata.shutter_speed);
    metadata.aperture = other.aperture > 0.0F ? other.aperture : 4.0;
    metadata.focal_length = other.focal_len > 0.0F ? other.focal_len : 0.0;
    metadata.white_balance_multipliers = {
        static_cast<double>(color.cam_mul[0] > 0.0F ? color.cam_mul[0] : 1.0F),
        static_cast<double>(color.cam_mul[1] > 0.0F ? color.cam_mul[1] : 1.0F),
        static_cast<double>(color.cam_mul[2] > 0.0F ? color.cam_mul[2] : 1.0F),
        static_cast<double>(color.cam_mul[3] > 0.0F ? color.cam_mul[3] : 1.0F),
    };
    metadata.black_level = color.cblack[0] > 0 ? static_cast<int>(color.cblack[0]) : static_cast<int>(color.black);
    metadata.white_level = static_cast<int>(color.maximum);
    metadata.image_height = static_cast<int>(sizes.height);
    metadata.image_width = static_cast<int>(sizes.width);
    metadata.raw_height = static_cast<int>(sizes.raw_height);
    metadata.raw_width = static_cast<int>(sizes.raw_width);
    metadata.metadata_json = serialize_native_raw_metadata_json(metadata);
}
#endif

void fill_decoded_image_from_float_rgb(
    DecodedRawImage& decoded,
    const std::vector<float>& pixels,
    const int width,
    const int height,
    const int channels) {
    decoded.rgb_linear = Image(width, height, channels);
    decoded.rgb_linear.pixels = pixels;
    decoded.luminance = compute_luminance(decoded.rgb_linear);
    decoded.clipping_masks.red.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), 0);
    decoded.clipping_masks.green.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), 0);
    decoded.clipping_masks.blue.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), 0);

    decoded.summary.image_width = width;
    decoded.summary.image_height = height;
    decoded.summary.channels = channels;

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
        const std::uint8_t is_clip_r = r >= 0.99F ? 1 : 0;
        const std::uint8_t is_clip_g = g >= 0.99F ? 1 : 0;
        const std::uint8_t is_clip_b = b >= 0.99F ? 1 : 0;
        decoded.clipping_masks.red[i] = is_clip_r;
        decoded.clipping_masks.green[i] = is_clip_g;
        decoded.clipping_masks.blue[i] = is_clip_b;
        clip_r += is_clip_r;
        clip_g += is_clip_g;
        clip_b += is_clip_b;
    }

    decoded.summary.min_value = std::isfinite(min_value) ? min_value : 0.0F;
    decoded.summary.max_value = std::isfinite(max_value) ? max_value : 0.0F;
    decoded.summary.clipping_ratio_r = pixel_count > 0 ? static_cast<float>(clip_r) / static_cast<float>(pixel_count) : 0.0F;
    decoded.summary.clipping_ratio_g = pixel_count > 0 ? static_cast<float>(clip_g) / static_cast<float>(pixel_count) : 0.0F;
    decoded.summary.clipping_ratio_b = pixel_count > 0 ? static_cast<float>(clip_b) / static_cast<float>(pixel_count) : 0.0F;
    decoded.summary.raw_clipping_ratio = std::max({
        decoded.summary.clipping_ratio_r,
        decoded.summary.clipping_ratio_g,
        decoded.summary.clipping_ratio_b,
    });
    decoded.summary.summary_json = serialize_native_raw_decode_summary_json(decoded.summary);
}

}  // namespace

DecodedRawImageResponse decode_raw_image_from_file(const NativeRawDecodeRequest& request) {
    DecodedRawImageResponse response;
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
        response.error = make_libraw_decode_error(
            err,
            request.filename,
            "LIBRAW_OPEN_FAILED",
            "The RAW file could not be opened by LibRaw.",
            "LibRaw open_file failed");
        if (response.error.code == "LIBRAW_UNSUPPORTED_RAW") {
            response.status = "unsupported";
        }
        return response;
    }
    fill_metadata_from_raw_processor(response.decoded.metadata, raw_processor);

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
        response.error = make_libraw_decode_error(
            err,
            request.filename,
            "LIBRAW_UNPACK_FAILED",
            "The RAW file could not be unpacked by LibRaw.",
            "LibRaw unpack failed");
        return response;
    }

    err = raw_processor.dcraw_process();
    if (err != LIBRAW_SUCCESS) {
        response.status = "error";
        response.error = make_libraw_decode_error(
            err,
            request.filename,
            "LIBRAW_PROCESS_FAILED",
            "The RAW file could not be processed by LibRaw.",
            "LibRaw dcraw_process failed");
        return response;
    }

    libraw_processed_image_t* image = raw_processor.dcraw_make_mem_image(&err);
    if (image == nullptr || err != LIBRAW_SUCCESS) {
        response.status = "error";
        response.error = make_libraw_decode_error(
            err,
            request.filename,
            "LIBRAW_MEM_IMAGE_FAILED",
            "The RAW file could not be converted into an image buffer.",
            "LibRaw dcraw_make_mem_image failed");
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
    fill_decoded_image_from_float_rgb(response.decoded, pixels, width, height, channels);
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

NativeRawDecodeResponse decode_raw_from_file(const NativeRawDecodeRequest& request) {
    const auto decoded_response = decode_raw_image_from_file(request);
    NativeRawDecodeResponse response;
    response.ok = decoded_response.ok;
    response.filename = decoded_response.filename;
    response.status = decoded_response.status;
    response.summary = decoded_response.decoded.summary;
    response.metadata = decoded_response.decoded.metadata;
    response.error = decoded_response.error;
    return response;
}

}  // namespace dfee
