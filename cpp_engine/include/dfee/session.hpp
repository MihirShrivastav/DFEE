#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "dfee/bridge_types.hpp"
#include "dfee/cuda_runtime.hpp"
#include "dfee/raw_decode.hpp"
#include "dfee/profile.hpp"
#include "dfee/raw_metadata.hpp"
#include "dfee/solver.hpp"

namespace dfee {

class EngineSession {
public:
    explicit EngineSession(std::filesystem::path project_root);

    [[nodiscard]] const std::filesystem::path& project_root() const noexcept;
    [[nodiscard]] NativeProfilesResponse list_profiles() const;
    [[nodiscard]] NativeSelectResponse select_file(const NativeSelectRequest& request);
    [[nodiscard]] NativeRawMetadataResponse read_raw_metadata(const NativeRawMetadataRequest& request) const;
    [[nodiscard]] NativeRawDecodeResponse decode_raw(const NativeRawDecodeRequest& request);
    [[nodiscard]] NativeRawPreviewResponse raw_preview(const NativeRawPreviewRequest& request);
    [[nodiscard]] NativePreviewRenderResponse render_preview(const NativePreviewRenderRequest& request);
    [[nodiscard]] NativeExportResponse export_image(const NativeExportRequest& request);
    [[nodiscard]] NativeSessionCacheStateResponse cache_state() const;
    [[nodiscard]] CudaStatus cuda_status() const noexcept;

private:
    struct CachedDecode {
        std::string filename;
        bool draft_mode = true;
        DecodedRawImage decoded;
    };

    struct CachedPreview {
        std::string filename;
        Image rgb_linear;
        LuminanceImage luminance;
    };

    struct CachedRawPreviewJpeg {
        std::string filename;
        int max_edge = 1024;
        std::vector<std::uint8_t> jpeg_bytes;
    };

    struct CachedPreviewAnalysis {
        std::string filename;
        SolverInput solver_input;
        ZoneMasks zone_masks;
        SpatialMasks spatial_masks;
    };

    [[nodiscard]] std::string resolve_filename(const std::string& filename) const;
    void clear_decode_caches();
    void refresh_preview_cache_from_draft();
    [[nodiscard]] NativeRawPreviewResponse encode_raw_preview(const std::string& filename, int max_edge) const;

    std::filesystem::path project_root_;
    std::filesystem::path raw_dir_;
    std::filesystem::path stocks_dir_;
    std::filesystem::path print_stocks_dir_;
    std::string selected_filename_;
    std::optional<CachedDecode> draft_decode_cache_;
    std::optional<CachedDecode> full_decode_cache_;
    std::optional<CachedPreview> preview_cache_;
    std::optional<CachedRawPreviewJpeg> raw_preview_jpeg_cache_;
    std::optional<CachedPreviewAnalysis> preview_analysis_cache_;
};

}  // namespace dfee
