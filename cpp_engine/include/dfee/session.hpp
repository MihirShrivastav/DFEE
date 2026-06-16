#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include "dfee/bridge_types.hpp"
#include "dfee/cuda_runtime.hpp"
#include "dfee/raw_decode.hpp"
#include "dfee/profile.hpp"
#include "dfee/raw_metadata.hpp"

namespace dfee {

class EngineSession {
public:
    explicit EngineSession(std::filesystem::path project_root);

    [[nodiscard]] const std::filesystem::path& project_root() const noexcept;
    [[nodiscard]] NativeProfilesResponse list_profiles() const;
    [[nodiscard]] NativeSelectResponse select_file(const NativeSelectRequest& request);
    [[nodiscard]] NativeRawMetadataResponse read_raw_metadata(const NativeRawMetadataRequest& request) const;
    [[nodiscard]] NativeRawDecodeResponse decode_raw(const NativeRawDecodeRequest& request);
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

    [[nodiscard]] std::string resolve_filename(const std::string& filename) const;
    void clear_decode_caches();
    void refresh_preview_cache_from_draft();

    std::filesystem::path project_root_;
    std::filesystem::path raw_dir_;
    std::filesystem::path stocks_dir_;
    std::filesystem::path print_stocks_dir_;
    std::string selected_filename_;
    std::optional<CachedDecode> draft_decode_cache_;
    std::optional<CachedDecode> full_decode_cache_;
    std::optional<CachedPreview> preview_cache_;
};

}  // namespace dfee
