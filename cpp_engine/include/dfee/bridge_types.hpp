#pragma once

#include <array>
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
    std::string color_space = "srgb"; // for TIFF/rendered inputs: srgb | adobe_rgb | prophoto
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

struct NativeSelectDiagnostics {
    std::string tonal_skew = "normal";
    float dynamic_range_stops = 0.0F;
    float midtone_anchor = 0.18F;
    float highlight_headroom = 0.0F;
    float shadow_depth = 0.0F;
    float neon_risk = 0.0F;
    std::array<std::string, 3> dominant_hues{"Red", "Orange", "Yellow"};
    float palette_entropy = 0.0F;
    float specular_ratio = 0.0F;
    float neutral_confidence = 0.0F;
};

struct NativeSelectResponse {
    bool ok = false;
    std::string filename;
    std::string status;
    std::string message;
    NativeRawMetadata metadata;
    NativeSelectDiagnostics diagnostics;
    NativeError error;
    NativeEngineMetadata engine;
};

struct NativeRawPreviewRequest {
    std::string filename;
    int max_edge = 1024;
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

struct NativeRawDecodeRequest {
    std::string filename;
    bool draft_mode = true;
    std::string color_space = "srgb"; // for TIFF/rendered inputs: srgb | adobe_rgb | prophoto
};

struct NativeRawDecodeSummary {
    int image_width = 0;
    int image_height = 0;
    int channels = 3;
    float min_value = 0.0F;
    float max_value = 0.0F;
    float clipping_ratio_r = 0.0F;
    float clipping_ratio_g = 0.0F;
    float clipping_ratio_b = 0.0F;
    float raw_clipping_ratio = 0.0F;
    std::string summary_json;
};

struct NativeRawDecodeResponse {
    bool ok = false;
    std::string filename;
    std::string status;
    NativeRawDecodeSummary summary;
    NativeRawMetadata metadata;
    NativeError error;
    NativeEngineMetadata engine;
};

struct NativeSessionCacheState {
    std::string selected_filename;
    bool draft_decode_cached = false;
    int draft_width = 0;
    int draft_height = 0;
    std::size_t draft_decode_bytes = 0;
    bool preview_cached = false;
    int preview_width = 0;
    int preview_height = 0;
    std::size_t preview_bytes = 0;
    bool raw_preview_jpeg_cached = false;
    std::size_t raw_preview_jpeg_bytes = 0;
    bool preview_analysis_cached = false;
    std::size_t preview_analysis_bytes = 0;
    bool full_decode_cached = false;
    int full_width = 0;
    int full_height = 0;
    std::size_t full_decode_bytes = 0;
    bool export_analysis_cached = false;
    std::size_t export_analysis_bytes = 0;
    std::size_t total_estimated_bytes = 0;
    std::size_t cache_budget_bytes = 0;
};

struct NativeSessionCacheStateResponse {
    bool ok = false;
    NativeSessionCacheState cache;
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
    std::string effect_pipeline_version = "parity_v1";
    float exposure = 0.0F;
    std::string exposure_placement = "as_shot";
    float film_exposure_ev = 0.0F;
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
    // Color grading (perceptual 3-way + global). 0 = neutral.
    float cg_shadow_hue = 0.0F;
    float cg_shadow_sat = 0.0F;
    float cg_shadow_lum = 0.0F;
    float cg_midtone_hue = 0.0F;
    float cg_midtone_sat = 0.0F;
    float cg_midtone_lum = 0.0F;
    float cg_highlight_hue = 0.0F;
    float cg_highlight_sat = 0.0F;
    float cg_highlight_lum = 0.0F;
    float cg_global_hue = 0.0F;
    float cg_global_sat = 0.0F;
    float cg_global_lum = 0.0F;
    float cg_balance = 0.0F;
    float cg_blending = 0.0F;
    float cg_crossbalance = 0.0F;
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
    float highlight_color_hold = 0.0F;
    float shadow_color_retention = 0.0F;
    float palette_range = 0.0F;
    float emulsion_color_density = 0.0F;
    float film_color_density = 100.0F;
    float film_color_compression = 100.0F;
    float highlight_rolloff = 100.0F;
    float film_contrast = 100.0F;
    // Master stock-tone control. 100 = the profile's authored baseline.
    float profile_strength = 100.0F;
    // Rendered-input (TIFF) handling: how much to trust the TIFF's baked exposure/tone
    // and apply the film's own tone subtly. 0 = full RAW-style auto exposure + tone,
    // 100 = fully trust the TIFF (film tone applied gently). Ignored for RAW inputs.
    float rendered_input = 80.0F;
    bool adaptive = true;
    float halation_strength = 100.0F;
    float halation_threshold = 50.0F;
    float shadow_lift = 0.0F;  // -100..100 bipolar; 0 = stock's natural base-fog floor
    std::string print_stock = "none";
    float print_strength = 1.0F;
    float print_c = 0.0F;
    float print_m = 0.0F;
    float print_y = 0.0F;
    float print_contrast = 0.0F;
    float print_black_point = 0.0F;
    // Geometry (Phase 1): a final spatial transform applied after the full render.
    // Composition order: flip -> 90-degree quadrant rotate -> straighten (fine angle,
    // auto-cropped to the largest inscribed rect) -> normalized crop. crop_* are in
    // [0,1] on the flipped/rotated/straightened image. Identity defaults = whole frame.
    float crop_x = 0.0F;
    float crop_y = 0.0F;
    float crop_w = 1.0F;
    float crop_h = 1.0F;
    float straighten_deg = 0.0F;  // -45..45, positive rotates the image clockwise
    int rotate_quadrant = 0;      // 0..3, number of 90-degree clockwise rotations
    bool flip_h = false;
    bool flip_v = false;
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

// The solver-resolved camera-grain values for the active RAW/stock pair.
// This lets the UI materialize Auto grain into equivalent Custom controls
// without rendering another preview or reimplementing stock/ISO logic in JS.
struct NativeGrainResolutionResponse {
    bool ok = false;
    std::string filename;
    std::string stock;
    std::string status;
    float grain_strength = 0.0F;
    float grain_size = 0.0F;
    float grain_roughness = 0.0F;
    NativeError error;
    NativeEngineMetadata engine;
};

struct NativeExportRequest : NativePreviewRenderRequest {
    std::string export_format = "tiff";
    int jpeg_quality = 92;
    int export_dpi = 300;
    bool embed_metadata = true;
    std::string export_color_space = "srgb";
    // Empty keeps the standard sibling-file naming policy. A caller that owns
    // a working file (for example Lightroom external editing) may request an
    // exact destination. The exporter writes a temporary sibling first and
    // replaces this path only after encoding and metadata work both succeed.
    std::filesystem::path output_path;
};

struct NativeExportResponse {
    bool ok = false;
    std::string filename;
    std::string status;
    std::filesystem::path output_path;
    std::filesystem::path report_path;
    std::string export_format = "tiff";
    std::string format_label;
    NativeError error;
    NativeEngineMetadata engine;
};

}  // namespace dfee
