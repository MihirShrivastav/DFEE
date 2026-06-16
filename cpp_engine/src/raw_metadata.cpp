#include "dfee/raw_metadata.hpp"

#include "dfee/bridge_utils.hpp"
#include "dfee/version.hpp"

#include <cmath>
#include <filesystem>
#include <sstream>

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

NativeError make_libraw_metadata_error(const int err, const std::string& filename) {
    const std::string error_text = libraw_strerror(err);
    if (err == LIBRAW_FILE_UNSUPPORTED) {
        return {
            .code = "LIBRAW_UNSUPPORTED_RAW",
            .user_message = "The selected file is not a supported RAW format.",
            .detail = "LibRaw reported an unsupported RAW file for " + filename + ": " + error_text,
        };
    }

    return {
        .code = "LIBRAW_OPEN_FAILED",
        .user_message = "The RAW file could not be opened by LibRaw.",
        .detail = "LibRaw open_file failed for " + filename + ": " + error_text,
    };
}

}  // namespace

NativeRawMetadataResponse read_raw_metadata_from_file(const NativeRawMetadataRequest& request) {
    NativeRawMetadataResponse response;
    response.filename = request.filename;

    if (request.filename.empty()) {
        response.status = "error";
        response.error = {
            .code = "RAW_FILENAME_MISSING",
            .user_message = "Select a RAW file before continuing.",
            .detail = "read_raw_metadata received an empty filename.",
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
    const int open_result = raw_processor.open_file(request.filename.c_str());
    if (open_result != LIBRAW_SUCCESS) {
        response.status = open_result == LIBRAW_FILE_UNSUPPORTED ? "unsupported" : "error";
        response.error = make_libraw_metadata_error(open_result, request.filename);
        return response;
    }

    const auto& idata = raw_processor.imgdata.idata;
    const auto& lens = raw_processor.imgdata.lens;
    const auto& other = raw_processor.imgdata.other;
    const auto& color = raw_processor.imgdata.color;
    const auto& sizes = raw_processor.imgdata.sizes;

    response.ok = true;
    response.status = "loaded";
    response.metadata.camera_make = safe_cstr(idata.make);
    response.metadata.camera_model = safe_cstr(idata.model);
    response.metadata.lens_model = safe_cstr(lens.Lens);
    response.metadata.iso = other.iso_speed > 0.0F ? static_cast<int>(std::lround(other.iso_speed)) : 100;
    response.metadata.shutter_speed = other.shutter > 0.0F ? other.shutter : (1.0 / 125.0);
    response.metadata.shutter_speed_str = format_shutter_speed(response.metadata.shutter_speed);
    response.metadata.aperture = other.aperture > 0.0F ? other.aperture : 4.0;
    response.metadata.focal_length = other.focal_len > 0.0F ? other.focal_len : 0.0;
    response.metadata.white_balance_multipliers = {
        static_cast<double>(color.cam_mul[0] > 0.0F ? color.cam_mul[0] : 1.0F),
        static_cast<double>(color.cam_mul[1] > 0.0F ? color.cam_mul[1] : 1.0F),
        static_cast<double>(color.cam_mul[2] > 0.0F ? color.cam_mul[2] : 1.0F),
        static_cast<double>(color.cam_mul[3] > 0.0F ? color.cam_mul[3] : 1.0F),
    };
    response.metadata.black_level = color.cblack[0] > 0 ? static_cast<int>(color.cblack[0]) : static_cast<int>(color.black);
    response.metadata.white_level = static_cast<int>(color.maximum);
    response.metadata.image_height = static_cast<int>(sizes.height);
    response.metadata.image_width = static_cast<int>(sizes.width);
    response.metadata.raw_height = static_cast<int>(sizes.raw_height);
    response.metadata.raw_width = static_cast<int>(sizes.raw_width);
    response.metadata.metadata_json = serialize_native_raw_metadata_json(response.metadata);
    return response;
#else
    response.status = "unavailable";
    response.error = {
        .code = "LIBRAW_UNAVAILABLE",
        .user_message = "Native RAW metadata extraction is not available in this build.",
        .detail = "DFEE was built without LibRaw discovery; configure with the windows-msvc-vcpkg preset.",
    };
    return response;
#endif
}

}  // namespace dfee
