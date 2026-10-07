#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "dfee/bridge_types.hpp"
#include "dfee/cuda_runtime.hpp"
#include "dfee/raw_decode.hpp"
#include "dfee/profile.hpp"
#include "dfee/raw_metadata.hpp"
#include "dfee/solver.hpp"

namespace dfee {

// Pipeline classification helpers. These are free functions so that tests and
// other translation units can call them independently of EngineSession.

// Returns true when the given effect_pipeline_version runs the subtractive
// colour-density stages (filmic_v3 and filmic_v4).
[[nodiscard]] bool is_subtractive_effect_pipeline(const std::string& value);

// Returns true only when the pipeline uses the characteristic-curve path
// (filmic_v4 and later).
[[nodiscard]] bool is_characteristic_curve_pipeline(const std::string& value);

// Returns nullopt when the version is accepted, or a NativeError otherwise.
[[nodiscard]] std::optional<NativeError> validate_effect_pipeline_version(const std::string& value);

class EngineSession {
public:
    explicit EngineSession(std::filesystem::path project_root);

    [[nodiscard]] const std::filesystem::path& project_root() const noexcept;
    [[nodiscard]] NativeProfilesResponse list_profiles() const;
    [[nodiscard]] NativeSelectResponse select_file(const NativeSelectRequest& request);
    [[nodiscard]] NativeRawMetadataResponse read_raw_metadata(const NativeRawMetadataRequest& request) const;
    [[nodiscard]] NativeRawDecodeResponse decode_raw(const NativeRawDecodeRequest& request);
    [[nodiscard]] NativeRawPreviewResponse raw_preview(const NativeRawPreviewRequest& request);
    [[nodiscard]] NativeGrainResolutionResponse resolve_auto_grain(const NativePreviewRenderRequest& request);
    [[nodiscard]] NativePreviewRenderResponse render_preview(const NativePreviewRenderRequest& request);
    [[nodiscard]] NativeLookProxyResponse render_look_proxy(const NativeLookProxyRequest& request);
    [[nodiscard]] NativeExportResponse export_image(const NativeExportRequest& request);
    [[nodiscard]] NativeSessionCacheStateResponse cache_state() const;
    [[nodiscard]] CudaStatus cuda_status() const noexcept;

    // Returns true when the given effect_pipeline_version is accepted by this
    // native engine build (parity_v1, filmic_v2, filmic_v3, filmic_v4). Empty
    // resolves to the default (parity_v1).
    [[nodiscard]] static bool is_effect_pipeline_supported(const std::string& version);

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
        bool rendered_input = false;  // display-referred source (TIFF or developed RAW)
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

    struct CachedExportAnalysis {
        std::string filename;
        SolverInput solver_input;
        ZoneMasks zone_masks;
        SpatialMasks spatial_masks;
    };

    // The preview source downscaled for proxies (one photo, one size at a time).
    struct CachedProxySource {
        std::string filename;
        int max_edge = 0;
        Image rgb_linear;
        ZoneMasks zone_masks;
        SpatialMasks spatial_masks;
    };

    template <typename Profile>
    struct CachedProfile {
        std::filesystem::file_time_type mtime;
        Profile profile;
    };

    [[nodiscard]] std::string resolve_filename(const std::string& filename) const;
    void populate_preview_analysis_cache(
        const std::string& filename,
        SolverInput& solver_input,
        ZoneMasks& zone_masks,
        SpatialMasks& spatial_masks);
    void enforce_session_cache_budget();
    void clear_decode_caches();
    void refresh_preview_cache_from_draft();
    // Parsed profiles, cached by id and re-read when the YAML's mtime changes.
    [[nodiscard]] FilmStockProfile film_profile(const std::string& stock_id);
    [[nodiscard]] PrintStockProfile print_profile(const std::string& print_stock_id);
    // A cached render source: the scene-linear working image plus the analysis that
    // drives the solver and the masks at the same resolution.
    struct PipelineSource {
        const Image* rgb_linear = nullptr;
        bool rendered_input = false;
        const SolverInput* solver_input = nullptr;
        const ZoneMasks* zone_masks = nullptr;
        const SpatialMasks* spatial_masks = nullptr;
    };
    struct PipelineOptions {
        std::string stage_prefix = "render_preview";  // stage timer names: <prefix>_<stage>
        bool include_grain = true;
        bool apply_geometry = true;
        bool dump_stages = true;                       // DFEE_STAGE_DUMP debugging
    };
    // The film look: profiles -> plan -> film stages -> post -> geometry. Returns the
    // final scene-linear image, or nullopt with `error` set when a profile cannot load.
    [[nodiscard]] std::optional<Image> run_film_pipeline(
        const NativePreviewRenderRequest& request,
        const std::string& filename,
        const PipelineSource& source,
        const PipelineOptions& options,
        NativeEngineMetadata& engine,
        NativeError& error);
    // "No film": neutral scene placement, baseline develop, post and geometry.
    [[nodiscard]] Image run_neutral_pipeline(
        const NativePreviewRenderRequest& request,
        const PipelineSource& source,
        const PipelineOptions& options,
        NativeEngineMetadata& engine);
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
    std::optional<CachedExportAnalysis> export_analysis_cache_;
    std::unordered_map<std::string, CachedProfile<FilmStockProfile>> film_profile_cache_;
    std::unordered_map<std::string, CachedProfile<PrintStockProfile>> print_profile_cache_;
    std::size_t profile_loads_ = 0;
    std::optional<CachedProxySource> proxy_source_cache_;
};

}  // namespace dfee
