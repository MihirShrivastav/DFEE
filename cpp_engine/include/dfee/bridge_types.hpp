#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "dfee/cuda_runtime.hpp"
#include "dfee/native_error.hpp"

namespace dfee {

struct NativeStageTiming {
    std::string stage;
    double milliseconds = 0.0;
};

struct NativeEngineMetadata {
    std::string engine_version;
    CudaStatus cuda_status;
    bool libraw_enabled = false;
    std::string libraw_version;
    std::vector<NativeStageTiming> timings;
    std::string metadata_json;
};

struct NativeStockSummary {
    std::string stock_id;
    std::string stock_name;
    std::string stock_type;
    std::filesystem::path path;
};

struct NativePrintStockSummary {
    std::string print_stock_id;
    std::string print_stock_name;
    std::filesystem::path path;
};

struct NativeProfilesResponse {
    std::vector<NativeStockSummary> stocks;
    std::vector<NativePrintStockSummary> print_stocks;
    NativeEngineMetadata engine;
};

struct NativeSelectRequest {
    std::string filename;
};

struct NativeSelectResponse {
    bool ok = false;
    std::string filename;
    std::string status;
    std::string message;
    NativeError error;
    NativeEngineMetadata engine;
};

struct NativeRawPreviewRequest {
    std::string filename;
    int max_edge = 1024;
};

struct NativeRawMetadata {
    std::string camera_make;
    std::string camera_model;
    std::string lens_model;
    int iso = 100;
    double shutter_speed = 1.0 / 125.0;
    std::string shutter_speed_str;
    double aperture = 4.0;
    double focal_length = 0.0;
    std::vector<double> white_balance_multipliers{1.0, 1.0, 1.0, 1.0};
    int black_level = 0;
    int white_level = 0;
    int image_height = 0;
    int image_width = 0;
    int raw_height = 0;
    int raw_width = 0;
    std::string metadata_json;
};

struct NativeRawMetadataRequest {
    std::string filename;
};

struct NativeRawMetadataResponse {
    bool ok = false;
    std::string filename;
    std::string status;
    NativeRawMetadata metadata;
    NativeError error;
    NativeEngineMetadata engine;
};

struct NativeRawPreviewResponse {
    bool ok = false;
    std::string filename;
    std::string status;
    std::string content_type = "image/jpeg";
    std::vector<std::uint8_t> jpeg_bytes;
    NativeError error;
    NativeEngineMetadata engine;
};

struct NativePreviewRenderRequest {
    std::string filename;
    std::string stock;
    float exposure = 0.0F;
    float highlights = 0.0F;
    float shadows = 0.0F;
    float blacks = 0.0F;
    float whites = 0.0F;
    float midtones = 0.0F;
    float contrast = 0.0F;
    float temp = 0.0F;
    float tint = 0.0F;
    float saturation = 0.0F;
    float vibrance = 0.0F;
    std::string curves = "[[0,0],[1,1]]";
    float hsl_red_h = 0.0F;
    float hsl_red_s = 0.0F;
    float hsl_red_l = 0.0F;
    float hsl_orange_h = 0.0F;
    float hsl_orange_s = 0.0F;
    float hsl_orange_l = 0.0F;
    float hsl_yellow_h = 0.0F;
    float hsl_yellow_s = 0.0F;
    float hsl_yellow_l = 0.0F;
    float hsl_green_h = 0.0F;
    float hsl_green_s = 0.0F;
    float hsl_green_l = 0.0F;
    float hsl_aqua_h = 0.0F;
    float hsl_aqua_s = 0.0F;
    float hsl_aqua_l = 0.0F;
    float hsl_blue_h = 0.0F;
    float hsl_blue_s = 0.0F;
    float hsl_blue_l = 0.0F;
    float hsl_purple_h = 0.0F;
    float hsl_purple_s = 0.0F;
    float hsl_purple_l = 0.0F;
    float hsl_magenta_h = 0.0F;
    float hsl_magenta_s = 0.0F;
    float hsl_magenta_l = 0.0F;
    float clarity = 0.0F;
    float texture = 0.0F;
    float dehaze = 0.0F;
    float sharpness = 0.0F;
    float sharpness_mask = 0.5F;
    float bloom = 0.0F;
    float adaptation = 1.0F;
    std::string grain = "Auto";
    float grain_strength = -1.0F;
    float grain_size = -1.0F;
    float grain_roughness = -1.0F;
    std::string halation = "Auto";
    float film_color = 100.0F;
    std::string print_stock = "none";
    float print_strength = 1.0F;
    float print_c = 0.0F;
    float print_m = 0.0F;
    float print_y = 0.0F;
    float print_contrast = 0.0F;
    float print_black_point = 0.0F;
};

struct NativePreviewRenderResponse {
    bool ok = false;
    std::string filename;
    std::string status;
    std::string content_type = "image/jpeg";
    std::vector<std::uint8_t> jpeg_bytes;
    NativeError error;
    NativeEngineMetadata engine;
};

struct NativeExportRequest : NativePreviewRenderRequest {
    std::string export_format = "tiff";
};

struct NativeExportResponse {
    bool ok = false;
    std::string filename;
    std::string status;
    std::filesystem::path output_path;
    std::filesystem::path report_path;
    std::string export_format = "tiff";
    NativeError error;
    NativeEngineMetadata engine;
};

}  // namespace dfee
